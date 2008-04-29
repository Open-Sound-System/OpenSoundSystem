/*
 * Purpose: Driver for the UltraSparc workstations using CS4231 codec for audio
 *
 */

#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 1997-2008. All rights reserved.

#include "oss_audiocs_cfg.h"
#include <oss_pci.h>

#include "cs4231_mixer.h"

// TODO: Remove this private definition of DDB()
#undef DDB
#define DDB(x) x

typedef struct
{
  oss_device_t *osdev;
  oss_mutex_t mutex;
  oss_mutex_t low_mutex;
  oss_native_word base;
  oss_native_word auxio_base;
  unsigned char MCE_bit;
  unsigned char saved_regs[32];

  int audio_flags;
  int record_dev, playback_dev;
  int speed;
  unsigned char speed_bits;
  int channels;
  int audio_format;
  unsigned char format_bits;

  int xfer_count;
  int audio_mode;
  int open_mode;
  char *chip_name, *name;
  int model;
#define MD_1848		1
#define MD_4231		2
#define MD_4231A	3

  /* Mixer parameters */
  int is_muted;
  int recmask;
  int supported_devices, orig_devices;
  int supported_rec_devices, orig_rec_devices;
  int *levels;
  short mixer_reroute[32];
  int dev_no;
  volatile unsigned long timer_ticks;
  int timer_running;
  int irq_ok;
  mixer_ents *mix_devices;
  int mixer_output_port;
}
cs4231_devc_t;

typedef struct cs4231_port_info
{
  int open_mode;
}
cs4231_port_info;

static int ad_format_mask[9 /*devc->model */ ] =
{
  0,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE |
    AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE |
    AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW,	/* AD1845 */
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE |
    AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE | AFMT_S16_BE | AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_S16_BE |
    AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE		/* CMI8330 */
};

#define CS_INB(osdev, addr)		(INL(osdev, addr) & 0xff)
#define CS_OUTB(osdev, data, addr)	OUTL(osdev, data & 0xff, addr)

/*
 * CS4231 codec I/O registers
 */
#define io_Index_Addr(d)	((d)->base)
#define io_Indexed_Data(d)	((d)->base+4)
#define io_Status(d)		((d)->base+8)
#define io_Polled_IO(d)		((d)->base+12)
#define CS4231_IO_RETRIES	10

/*
 * EB2 audio registers
 */
#define io_EB2_AUXIO(d)		((d)->auxio_base)
#define		EB2_AUXIO_COD_PDWN	0x00000001u	/* power down Codec */

static int cs4231_open (int dev, int mode, int open_flags);
static void cs4231_close (int dev, int mode);
static int cs4231_ioctl (int dev, unsigned int cmd, ioctl_arg arg);
static void cs4231_output_block (int dev, oss_native_word buf, int count,
				 int fragsize, int intrflag);
static void cs4231_start_input (int dev, oss_native_word buf, int count,
				int fragsize, int intrflag);
static int cs4231_prepare_for_output (int dev, int bsize, int bcount);
static int cs4231_prepare_for_input (int dev, int bsize, int bcount);
static void cs4231_halt (int dev);
static void cs4231_halt_input (int dev);
static void cs4231_halt_output (int dev);
static void cs4231_trigger (int dev, int bits);

static void
eb2_power(cs4231_devc_t * devc, int level)
{
	unsigned int tmp;
	oss_native_word flags;

  	MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
	tmp = INL(devc->osdev, io_EB2_AUXIO(devc));
cmn_err(CE_CONT, "AUXIO=%08x\n", tmp);

	tmp &= ~EB2_AUXIO_COD_PDWN;

	if (!level)
	   tmp |= EB2_AUXIO_COD_PDWN;
	OUTL(devc->osdev, tmp, io_EB2_AUXIO(devc));

	oss_udelay(10000);
cmn_err(CE_CONT, "AUXIO set to %08x, %08x (level=%d)\n", tmp, INL(devc->osdev, io_EB2_AUXIO(devc)), level);

  	MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
}

static int
ad_read (cs4231_devc_t * devc, int reg)
{
  oss_native_word flags;
  int x;

  reg = reg & 0xff;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  for (x=0;x<CS4231_IO_RETRIES;x++)
  {
     CS_OUTB (devc->osdev, (unsigned char) reg | devc->MCE_bit,
	io_Index_Addr (devc));
     oss_udelay(1000);

     if (CS_INB (devc->osdev, io_Index_Addr (devc)) == (reg | devc->MCE_bit))
	break;
  }

  if (x==CS4231_IO_RETRIES)
  {
     cmn_err(CE_NOTE, "Indirect register selection failed (read %d)\n", reg);
     cmn_err(CE_CONT, "Reg=%02x (%02x)\n", CS_INB (devc->osdev, io_Index_Addr (devc)), reg | devc->MCE_bit);
  }

  x = CS_INB (devc->osdev, io_Indexed_Data (devc));
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);

  return x;
}

static void
ad_write (cs4231_devc_t * devc, int reg, int data)
{
  oss_native_word flags;
  int x;

  reg &= 0xff;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  for (x=0;x<CS4231_IO_RETRIES;x++)
  {
     CS_OUTB (devc->osdev, (unsigned char) reg | devc->MCE_bit,
	io_Index_Addr (devc));

     oss_udelay(1000);

     if (CS_INB (devc->osdev, io_Index_Addr (devc)) == (reg | devc->MCE_bit))
	break;
  }

  if (x==CS4231_IO_RETRIES)
  {
     cmn_err(CE_NOTE, "Indirect register selection failed (write %d)\n", reg);
     cmn_err(CE_CONT, "Reg=%02x (%02x)\n", CS_INB (devc->osdev, io_Index_Addr (devc)), reg | devc->MCE_bit);
  }

  CS_OUTB (devc->osdev, (unsigned char) (data & 0xff), io_Indexed_Data (devc));
  oss_udelay(1000);

  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
}

static void
ad_mute (cs4231_devc_t * devc)
{
  int i;
  unsigned char prev;

  /*
   * Save old register settings and mute output channels
   */
  for (i = 6; i < 8; i++)
    {
      prev = devc->saved_regs[i] = ad_read (devc, i);

      devc->is_muted = 1;
      ad_write (devc, i, prev | 0x80);
    }
}

static void
ad_unmute (cs4231_devc_t * devc)
{
  int i, dummy;

  /*
   * Restore back old volume registers (unmute)
   */
  for (i = 6; i < 8; i++)
    {
      ad_write (devc, i, devc->saved_regs[i] & ~0x80);
    }
  devc->is_muted = 0;
}

static void
ad_enter_MCE (cs4231_devc_t * devc)
{
  oss_native_word flags;
  unsigned short prev;

  int timeout = 1000;
  while (timeout > 0 && CS_INB (devc->osdev, devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  devc->MCE_bit = 0x40;
  prev = CS_INB (devc->osdev, io_Index_Addr (devc));
  if (prev & 0x40)
    {
      MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
      return;
    }

  CS_OUTB (devc->osdev, devc->MCE_bit, io_Index_Addr (devc));
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
}

static void
ad_leave_MCE (cs4231_devc_t * devc)
{
  oss_native_word flags;
  unsigned char prev, acal;
  int timeout = 1000;

  while (timeout > 0 && CS_INB (devc->osdev, devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  acal = ad_read (devc, 9);

  devc->MCE_bit = 0x00;
  prev = CS_INB (devc->osdev, io_Index_Addr (devc));
  CS_OUTB (devc->osdev, 0x00, io_Index_Addr (devc));	/* Clear the MCE bit */

  if ((prev & 0x40) == 0)	/* Not in MCE mode */
    {
      MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
      return;
    }

  CS_OUTB (devc->osdev, 0x00, io_Index_Addr (devc));	/* Clear the MCE bit */
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
}

static int
cmi8330_set_recmask (cs4231_devc_t * devc, int mask)
{
  unsigned char bits = 0;

  mask &= SOUND_MASK_MIC | SOUND_MASK_LINE | SOUND_MASK_CD;

  if (mask & SOUND_MASK_MIC)
    bits |= 0x01;

  if (mask & SOUND_MASK_LINE)
    bits |= 0x06;

  if (mask & SOUND_MASK_LINE1)
    bits |= 0x18;

  ad_write (devc, 0x10, bits);

  return devc->recmask = mask;
}

static int
cs4231_set_recmask (cs4231_devc_t * devc, int mask)
{
  unsigned char recdev;
  int i, n;

  mask &= devc->supported_rec_devices;

  /* Rename the mixer bits if necessary */
  for (i = 0; i < 32; i++)
    if (devc->mixer_reroute[i] != i)
      if (mask & (1 << i))
	{
	  mask &= ~(1 << i);
	  mask |= (1 << devc->mixer_reroute[i]);
	}

  n = 0;
  for (i = 0; i < 32; i++)	/* Count selected device bits */
    if (mask & (1 << i))
      n++;

  if (n == 0)
    mask = SOUND_MASK_MIC;
  else if (n != 1)		/* Too many devices selected */
    {
      mask &= ~devc->recmask;	/* Filter out active settings */

      n = 0;
      for (i = 0; i < 32; i++)	/* Count selected device bits */
	if (mask & (1 << i))
	  n++;

      if (n != 1)
	mask = SOUND_MASK_MIC;
    }

  switch (mask)
    {
    case SOUND_MASK_MIC:
      recdev = 2;
      break;

    case SOUND_MASK_LINE:
    case SOUND_MASK_LINE3:
      recdev = 0;
      break;

    case SOUND_MASK_CD:
    case SOUND_MASK_LINE1:
      recdev = 1;
      break;

    case SOUND_MASK_IMIX:
      recdev = 3;
      break;

    default:
      mask = SOUND_MASK_MIC;
      recdev = 2;
    }

  recdev <<= 6;
  ad_write (devc, 0, (ad_read (devc, 0) & 0x3f) | recdev);
  ad_write (devc, 1, (ad_read (devc, 1) & 0x3f) | recdev);

  /* Rename the mixer bits back if necessary */
  for (i = 0; i < 32; i++)
    if (devc->mixer_reroute[i] != i)
      if (mask & (1 << devc->mixer_reroute[i]))
	{
	  mask &= ~(1 << devc->mixer_reroute[i]);
	  mask |= (1 << i);
	}

  devc->recmask = mask;
  return mask;
}

static void
change_bits (cs4231_devc_t * devc, unsigned char *regval, int dev, int chn,
	     int newval, int regoffs)
{
  unsigned char mask;
  int shift;
  int mute;
  int mutemask;
  int set_mute_bit;

  set_mute_bit = (newval == 0) || (devc->is_muted && dev == SOUND_MIXER_PCM);

  if (devc->mix_devices[dev][chn].polarity == 1)	/* Reverse */
    {
      newval = 100 - newval;
    }

  mask = (1 << devc->mix_devices[dev][chn].nbits) - 1;
  shift = devc->mix_devices[dev][chn].bitpos;

#if 0
  newval = (int) ((newval * mask) + 50) / 100;	/* Scale it */
  *regval &= ~(mask << shift);	/* Clear bits */
  *regval |= (newval & mask) << shift;	/* Set new value */
#else
  if (devc->mix_devices[dev][RIGHT_CHN].mutepos == 8)
    {				/* if there is no mute bit */
      mute = 0;			/* No mute bit; do nothing special */
      mutemask = ~0;		/* No mute bit; do nothing special */
    }
  else
    {
      mute = (set_mute_bit << devc->mix_devices[dev][RIGHT_CHN].mutepos);
      mutemask = ~(1 << devc->mix_devices[dev][RIGHT_CHN].mutepos);
    }

  newval = (int) ((newval * mask) + 50) / 100;	/* Scale it */
  *regval &= (~(mask << shift)) & (mutemask);	/* Clear bits */
  *regval |= ((newval & mask) << shift) | mute;	/* Set new value */
#endif
}

static int
cs4231_mixer_get (cs4231_devc_t * devc, int dev)
{
  if (!((1 << dev) & devc->supported_devices))
    return -EINVAL;

  dev = devc->mixer_reroute[dev];

  return devc->levels[dev];
}

static int
cs4231_mixer_set (cs4231_devc_t * devc, int dev, int value)
{
  int left = value & 0x000000ff;
  int right = (value & 0x0000ff00) >> 8;
  int retvol;

  int regoffs, regoffs1;
  unsigned char val;

  if (dev > 31)
    return -EINVAL;

  if (!(devc->supported_devices & (1 << dev)))
    return -EINVAL;

  dev = devc->mixer_reroute[dev];

  if (left > 100)
    left = 100;
  if (right > 100)
    right = 100;

  if (devc->mix_devices[dev][RIGHT_CHN].nbits == 0)	/* Mono control */
    right = left;

  retvol = left | (right << 8);

#if 1
  /* Scale volumes */
  left = mix_cvt[left];
  right = mix_cvt[right];

  /* Scale it again */
  left = mix_cvt[left];
  right = mix_cvt[right];
#endif

  if (devc->mix_devices[dev][LEFT_CHN].nbits == 0)
    return -EINVAL;

  devc->levels[dev] = retvol;

  /*
   * Set the left channel
   */

  regoffs1 = regoffs = devc->mix_devices[dev][LEFT_CHN].regno;
  val = ad_read (devc, regoffs);
  change_bits (devc, &val, dev, LEFT_CHN, left, regoffs);
  devc->saved_regs[regoffs] = val;

  /*
   * Set the right channel
   */

  if (devc->mix_devices[dev][RIGHT_CHN].nbits == 0)
    {
      ad_write (devc, regoffs, val);
      return retvol;		/* Was just a mono channel */
    }

  regoffs = devc->mix_devices[dev][RIGHT_CHN].regno;
  if (regoffs != regoffs1)
    {
      ad_write (devc, regoffs1, val);
      val = ad_read (devc, regoffs);
    }

  change_bits (devc, &val, dev, RIGHT_CHN, right, regoffs);
  ad_write (devc, regoffs, val);
  devc->saved_regs[regoffs] = val;

  return retvol;
}

static void
cs4231_mixer_reset (cs4231_devc_t * devc)
{
  int i;

  devc->mix_devices = &(cs4231_mix_devices[0]);
  devc->supported_rec_devices = MODE1_REC_DEVICES;

  for (i = 0; i < 32; i++)
    devc->mixer_reroute[i] = i;

  switch (devc->model)
    {
    case MD_4231:
    case MD_4231A:
      devc->supported_devices = MODE2_MIXER_DEVICES;
      break;

    default:
      devc->supported_devices = MODE1_MIXER_DEVICES;
    }

  devc->orig_devices = devc->supported_devices;
  devc->orig_rec_devices = devc->supported_rec_devices;

  devc->levels = default_mixer_levels;

  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
    if (devc->supported_devices & (1 << i))
      cs4231_mixer_set (devc, i, devc->levels[i]);
  cs4231_set_recmask (devc, SOUND_MASK_MIC);
}

static int
cs4231_mixer_ioctl (int dev, int audiodev, unsigned int cmd, ioctl_arg arg)
{
  cs4231_devc_t *devc = mixer_devs[dev]->devc;

  if (cmd == SOUND_MIXER_PRIVATE1)
    {
      int val;

      val = *arg;

      if (val == 0xffff)
	return *arg = devc->mixer_output_port;

      val &= (AUDIO_SPEAKER | AUDIO_HEADPHONE | AUDIO_LINE_OUT);

      devc->mixer_output_port = val;

      if (val & AUDIO_SPEAKER)
	ad_write (devc, 26, ad_read (devc, 26) & ~0x40);	/* Unmute mono out */
      else
	ad_write (devc, 26, ad_read (devc, 26) | 0x40);	/* Mute mono out */

      return *arg = devc->mixer_output_port;
    }

  if (((cmd >> 8) & 0xff) == 'M')
    {
      int val;

      if (IOC_IS_OUTPUT (cmd))
	switch (cmd & 0xff)
	  {
	  case SOUND_MIXER_RECSRC:
	    val = *arg;
	    return *arg = cs4231_set_recmask (devc, val);
	    break;

	  default:
	    val = *arg;
	    return *arg = cs4231_mixer_set (devc, cmd & 0xff, val);
	  }
      else
	switch (cmd & 0xff)	/*
				 * Return parameters
				 */
	  {

	  case SOUND_MIXER_RECSRC:
	    return *arg = devc->recmask;
	    break;

	  case SOUND_MIXER_DEVMASK:
	    return *arg = devc->supported_devices;
	    break;

	  case SOUND_MIXER_STEREODEVS:
	    return *arg = devc->supported_devices &
	      ~(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
	    break;

	  case SOUND_MIXER_RECMASK:
	    return *arg = devc->supported_rec_devices;
	    break;

	  case SOUND_MIXER_CAPS:
	    return *arg = SOUND_CAP_EXCL_INPUT;
	    break;

	  default:
	    return *arg = cs4231_mixer_get (devc, cmd & 0xff);
	  }
    }
  else
    return -EINVAL;
}

static int
cs4231_set_rate (int dev, int arg)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  /*
   * The sampling speed is encoded in the least significant nibble of I8. The
   * LSB selects the clock source (0=24.576 MHz, 1=16.9344 MHz) and other
   * three bits select the divisor (indirectly):
   *
   * The available speeds are in the following table. Keep the speeds in
   * the increasing order.
   */
  typedef struct
  {
    int speed;
    unsigned char bits;
  }
  speed_struct;

  static speed_struct speed_table[] = {
    {5510, (0 << 1) | 1},
    {5510, (0 << 1) | 1},
    {6620, (7 << 1) | 1},
    {8000, (0 << 1) | 0},
    {9600, (7 << 1) | 0},
    {11025, (1 << 1) | 1},
    {16000, (1 << 1) | 0},
    {18900, (2 << 1) | 1},
    {22050, (3 << 1) | 1},
    {27420, (2 << 1) | 0},
    {32000, (3 << 1) | 0},
    {33075, (6 << 1) | 1},
    {37800, (4 << 1) | 1},
    {44100, (5 << 1) | 1},
    {48000, (6 << 1) | 0}
  };

  int i, n, selected = -1;

  n = sizeof (speed_table) / sizeof (speed_struct);

  if (arg <= 0)
    return devc->speed;

#if 1
  if (ad_read (devc, 9) & 0x03)
    return devc->speed;
#endif

  if (arg < speed_table[0].speed)
    selected = 0;
  if (arg > speed_table[n - 1].speed)
    selected = n - 1;

  for (i = 1 /*really */ ; selected == -1 && i < n; i++)
    if (speed_table[i].speed == arg)
      selected = i;
    else if (speed_table[i].speed > arg)
      {
	int diff1, diff2;

	diff1 = arg - speed_table[i - 1].speed;
	diff2 = speed_table[i].speed - arg;

	if (diff1 < diff2)
	  selected = i - 1;
	else
	  selected = i;
      }

  if (selected == -1)
    {
      cmn_err (CE_WARN, "Can't find supported sample rate?\n");
      selected = 3;
    }

  devc->speed = speed_table[selected].speed;
  devc->speed_bits = speed_table[selected].bits;
  return devc->speed;
}

static short
cs4231_set_channels (int dev, short arg)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  if (arg != 1 && arg != 2)
    {
      return devc->channels;
    }

#if 1
  if (ad_read (devc, 9) & 0x03)
    {
      return devc->channels;
    }
#endif

  devc->channels = arg;
  return arg;
}

static unsigned int
cs4231_set_bits (int dev, unsigned int arg)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  static struct format_tbl
  {
    int format;
    unsigned char bits;
  }
  format2bits[] =
  {
    {
    0, 0}
    ,
    {
    AFMT_MU_LAW, 1}
    ,
    {
    AFMT_A_LAW, 3}
    ,
    {
    AFMT_IMA_ADPCM, 5}
    ,
    {
    AFMT_U8, 0}
    ,
    {
    AFMT_S16_LE, 2}
    ,
    {
    AFMT_S16_BE, 6}
    ,
    {
    AFMT_S8, 0}
    ,
    {
    AFMT_U16_LE, 0}
    ,
    {
    AFMT_U16_BE, 0}
  };
  int i, n = sizeof (format2bits) / sizeof (struct format_tbl);

  if (arg == 0)
    return devc->audio_format;

#if 1
  if (ad_read (devc, 9) & 0x03)
    return devc->audio_format;
#endif

  if (!(arg & ad_format_mask[devc->model]))
    arg = AFMT_U8;

  devc->audio_format = arg;

  for (i = 0; i < n; i++)
    if (format2bits[i].format == arg)
      {
	if ((devc->format_bits = format2bits[i].bits) == 0)
	  return devc->audio_format = AFMT_U8;	/* Was not supported */

	return arg;
      }

  /* Still hanging here. Something must be terribly wrong */
  devc->format_bits = 0;
  return devc->audio_format = AFMT_U8;
}

static const audiodrv_t cs4231_audio_driver = {
  cs4231_open,
  cs4231_close,
  cs4231_output_block,
  cs4231_start_input,
  cs4231_ioctl,
  cs4231_prepare_for_input,
  cs4231_prepare_for_output,
  cs4231_halt,
  NULL,
  NULL,
  cs4231_halt_input,
  cs4231_halt_output,
  cs4231_trigger,
  cs4231_set_rate,
  cs4231_set_bits,
  cs4231_set_channels
};

static mixer_driver_t cs4231_mixer_driver = {
  cs4231_mixer_ioctl
};

static int
cs4231_open (int dev, int mode, int open_flags)
{
  cs4231_devc_t *devc = NULL;
  cs4231_port_info *portc;
  oss_native_word flags;

  if (dev < 0 || dev >= num_audio_engines)
    return -ENXIO;

  devc = (cs4231_devc_t *) audio_engines[dev]->devc;
  portc = (cs4231_port_info *) audio_engines[dev]->portc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (portc->open_mode || (devc->open_mode & mode))
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  if (devc->open_mode == 0)
    {
      cs4231_trigger (dev, 0);
    }

  devc->open_mode |= mode;
  portc->open_mode = mode;
  devc->audio_mode &= ~mode;

  if (mode & OPEN_READ)
    devc->record_dev = dev;
  if (mode & OPEN_WRITE)
    devc->playback_dev = dev;
/*
 * Mute output until the playback really starts. This decreases clicking (hope so).
 */
  ad_mute (devc);
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

static void
cs4231_close (int dev, int mode)
{
  oss_native_word flags;
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;

  DDB (cmn_err (CE_CONT, "cs4231_close(void)\n"));

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  cs4231_halt (dev);

  devc->open_mode &= ~portc->open_mode;
  devc->audio_mode &= ~portc->open_mode;
  portc->open_mode = 0;

  ad_mute (devc);
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static int
cs4231_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  return -EINVAL;
}

static void
cs4231_output_block (int dev, oss_native_word buf, int count, int fragsize,
		     int intrflag)
{
  oss_native_word flags;
  unsigned int cnt;
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  cnt = fragsize;
  /* cnt = count; */

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (devc->audio_format == AFMT_IMA_ADPCM)
    {
      cnt /= 4;
    }
  else
    {
      if (devc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
	cnt >>= 1;
    }
  if (devc->channels > 1)
    cnt >>= 1;
  cnt--;

  if (devc->audio_mode & PCM_ENABLE_OUTPUT
      && audio_engines[dev]->flags & ADEV_AUTOMODE && intrflag
      && cnt == devc->xfer_count)
    {
      devc->audio_mode |= PCM_ENABLE_OUTPUT;
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return;
    }

  ad_write (devc, 15, (unsigned char) (cnt & 0xff));
  ad_write (devc, 14, (unsigned char) ((cnt >> 8) & 0xff));

  devc->xfer_count = cnt;
  devc->audio_mode |= PCM_ENABLE_OUTPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
cs4231_start_input (int dev, oss_native_word buf, int count, int fragsize,
		    int intrflag)
{
  oss_native_word flags;
  unsigned int cnt;
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  cnt = fragsize;
  /* cnt = count; */

  if (devc->audio_format == AFMT_IMA_ADPCM)
    {
      cnt /= 4;
    }
  else
    {
      if (devc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
	cnt >>= 1;
    }
  if (devc->channels > 1)
    cnt >>= 1;
  cnt--;

  if (devc->audio_mode & PCM_ENABLE_INPUT
      && audio_engines[dev]->flags & ADEV_AUTOMODE && intrflag
      && cnt == devc->xfer_count)
    {
      devc->audio_mode |= PCM_ENABLE_INPUT;
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return;			/*
				 * Auto DMA mode on. No need to react
				 */
    }

  ad_write (devc, 31, (unsigned char) (cnt & 0xff));
  ad_write (devc, 30, (unsigned char) ((cnt >> 8) & 0xff));

  ad_unmute (devc);

  devc->xfer_count = cnt;
  devc->audio_mode |= PCM_ENABLE_INPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
set_output_format (int dev)
{
  int timeout;
  unsigned char fs, old_fs, tmp = 0;
  oss_native_word flags;
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  if (ad_read (devc, 9) & 0x03)
    return;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  fs = devc->speed_bits | (devc->format_bits << 5);

  if (devc->channels > 1)
    fs |= 0x10;

  ad_enter_MCE (devc);		/* Enables changes to the format select reg */

  old_fs = ad_read (devc, 8);

  ad_write (devc, 8, fs);
  /*
   * Write to I8 starts resynchronization. Wait until it completes.
   */
  timeout = 0;
  while (timeout < 100 && CS_INB (devc->osdev, devc->base) != 0x80)
    timeout++;
  timeout = 0;
  while (timeout < 10000 && CS_INB (devc->osdev, devc->base) == 0x80)
    timeout++;

  ad_leave_MCE (devc);

  devc->xfer_count = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
set_input_format (int dev)
{
  int timeout;
  unsigned char fs, old_fs, tmp = 0;
  oss_native_word flags;
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  fs = devc->speed_bits | (devc->format_bits << 5);

  if (devc->channels > 1)
    fs |= 0x10;

  ad_enter_MCE (devc);		/* Enables changes to the format select reg */

  /*
   * If mode >= 2 (CS4231), set I28. It's the capture format register.
   */
  if (devc->model != MD_1848)
    {
      old_fs = ad_read (devc, 28);
      ad_write (devc, 28, fs);

      /*
       * Write to I28 starts resynchronization. Wait until it completes.
       */
      timeout = 0;
      while (timeout < 100 && CS_INB (devc->osdev, devc->base) != 0x80)
	timeout++;

      timeout = 0;
      while (timeout < 10000 && CS_INB (devc->osdev, devc->base) == 0x80)
	timeout++;

      if (devc->model != MD_1848)
	{
	  /*
	   * CS4231 compatible devices don't have separate sampling rate selection
	   * register for recording an playback. The I8 register is shared so we have to
	   * set the speed encoding bits of it too.
	   */
	  unsigned char tmp = devc->speed_bits | (ad_read (devc, 8) & 0xf0);
	  ad_write (devc, 8, tmp);
	  /*
	   * Write to I8 starts resynchronization. Wait until it completes.
	   */
	  timeout = 0;
	  while (timeout < 100 && CS_INB (devc->osdev, devc->base) != 0x80)
	    timeout++;

	  timeout = 0;
	  while (timeout < 10000 && CS_INB (devc->osdev, devc->base) == 0x80)
	    timeout++;
	}
    }
  else
    {				/* For CS4231 set I8. */

      old_fs = ad_read (devc, 8);
      ad_write (devc, 8, fs);
      /*
       * Write to I8 starts resynchronization. Wait until it completes.
       */
      timeout = 0;
      while (timeout < 100 && CS_INB (devc->osdev, devc->base) != 0x80)
	timeout++;
      timeout = 0;
      while (timeout < 10000 && CS_INB (devc->osdev, devc->base) == 0x80)
	timeout++;
    }

  ad_leave_MCE (devc);
  devc->xfer_count = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

}

static void
set_sample_format (int dev)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  if (ad_read (devc, 9) & 0x03)	/* Playback or recording active */
    return;

  set_input_format (dev);
  set_output_format (dev);
}

static int
cs4231_prepare_for_output (int dev, int bsize, int bcount)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  ad_mute (devc);

  cs4231_halt_output (dev);
  set_sample_format (dev);

  //TODO: Prepare the DMA engine
  return 0;
}

static int
cs4231_prepare_for_input (int dev, int bsize, int bcount)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;

  if (devc->audio_mode)
    return 0;

  cs4231_halt_input (dev);
  set_sample_format (dev);
  //TODO: Prepare the DMA engine
  return 0;
}

static void
cs4231_halt (int dev)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;

  unsigned char bits = ad_read (devc, 9);

  if (bits & 0x01 && portc->open_mode & OPEN_WRITE)
    cs4231_halt_output (dev);

  if (bits & 0x02 && portc->open_mode & OPEN_READ)
    cs4231_halt_input (dev);
}

static void
cs4231_halt_input (int dev)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;
  oss_native_word flags;

  if (!(portc->open_mode & OPEN_READ))
    return;
  if (!(ad_read (devc, 9) & 0x02))
    return;			/* Capture not enabled */

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  ad_write (devc, 9, ad_read (devc, 9) & ~0x02);	/* Stop capture */
  // TODO: Stop DMA

  CS_OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */
  CS_OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */

  devc->audio_mode &= ~PCM_ENABLE_INPUT;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
cs4231_halt_output (int dev)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;
  oss_native_word flags;

  if (!(portc->open_mode & OPEN_WRITE))
    return;
  if (!(ad_read (devc, 9) & 0x01))
    return;			/* Playback not enabled */

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  ad_mute (devc);
  oss_udelay (10);

  ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */
  //TODO: Disable DMA

  CS_OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */
  CS_OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */

  devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
cs4231_trigger (int dev, int state)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;
  oss_native_word flags;
  unsigned char tmp, old, oldstate;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  oldstate = state;
  state &= devc->audio_mode;

  tmp = old = ad_read (devc, 9);

  if (portc->open_mode & OPEN_READ)
    {
      if (state & PCM_ENABLE_INPUT)
	tmp |= 0x02;
      else
	tmp &= ~0x02;
    }

  if (portc->open_mode & OPEN_WRITE)
    {
      if (state & PCM_ENABLE_OUTPUT)
	tmp |= 0x01;
      else
	tmp &= ~0x01;
    }

  /* ad_mute(devc); */
  if (tmp != old)
    {
      ad_write (devc, 9, tmp);
      if (state & PCM_ENABLE_OUTPUT)
	{
	  oss_udelay (10);
	  oss_udelay (10);
	  oss_udelay (10);
	  ad_unmute (devc);
	}
    }

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
cs4231_init_hw (cs4231_devc_t * devc)
{
  int i;
  /*
   * Initial values for the indirect registers of CS4248/CS4231.
   */
  static int init_values[] = {
    0xa8, 0xa8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
    0x00, 0x0c, 0x02, 0x00, 0x8a, 0x01, 0x00, 0x00,

    /* Positions 16 to 31 just for CS4231/2 and ad1845 */
    0x80, 0x00, 0x10, 0x10, 0x00, 0x00, 0x1f, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  for (i = 0; i < 16; i++)
    ad_write (devc, i, init_values[i]);

/*
 * The XCTL0 (0x40) and XCTL1 (0x80) bits of I10 are used in Sparcs to
 * control codec's output pins which mute the line out and speaker out
 * connectors (respectively).
 *
 * Set them both to 0 (not muted). Better control is required in future.
 */

  ad_write (devc, 10, ad_read (devc, 10) & ~0xc0);

  ad_mute (devc);		/* Mute PCM until next use and initialize some variables */

  if (devc->model > MD_1848)
    {
      ad_write (devc, 12, ad_read (devc, 12) | 0x40);	/* Mode2 = enabled */

      for (i = 16; i < 32; i++)
	ad_write (devc, i, init_values[i]);

    }

  if (devc->model > MD_1848)
    {
      if (devc->audio_flags & ADEV_DUPLEX)
	ad_write (devc, 9, ad_read (devc, 9) & ~0x04);	/* Dual DMA mode */
      else
	ad_write (devc, 9, ad_read (devc, 9) | 0x04);	/* Single DMA mode */

    }
  else
    {
      devc->audio_flags &= ~ADEV_DUPLEX;
      ad_write (devc, 9, ad_read (devc, 9) | 0x04);	/* Single DMA mode */
    }

  CS_OUTB (devc->osdev, 0, io_Status (devc));	/* Clear pending interrupts */

  /*
   * Toggle the MCE bit. It completes the initialization phase.
   */

  ad_enter_MCE (devc);		/* In case the bit was off */
  ad_leave_MCE (devc);

/*
 * Perform full calibration
 */

  ad_enter_MCE (devc);
  ad_write (devc, 9, ad_read (devc, 9) | 0x18);	/* Enable autocalibration */
  ad_leave_MCE (devc);		/* This will trigger autocalibration */

  ad_enter_MCE (devc);
  ad_write (devc, 9, ad_read (devc, 9) & ~0x18);	/* Disable autocalibration */
  ad_leave_MCE (devc);
}

int
cs4231_detect (cs4231_devc_t * devc)
{

  unsigned char tmp;
  unsigned char tmp1 = 0xff, tmp2 = 0xff;

  int i;

cmn_err(CE_CONT, "a\n");
  devc->MCE_bit = 0x40;
  devc->chip_name = devc->name = "CS4231";
  devc->model = MD_4231;	/* CS4231 or CS4248 */
  devc->levels = NULL;

  /*
   * Check that the I/O address is in use.
   *
   * The bit 0x80 of the base I/O port is known to be 0 after the
   * chip has performed its power on initialization. Just assume
   * this has happened before the OS is starting.
   *
   * If the I/O address is unused, it typically returns 0xff.
   */

cmn_err(CE_CONT, "b (%x)\n", devc->base);
  if (CS_INB (devc->osdev, devc->base) == 0xff)
    {
      DDB (cmn_err
	   (CE_CONT,
	    "cs4231_detect: The base I/O address appears to be dead\n"));
    }
cmn_err(CE_CONT, "c\n");

#if 1
/*
 * Wait for the device to stop initialization
 */
  DDB (cmn_err (CE_CONT, "cs4231_detect() - step 0\n"));

  for (i = 0; i < 10000000; i++)
    {
      unsigned char x = CS_INB (devc->osdev, devc->base);
      if (x == 0xff || !(x & 0x80))
	break;
    }

#endif
cmn_err(CE_CONT, "d\n");

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step A\n"));

  if (CS_INB (devc->osdev, devc->base) == 0x80)	/* Not ready. Let's wait */
    ad_leave_MCE (devc);
cmn_err(CE_CONT, "e\n");

  if ((CS_INB (devc->osdev, devc->base) & 0x80) != 0x00)	/* Not a CS4231 */
    {
      DDB (cmn_err (CE_WARN, "cs4231 detect error - step A (%02x)\n",
		    (int) CS_INB (devc->osdev, devc->base)));
      return 0;
    }
cmn_err(CE_CONT, "f\n");

  /*
   * Test if it's possible to change contents of the indirect registers.
   * Registers 0 and 1 are ADC volume registers. The bit 0x10 is read only
   * so try to avoid using it.
   */

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step B\n"));
  ad_write (devc, 0, 0xaa);
  ad_write (devc, 1, 0x45);	/* 0x55 with bit 0x10 clear */
  oss_udelay (10);
cmn_err(CE_CONT, "g\n");

  if ((tmp1 = ad_read (devc, 0)) != 0xaa ||
      (tmp2 = ad_read (devc, 1)) != 0x45)
    {
      DDB (cmn_err
	   (CE_WARN, "cs4231 detect error - step B (%x/%x)\n", tmp1, tmp2));
#if 1
      if (tmp1 == 0x8a && tmp2 == 0xff)	/* AZT2320 ????? */
	{
	  DDB (cmn_err (CE_CONT, "Ignoring error\n"));
	}
      else
#endif
	return 0;
    }

cmn_err(CE_CONT, "h\n");
  DDB (cmn_err (CE_CONT, "cs4231_detect() - step C\n"));
  ad_write (devc, 0, 0x45);
  ad_write (devc, 1, 0xaa);
  oss_udelay (10);

cmn_err(CE_CONT, "i\n");
  if ((tmp1 = ad_read (devc, 0)) != 0x45
      || (tmp2 = ad_read (devc, 1)) != 0xaa)
    {
      DDB (cmn_err
	   (CE_WARN, "cs4231 detect error - step C (%x/%x)\n", tmp1, tmp2));
#if 1
      if (tmp1 == 0x65 && tmp2 == 0xff)	/* AZT2320 ????? */
	{
	  DDB (cmn_err (CE_CONT, "Ignoring error\n"));
	}
      else
#endif
	return 0;
    }

cmn_err(CE_CONT, "j\n");
  /*
   * The indirect register I12 has some read only bits. Lets
   * try to change them.
   */
  DDB (cmn_err (CE_CONT, "cs4231_detect() - step D\n"));
  tmp = ad_read (devc, 12);
  ad_write (devc, 12, (~tmp) & 0x0f);
cmn_err(CE_CONT, "k\n");

  if ((tmp & 0x0f) != ((tmp1 = ad_read (devc, 12)) & 0x0f))
    {
      DDB (cmn_err (CE_WARN, "cs4231 detect error - step D (%x)\n", tmp1));
      return 0;
    }

cmn_err(CE_CONT, "l\n");
  /*
   * NOTE! Last 4 bits of the reg I12 tell the chip revision.
   *   0x01=RevB and 0x0A=RevC.
   */

  /*
   * The original CS4231/CS4248 has just 15 indirect registers. This means
   * that I0 and I16 should return the same value (etc.).
   * However this doesn't work with CS4248. Actually it seems to be impossible
   * to detect if the chip is a CS4231 or CS4248.
   * Ensure that the Mode2 enable bit of I12 is 0. Otherwise this test fails
   * with CS4231.
   */

#if 1
/*
 * OPTi 82C930 has mode2 control bit in another place. This test will fail
 * with it. Accept this situation as a possible indication of this chip.
 */

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step F\n"));
  ad_write (devc, 12, 0);	/* Mode2=disabled */
cmn_err(CE_CONT, "m\n");

  for (i = 0; i < 16; i++)
    if ((tmp1 = ad_read (devc, i)) != (tmp2 = ad_read (devc, i + 16)))
      {
	DDB (cmn_err
	     (CE_CONT, "cs4231 detect step F(%d/%x/%x) - OPTi chip???\n", i,
	      tmp1, tmp2));
	break;
      }
#endif
cmn_err(CE_CONT, "n\n");

  /*
   * Try to switch the chip to mode2 (CS4231) by setting the MODE2 bit (0x40).
   * The bit 0x80 is always 1 in CS4248 and CS4231.
   */

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step G\n"));

cmn_err(CE_CONT, "o\n");
  ad_write (devc, 12, 0x40);	/* Set mode2, clear 0x80 */

  tmp1 = ad_read (devc, 12);
  if (tmp1 & 0x80)
    {
      devc->chip_name = "CS4248";	/* Our best knowledge just now */
    }

cmn_err(CE_CONT, "p\n");
  if ((tmp1 & 0xc0) == (0x80 | 0x40))
    {
      /*
       *      CS4231 detected - is it?
       *
       *      Verify that setting I0 doesn't change I16.
       */
      DDB (cmn_err (CE_CONT, "cs4231_detect() - step H\n"));
cmn_err(CE_CONT, "q\n");
      ad_write (devc, 16, 0);	/* Set I16 to known value */

      ad_write (devc, 0, 0x45);
cmn_err(CE_CONT, "r\n");
      if ((tmp1 = ad_read (devc, 16)) != 0x45)	/* No change -> CS4231? */
	{
cmn_err(CE_CONT, "s\n");

	  ad_write (devc, 0, 0xaa);
	  if ((tmp1 = ad_read (devc, 16)) == 0xaa)	/* Rotten bits? */
	    {
	      DDB (cmn_err
		   (CE_WARN, "cs4231 detect error - step H(%x)\n", tmp1));
	      return 0;
	    }

	  /*
	   * Verify that some bits of I25 are read only.
	   */

cmn_err(CE_CONT, "t\n");
	  DDB (cmn_err (CE_CONT, "cs4231_detect() - step I\n"));
	  tmp1 = ad_read (devc, 25);	/* Original bits */
	  ad_write (devc, 25, ~tmp1);	/* Invert all bits */
cmn_err(CE_CONT, "u\n");
	  if ((ad_read (devc, 25) & 0xe7) == (tmp1 & 0xe7))
	    {
	      int id, full_id;

	      /*
	       *      It's at least CS4231
	       */
	      devc->chip_name = "CS4231";

	      devc->model = MD_4231;

	      /*
	       * It could be an AD1845 or CS4231A as well.
	       * CS4231 and AD1845 report the same revision info in I25
	       * while the CS4231A reports different.
	       */

cmn_err(CE_CONT, "v\n");
	      id = ad_read (devc, 25) & 0xe7;
	      full_id = ad_read (devc, 25);
	      if (id == 0x80)	/* Device busy??? */
		id = ad_read (devc, 25) & 0xe7;
	      if (id == 0x80)	/* Device still busy??? */
		id = ad_read (devc, 25) & 0xe7;
	      DDB (cmn_err
		   (CE_CONT, "cs4231_detect() - step J (%02x/%02x)\n", id,
		    ad_read (devc, 25)));
cmn_err(CE_CONT, "w\n");

	      switch (id)
		{

		case 0xa0:
		  devc->chip_name = "CS4231A";
		  devc->model = MD_4231A;
		  break;

		default:	/* Assume CS4231 or OPTi 82C930 */
		  DDB (cmn_err (CE_CONT, "I25 = %02x/%02x\n",
				ad_read (devc, 25),
				ad_read (devc, 25) & 0xe7));
		  devc->model = MD_4231;

		}
	    }
cmn_err(CE_CONT, "x\n");
	  ad_write (devc, 25, tmp1);	/* Restore bits */

	  DDB (cmn_err (CE_CONT, "cs4231_detect() - step K\n"));
	}
    }

  DDB (cmn_err (CE_CONT, "cs4231_detect() - Detected OK\n"));
cmn_err(CE_CONT, "OK\n");

  return 1;
}

void
cs4231_init (cs4231_devc_t * devc, char *name)
{
  int my_dev, my_mixer;
  char dev_name[100];

  cs4231_port_info *portc = NULL;

  devc->open_mode = 0;
  devc->timer_ticks = 0;
  devc->audio_flags = ADEV_AUTOMODE;
  devc->playback_dev = devc->record_dev = 0;
  if (name != NULL)
    devc->name = name;

  if (name != NULL && name[0] != 0)
    sprintf (dev_name, "%s (%s)", name, devc->chip_name);
  else
    sprintf (dev_name, "Generic audio codec (%s)", devc->chip_name);

  if ((my_mixer = oss_install_mixer (OSS_MIXER_DRIVER_VERSION,
				     devc->osdev,
				     devc->osdev,
				     dev_name,
				     &cs4231_mixer_driver,
				     sizeof (mixer_driver_t), devc)) >= 0)
    {
      audio_engines[my_dev]->mixer_dev = my_mixer;
      cs4231_mixer_reset (devc);
    }

  if (devc->model > MD_1848)
    {
      devc->audio_flags |= ADEV_DUPLEX;
    }

  if ((my_dev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				      devc->osdev,
				      devc->osdev,
				      dev_name,
				      &cs4231_audio_driver,
				      sizeof (audiodrv_t),
				      devc->audio_flags,
				      ad_format_mask[devc->model],
				      devc, -1)) < 0)
    {
      return;
    }

  portc = PMALLOC (devc->osdev, sizeof (*portc));
  audio_engines[my_dev]->portc = portc;
  audio_engines[my_dev]->min_block = 512;
  memset ((char *) portc, 0, sizeof (*portc));

  cs4231_init_hw (devc);

#if 0
  test_it (devc);
#endif
}

void
cs4231_unload (cs4231_devc_t * devc)
{
#if 0
  int i, dev = 0;

  for (i = 0; devc == NULL && i < nr_cs4231_devs; i++)
    if (adev_info[i].base == io_base)
      {
	devc = &adev_info[i];
	dev = devc->dev_no;
      }

  if (devc != NULL)
    {

      if (!share_dma)
	{
	  if (irq > 0)
	    snd_release_irq (devc->irq, NULL);

	  FREE_DMA_CHN (audio_engines[dev]->dmap_out->dma);

	  if (audio_engines[dev]->dmap_in->dma !=
	      audio_engines[dev]->dmap_out->dma)
	    FREE_DMA_CHN (audio_engines[dev]->dmap_in->dma);
	}
    }
  else
    cmn_err (CE_WARN, "Can't find device to be unloaded. Base=%x\n", io_base);
#endif
}

int
cs4231intr (oss_device_t * osdev)
{
  unsigned char status;
  cs4231_devc_t *devc = osdev->devc;
  int alt_stat = 0xff;
  unsigned char c930_stat = 0;
  int cnt = 0;
  int serviced = 0;

  devc->irq_ok = 1;

interrupt_again:		/* Jump back here if int status doesn't reset */

  status = CS_INB (devc->osdev, io_Status (devc));

  if (status == 0x80)
    cmn_err (CE_CONT, "cs4231intr: Why?\n");
  else
    serviced = 1;
  if (devc->model == MD_1848)
    CS_OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */

  if (status & 0x01)
    {
      if (devc->model != MD_1848)
	{
	  alt_stat = ad_read (devc, 24);
	}

      /* Acknowledge the intr before proceeding */
      if (devc->model != MD_1848)
	ad_write (devc, 24, ad_read (devc, 24) & ~alt_stat);	/* Selective ack */

      if (devc->open_mode & OPEN_READ && devc->audio_mode & PCM_ENABLE_INPUT
	  && alt_stat & 0x20)
	{
	  oss_audio_inputintr (devc->record_dev, 0);
	}

      if (devc->open_mode & OPEN_WRITE && devc->audio_mode & PCM_ENABLE_OUTPUT
	  && alt_stat & 0x10)
	{
	  oss_audio_outputintr (devc->playback_dev, 1);
	}

      if (devc->model != MD_1848 && alt_stat & 0x40)	/* Timer interrupt */
	{
	  devc->timer_ticks++;
	}
    }

#if 1
/*
 * Sometimes playback or capture interrupts occur while a timer interrupt
 * is being handled. The interrupt will not be retriggered if we don't
 * handle it now. Check if an interrupt is still pending and restart
 * the handler in this case.
 */
  if (CS_INB (devc->osdev, io_Status (devc)) & 0x01 && cnt++ < 4)
    {
      goto interrupt_again;
    }
#endif
  return serviced;
}

int
oss_audiocs_attach (oss_device_t * osdev)
{
  unsigned int dw;

  cs4231_devc_t *devc = osdev->devc;

  DDB(cmn_err(CE_CONT, "Entered oss_audiocs_attach()\n"));

  if ((devc = PMALLOC (osdev, sizeof (*devc))) == NULL)
    {
      cmn_err (CE_WARN, "Out of memory\n");
      return 0;
    }

  devc->osdev = osdev;
  osdev->devc = devc;
  devc->open_mode = 0;

  devc->chip_name = "Generic CS4231";

/*
 * Map I/O registers.
 *
 * Note that MAP_PCI_IOADDR uses different region numbering than 
 *      ddi_regs_map_setup.
 *
 * TODO: Replace MAP_PCI_IOADDR() with something that is not PCI specific.
 */
  devc->base = MAP_PCI_IOADDR (devc->osdev, -1, 0);
  //devc->play_base = MAP_PCI_IOADDR (devc->osdev, 0, 0);
  //devc->record_base = MAP_PCI_IOADDR (devc->osdev, 1, 0);
  devc->auxio_base = MAP_PCI_IOADDR (devc->osdev, 2, 0);
cmn_err(CE_CONT, "Base address = %x, auxio=%x\n", devc->base, devc->auxio_base);

  MUTEX_INIT (devc->osdev, devc->mutex, MH_DRV);
  MUTEX_INIT (devc->osdev, devc->low_mutex, MH_DRV + 1);

  eb2_power(devc, 1);

  if (!cs4231_detect(devc))
     return 0;

  oss_register_device (osdev, devc->chip_name);

  return 1;
}

int
oss_audiocs_detach (oss_device_t * osdev)
{
  cs4231_devc_t *devc = (cs4231_devc_t *) osdev->devc;

  if (oss_disable_device (osdev) < 0)
    return 0;

  eb2_power(devc, 0);

  MUTEX_CLEANUP (devc->mutex);
  MUTEX_CLEANUP (devc->low_mutex);

  oss_unregister_device (osdev);

  return 1;
}
