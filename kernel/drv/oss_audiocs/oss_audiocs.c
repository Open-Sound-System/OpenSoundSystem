/*
 * Purpose: Driver for the UltraSparc workstations using CS4231 codec for audio
 *
 */

#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 1997-2008. All rights reserved.

#include "oss_audiocs_cfg.h"

#include "cs4231_mixer.h"

typedef struct
{
  oss_device_t *osdev;
  int base;
  int ctrl_base;		/* CS423x devices only */
  int irq;
  int dma1, dma2;
  int dual_dma;			/* 1, when two DMA channels allocated */
  unsigned char MCE_bit;
  unsigned char saved_regs[32];
  int debug_flag;

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
  int spdif_present;
  int recmask;
  int supported_devices, orig_devices;
  int supported_rec_devices, orig_rec_devices;
  int *levels;
  short mixer_reroute[32];
  int dev_no;
  volatile unsigned long timer_ticks;
  int timer_running;
  int irq_ok;
  int do_poll;
  mixer_ents *mix_devices;
  int mixer_output_port;
}
cs4231_devc;

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

#ifdef sparc
int cs4231_mixer_output_port = AUDIO_HEADPHONE | AUDIO_LINE_OUT;
#endif

#define io_Index_Addr(d)	((d)->base)
#define io_Indexed_Data(d)	((d)->base+4)
#define io_Status(d)		((d)->base+8)
#define io_Polled_IO(d)		((d)->base+12)

static int cs4231_open (int dev, int mode, int open_flags);
static void cs4231_close (int dev, int mode);
static int cs4231_ioctl (int dev, unsigned int cmd, ioctl_arg arg);
static void cs4231_output_block (int dev, unsigned long buf, int count,
				 int fragsize, int intrflag);
static void cs4231_start_input (int dev, unsigned long buf, int count,
				int fragsize, int intrflag);
static int cs4231_prepare_for_output (int dev, int bsize, int bcount);
static int cs4231_prepare_for_input (int dev, int bsize, int bcount);
static void cs4231_halt (int dev);
static void cs4231_halt_input (int dev);
static void cs4231_halt_output (int dev);
static void cs4231_trigger (int dev, int bits);
#if defined(CONFIG_SEQUENCER) && !defined(EXCLUDE_TIMERS)
static int cs4231_tmr_install (int dev);
static void cs4231_tmr_reprogram (int dev);
#endif

static int
ad_read (cs4231_devc * devc, int reg)
{
  unsigned long flags;
  int x;
  int timeout = 9000;

  while (timeout > 0 && INB (devc->osdev, devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  DISABLE_INTR (devc->osdev, flags);
  tenmicrosec (devc->osdev);
  OUTB (devc->osdev, (unsigned char) (reg & 0xff) | devc->MCE_bit,
	io_Index_Addr (devc));
  tenmicrosec (devc->osdev);
  x = INB (devc->osdev, io_Indexed_Data (devc));
  /* cmn_err(CE_CONT, "(%02x<-%02x)\n", reg|devc->MCE_bit, x); */
  RESTORE_INTR (flags);

  return x;
}

static void
ad_write (cs4231_devc * devc, int reg, int data)
{
  unsigned long flags;
  int timeout = 900000;

#if 0
  if (devc->debug_flag && reg != 24)
    DDB (cmn_err (CE_CONT, "[%2d,%02x]\n", reg, data));
#endif
  while (timeout > 0 && INB (devc->osdev, devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  DISABLE_INTR (devc->osdev, flags);
  OUTB (devc->osdev, (unsigned char) (reg & 0xff) | devc->MCE_bit,
	io_Index_Addr (devc));
  OUTB (devc->osdev, (unsigned char) (data & 0xff), io_Indexed_Data (devc));
  /* cmn_err(CE_CONT, "(%02d->%02x, MCE %x)\n", reg, data, devc->MCE_bit); */
  RESTORE_INTR (flags);
}

static unsigned char
ctrl_read (cs4231_devc * devc, int index)
{
  unsigned char d;
  unsigned long flags;
  int base;

  if (devc->ctrl_base)
    base = devc->ctrl_base;
  else
    base = devc->base + 4;

  DISABLE_INTR (devc->osdev, flags);

  OUTB (devc->osdev, index, base + 3);	/* CTRLbase+3 */
  d = INB (devc->osdev, base + 4);	/* CTRLbase+4 */
  RESTORE_INTR (flags);
  return d;
}

static void
ctrl_write (cs4231_devc * devc, int index, unsigned char d)
{
  unsigned long flags;
  int base;

  if (devc->ctrl_base)
    base = devc->ctrl_base;
  else
    base = devc->base + 4;
  DISABLE_INTR (devc->osdev, flags);

  OUTB (devc->osdev, index, base + 3);	/* CTRLbase+3 */
  OUTB (devc->osdev, d, base + 4);	/* CTRLbase+4 */
  RESTORE_INTR (flags);
}

static void
wait_for_calibration (cs4231_devc * devc)
{
  int timeout = 0;

  /*
   * Wait until the auto calibration process has finished.
   *
   * 1)       Wait until the chip becomes ready (reads don't return 0x80).
   * 2)       Wait until the ACI bit of I11 gets on and then off.
   */

  timeout = 100000;
  while (timeout > 0 && INB (devc->osdev, devc->base) == 0x80)
    timeout--;
  if (INB (devc->osdev, devc->base) & 0x80)
    cmn_err (CE_WARN, "Auto calibration timed out(1).\n");

  timeout = 100;
  while (timeout > 0 && !(ad_read (devc, 11) & 0x20))
    timeout--;
  if (!(ad_read (devc, 11) & 0x20))
    return;

  timeout = 80000;
  while (timeout > 0 && ad_read (devc, 11) & 0x20)
    timeout--;
  if (ad_read (devc, 11) & 0x20)
    cmn_err (CE_WARN, "Auto calibration timed out(3).\n");
}

static void
ad_mute (cs4231_devc * devc)
{
  int i;
  unsigned char prev;

  /*
   * Save old register settings and mute output channels
   */
  for (i = 6; i < 8; i++)
    {
      prev = devc->saved_regs[i] = ad_read (devc, i);

#if 1
      devc->is_muted = 1;
      ad_write (devc, i, prev | 0x80);
#endif
    }

#if 0
/*
 * Let's have some delay
 */

  for (i = 0; i < 1000; i++)
    INB (devc->osdev, devc->base);
#endif
}

static void
ad_unmute (cs4231_devc * devc)
{
#if 1
  int i, dummy;

/*
 * Let's have some delay
 */
  for (i = 0; i < 1000; i++)
    dummy = INB (devc->osdev, devc->base);

  /*
   * Restore back old volume registers (unmute)
   */
  for (i = 6; i < 8; i++)
    {
      ad_write (devc, i, devc->saved_regs[i] & ~0x80);
    }
  devc->is_muted = 0;
#endif
}

static void
ad_enter_MCE (cs4231_devc * devc)
{
  unsigned long flags;
  int timeout = 1000;
  unsigned short prev;

  while (timeout > 0 && INB (devc->osdev, devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  DISABLE_INTR (devc->osdev, flags);

  devc->MCE_bit = 0x40;
  prev = INB (devc->osdev, io_Index_Addr (devc));
  if (prev & 0x40)
    {
      RESTORE_INTR (flags);
      return;
    }

  OUTB (devc->osdev, devc->MCE_bit, io_Index_Addr (devc));
  RESTORE_INTR (flags);
}

static void
ad_leave_MCE (cs4231_devc * devc)
{
  unsigned long flags;
  unsigned char prev, acal;
  int timeout = 1000;

  while (timeout > 0 && INB (devc->osdev, devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  DISABLE_INTR (devc->osdev, flags);

  acal = ad_read (devc, 9);

  devc->MCE_bit = 0x00;
  prev = INB (devc->osdev, io_Index_Addr (devc));
  OUTB (devc->osdev, 0x00, io_Index_Addr (devc));	/* Clear the MCE bit */

  if ((prev & 0x40) == 0)	/* Not in MCE mode */
    {
      RESTORE_INTR (flags);
      return;
    }

  OUTB (devc->osdev, 0x00, io_Index_Addr (devc));	/* Clear the MCE bit */
  if (acal & 0x08)		/* Auto calibration is enabled */
    wait_for_calibration (devc);
  RESTORE_INTR (flags);
}

static int
cmi8330_set_recmask (cs4231_devc * devc, int mask)
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
cs4231_set_recmask (cs4231_devc * devc, int mask)
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
change_bits (cs4231_devc * devc, unsigned char *regval, int dev, int chn,
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
cs4231_mixer_get (cs4231_devc * devc, int dev)
{
  if (!((1 << dev) & devc->supported_devices))
    return RET_ERROR (EINVAL);

  dev = devc->mixer_reroute[dev];

  return devc->levels[dev];
}

static int
cs4231_mixer_set (cs4231_devc * devc, int dev, int value)
{
  int left = value & 0x000000ff;
  int right = (value & 0x0000ff00) >> 8;
  int retvol;

  int regoffs, regoffs1;
  unsigned char val;

  if (dev > 31)
    return RET_ERROR (EINVAL);

  if (!(devc->supported_devices & (1 << dev)))
    return RET_ERROR (EINVAL);

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
    return RET_ERROR (EINVAL);

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
cs4231_mixer_reset (cs4231_devc * devc)
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
#ifdef sparc
  if (devc->levels[31] == 0)
    devc->levels[31] = AUDIO_HEADPHONE | AUDIO_LINE_OUT | AUDIO_SPEAKER;	/* Enable speaker by default */
#endif

  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
    if (devc->supported_devices & (1 << i))
      cs4231_mixer_set (devc, i, devc->levels[i]);
  cs4231_set_recmask (devc, SOUND_MASK_MIC);
#ifdef sparc
# ifdef linux______
  cs4231_control (CS4231_SET_OUTPUTS, AUDIO_LINE_OUT);
# else
  cs4231_control (CS4231_SET_OUTPUTS, devc->levels[31]);
# endif
#else
  devc->mixer_output_port =
    devc->levels[31] | AUDIO_HEADPHONE | AUDIO_LINE_OUT;
  if (devc->mixer_output_port & AUDIO_SPEAKER)
    ad_write (devc, 26, ad_read (devc, 26) & ~0x40);	/* Unmute mono out */
  else
    ad_write (devc, 26, ad_read (devc, 26) | 0x40);	/* Mute mono out */
#endif
}

static int
spdif_control (cs4231_devc * devc, int val)
{
  if (devc->audio_mode)		/* Don't change if device in use */
    return devc->levels[SOUND_MIXER_DIGITAL1];

  if (val)
    val = (100 << 8) | 100;	/* Set to 100% */

  if (val)
    {
      /* Enable S/PDIF */
      ad_enter_MCE (devc);
      ad_write (devc, 16, ad_read (devc, 16) | 0x02);	/* Set SPE */
      ad_leave_MCE (devc);
      ctrl_write (devc, 4, ctrl_read (devc, 4) | 0x80);	/* Enable, U and V bits on */
      ctrl_write (devc, 5, 0x04);	/* Lower channel status */
      ctrl_write (devc, 6, 0x00);	/* Upper channel status */
      ctrl_write (devc, 8, ctrl_read (devc, 8) | 0x04);	/* Set SPS */
    }
  else
    {
      /* Disable S/PDIF */
      ctrl_write (devc, 4, ctrl_read (devc, 4) & ~0x80);	/* Disable */
      ctrl_write (devc, 5, 0x00);	/* Lower channel status */
      ctrl_write (devc, 6, 0x00);	/* Upper channel status */
      ctrl_write (devc, 8, ctrl_read (devc, 8) & ~0x04);	/* Clear SPS */
      ad_enter_MCE (devc);
      ad_write (devc, 16, ad_read (devc, 16) & ~0x02);	/* Clear SPE */
      ad_leave_MCE (devc);
    }

  return val;
}

static int
cs4231_mixer_ioctl (int dev, int audiodev, unsigned int cmd, ioctl_arg arg)
{
  cs4231_devc *devc = mixer_devs[dev]->devc;

  if (cmd == SOUND_MIXER_PRIVATE1)
    {
      int val;

      IOCTL_GET (arg, val);

      if (val == 0xffff)
	return IOCTL_OUT (arg, devc->mixer_output_port);

      val &= (AUDIO_SPEAKER | AUDIO_HEADPHONE | AUDIO_LINE_OUT);

      devc->mixer_output_port = val;
#ifdef sparc
      cs4231_mixer_output_port = val;
      devc->mixer_output_port = val;
#else
      val |= AUDIO_HEADPHONE | AUDIO_LINE_OUT;	/* Always on */
      devc->mixer_output_port = val;
#endif

      if (val & AUDIO_SPEAKER)
	ad_write (devc, 26, ad_read (devc, 26) & ~0x40);	/* Unmute mono out */
      else
	ad_write (devc, 26, ad_read (devc, 26) | 0x40);	/* Mute mono out */

      return IOCTL_OUT (arg, devc->mixer_output_port);
    }

  if (((cmd >> 8) & 0xff) == 'M')
    {
      int val;

      if (IOC_IS_OUTPUT (cmd))
	switch (cmd & 0xff)
	  {
	  case SOUND_MIXER_RECSRC:
	    IOCTL_GET (arg, val);
	    return IOCTL_OUT (arg, cs4231_set_recmask (devc, val));
	    break;

	  case SOUND_MIXER_DIGITAL1:	/* S/PDIF (if present) */
	    IOCTL_GET (arg, val);
	    if (devc->spdif_present)
	      val = spdif_control (devc, val);
	    return devc->levels[SOUND_MIXER_DIGITAL1] = val;
	    break;

	  default:
	    IOCTL_GET (arg, val);
	    return IOCTL_OUT (arg, cs4231_mixer_set (devc, cmd & 0xff, val));
	  }
      else
	switch (cmd & 0xff)	/*
				 * Return parameters
				 */
	  {

	  case SOUND_MIXER_RECSRC:
	    return IOCTL_OUT (arg, devc->recmask);
	    break;

	  case SOUND_MIXER_DEVMASK:
	    return IOCTL_OUT (arg, devc->supported_devices);
	    break;

	  case SOUND_MIXER_STEREODEVS:
	    return IOCTL_OUT (arg, devc->supported_devices &
			      ~(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX));
	    break;

	  case SOUND_MIXER_RECMASK:
	    return IOCTL_OUT (arg, devc->supported_rec_devices);
	    break;

	  case SOUND_MIXER_CAPS:
	    return IOCTL_OUT (arg, SOUND_CAP_EXCL_INPUT);
	    break;

	  default:
	    return IOCTL_OUT (arg, cs4231_mixer_get (devc, cmd & 0xff));
	  }
    }
  else
    return RET_ERROR (EINVAL);
}

static int
cs4231_set_speed (int dev, int arg)
{
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

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
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

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
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

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
  cs4231_set_speed,
  cs4231_set_bits,
  cs4231_set_channels
};

static const mixer_driver_t cs4231_mixer_driver = {
  cs4231_mixer_ioctl
};

static int
cs4231_open (int dev, int mode, int open_flags)
{
  cs4231_devc *devc = NULL;
  cs4231_port_info *portc;
  unsigned long flags;

  if (dev < 0 || dev >= num_audio_engines)
    return RET_ERROR (ENXIO);

  devc = (cs4231_devc *) audio_engines[dev]->devc;
  portc = (cs4231_port_info *) audio_engines[dev]->portc;

  DISABLE_INTR (devc->osdev, flags);
  if (portc->open_mode || (devc->open_mode & mode))
    {
      RESTORE_INTR (flags);
      return RET_ERROR (EBUSY);
    }

  devc->dual_dma = 0;

  if (audio_engines[dev]->flags & ADEV_DUPLEX)
    {
      devc->dual_dma = 1;
    }

  if (devc->open_mode == 0)
    {
      cs4231_trigger (dev, 0);
      devc->do_poll = 1;
    }

  devc->open_mode |= mode;
  portc->open_mode = mode;
  devc->audio_mode &= ~mode;

  if (mode & OPEN_READ)
    devc->record_dev = dev;
  if (mode & OPEN_WRITE)
    devc->playback_dev = dev;
  RESTORE_INTR (flags);
/*
 * Mute output until the playback really starts. This decreases clicking (hope so).
 */
  ad_mute (devc);

  return 0;
}

static void
cs4231_close (int dev, int mode)
{
  unsigned long flags;
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;

  DDB (cmn_err (CE_CONT, "cs4231_close(void)\n"));

  DISABLE_INTR (devc->osdev, flags);

  cs4231_halt (dev);

  devc->open_mode &= ~portc->open_mode;
  devc->audio_mode &= ~portc->open_mode;
  portc->open_mode = 0;

  ad_mute (devc);
  RESTORE_INTR (flags);
}

static int
cs4231_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  return RET_ERROR (EINVAL);
}

static void
cs4231_output_block (int dev, unsigned long buf, int count, int fragsize,
		     int intrflag)
{
  unsigned long flags, cnt;
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

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

  if (devc->audio_mode & PCM_ENABLE_OUTPUT
      && audio_engines[dev]->flags & ADEV_AUTOMODE && intrflag
      && cnt == devc->xfer_count)
    {
      devc->audio_mode |= PCM_ENABLE_OUTPUT;
      return;			/*
				 * Auto DMA mode on. No need to react
				 */
    }
  DISABLE_INTR (devc->osdev, flags);

  ad_write (devc, 15, (unsigned char) (cnt & 0xff));
  ad_write (devc, 14, (unsigned char) ((cnt >> 8) & 0xff));

  //DMABUF_start_dma (dev, buf, count, DMA_MODE_WRITE);

  devc->xfer_count = cnt;
  devc->audio_mode |= PCM_ENABLE_OUTPUT;
  RESTORE_INTR (flags);
}

static void
cs4231_start_input (int dev, unsigned long buf, int count, int fragsize,
		    int intrflag)
{
  unsigned long flags, cnt;
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

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
      return;			/*
				 * Auto DMA mode on. No need to react
				 */
    }
  DISABLE_INTR (devc->osdev, flags);

  ad_write (devc, 31, (unsigned char) (cnt & 0xff));
  ad_write (devc, 30, (unsigned char) ((cnt >> 8) & 0xff));

  ad_unmute (devc);
  //DMABUF_start_dma (dev, buf, count, DMA_MODE_READ);

  devc->xfer_count = cnt;
  devc->audio_mode |= PCM_ENABLE_INPUT;
  RESTORE_INTR (flags);
}

static void
set_output_format (int dev)
{
  int timeout;
  unsigned char fs, old_fs, tmp = 0;
  unsigned long flags;
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

  if (ad_read (devc, 9) & 0x03)
    return;

  DISABLE_INTR (devc->osdev, flags);
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
  while (timeout < 100 && INB (devc->osdev, devc->base) != 0x80)
    timeout++;
  timeout = 0;
  while (timeout < 10000 && INB (devc->osdev, devc->base) == 0x80)
    timeout++;

  ad_leave_MCE (devc);		/*
				 * Starts the calibration process.
				 */
  RESTORE_INTR (flags);
  devc->xfer_count = 0;
}

static void
set_input_format (int dev)
{
  int timeout;
  unsigned char fs, old_fs, tmp = 0;
  unsigned long flags;
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

  DISABLE_INTR (devc->osdev, flags);
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
      while (timeout < 100 && INB (devc->osdev, devc->base) != 0x80)
	timeout++;

      timeout = 0;
      while (timeout < 10000 && INB (devc->osdev, devc->base) == 0x80)
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
	  while (timeout < 100 && INB (devc->osdev, devc->base) != 0x80)
	    timeout++;

	  timeout = 0;
	  while (timeout < 10000 && INB (devc->osdev, devc->base) == 0x80)
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
      while (timeout < 100 && INB (devc->osdev, devc->base) != 0x80)
	timeout++;
      timeout = 0;
      while (timeout < 10000 && INB (devc->osdev, devc->base) == 0x80)
	timeout++;
    }

  ad_leave_MCE (devc);		/*
				 * Starts the calibration process.
				 */
  RESTORE_INTR (flags);
  devc->xfer_count = 0;

}

static void
set_sample_format (int dev)
{
#if 1
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

  if (ad_read (devc, 9) & 0x03)	/* Playback or recording active */
    return;
#endif

  set_input_format (dev);
  set_output_format (dev);
}

static int
cs4231_prepare_for_output (int dev, int bsize, int bcount)
{
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

  ad_mute (devc);
#ifdef sparc
#ifdef ENABLE_DMA
#ifdef DISABLE_DMA
  {
/*
 * This code fragment ensures that the playback FIFO is empty before
 * setting the codec for playback. Enabling playback for a moment should
 * be enough to do that.
 */
    int tmout;

    ad_write (devc, 9, ad_read (devc, 9) | 0x01);	/* Enable playback */
    DISABLE_DMA (dev, audio_engines[dev]->dmap_out, 0);
    for (tmout = 0; tmout < 1000000; tmout++)
      if (ad_read (devc, 11) & 0x10)	/* DRQ active */
	if (tmout > 10000)
	  break;
    ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */

    ENABLE_DMA (dev, audio_engines[dev]->dmap_out);
    devc->audio_mode &= ~PCM_ENABLE_OUTPUT;
  }
#endif
#endif

#ifdef RESET_DMA
  RESET_DMA (dev, audio_engines[dev]->dmap_out,
	     audio_engines[dev]->dmap_out->dma);
#endif
#endif

  cs4231_halt_output (dev);
  set_sample_format (dev);
  return 0;
}

static int
cs4231_prepare_for_input (int dev, int bsize, int bcount)
{
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;

  if (devc->audio_mode)
    return 0;

#ifdef RESET_DMA
  RESET_DMA (dev, audio_engines[dev]->dmap_in,
	     audio_engines[dev]->dmap_in->dma);
#endif
  cs4231_halt_input (dev);
  set_sample_format (dev);
  return 0;
}

static void
cs4231_halt (int dev)
{
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;
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
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;
  unsigned long flags;

  if (!(portc->open_mode & OPEN_READ))
    return;
  if (!(ad_read (devc, 9) & 0x02))
    return;			/* Capture not enabled */

  DISABLE_INTR (devc->osdev, flags);

#ifdef ENABLE_DMA
#ifdef DISABLE_DMA
  {
    int tmout;

    DISABLE_DMA (dev, audio_engines[dev]->dmap_in, 0);

    for (tmout = 0; tmout < 100000; tmout++)
      if (ad_read (devc, 11) & 0x10)
	break;
    ad_write (devc, 9, ad_read (devc, 9) & ~0x02);	/* Stop capture */

    ENABLE_DMA (dev, audio_engines[dev]->dmap_in);
    devc->audio_mode &= ~PCM_ENABLE_INPUT;
  }
#endif
#else
  ad_write (devc, 9, ad_read (devc, 9) & ~0x02);	/* Stop capture */
#endif

  OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */
  OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */

  devc->audio_mode &= ~PCM_ENABLE_INPUT;

  RESTORE_INTR (flags);
}

static void
cs4231_halt_output (int dev)
{
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;
  unsigned long flags;

  if (!(portc->open_mode & OPEN_WRITE))
    return;
  if (!(ad_read (devc, 9) & 0x01))
    return;			/* Playback not enabled */

  DISABLE_INTR (devc->osdev, flags);

  ad_mute (devc);
  tenmicrosec (devc->osdev);
#ifdef ENABLE_DMA
#ifdef DISABLE_DMA
  {
    int tmout;

    DISABLE_DMA (dev, audio_engines[dev]->dmap_out, 0);

    for (tmout = 0; tmout < 100000; tmout++)
      if (ad_read (devc, 11) & 0x10)
	break;
    ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */

    ENABLE_DMA (dev, audio_engines[dev]->dmap_out);
    devc->audio_mode &= ~PCM_ENABLE_OUTPUT;
  }
#endif
#else
  ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */
#endif

  OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */
  OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */

  devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

  RESTORE_INTR (flags);
}

static void
cs4231_trigger (int dev, int state)
{
  cs4231_devc *devc = (cs4231_devc *) audio_engines[dev]->devc;
  cs4231_port_info *portc = (cs4231_port_info *) audio_engines[dev]->portc;
  unsigned long flags;
  unsigned char tmp, old, oldstate;

  DISABLE_INTR (devc->osdev, flags);
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
	  tenmicrosec (devc->osdev);
	  tenmicrosec (devc->osdev);
	  tenmicrosec (devc->osdev);
	  ad_unmute (devc);
	}
    }

  RESTORE_INTR (flags);
}

static void
cs4231_init_hw (cs4231_devc * devc)
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

#ifdef sparc
  if (devc == NULL)
    devc = cs4231_devc;

  if (devc == NULL)
    return;
#endif

  devc->debug_flag = 1;

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

/*
 * Assume the MOM (0x40) bit of I26 is used to mute the internal speaker
 * in sparcs. Mute it now. Better control is required in future.
 */
      ad_write (devc, 26, ad_read (devc, 26) | 0x40);
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

  OUTB (devc->osdev, 0, io_Status (devc));	/* Clear pending interrupts */

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
cs4231_detect (int io_base, long *ad_flags, oss_device_t * osdev)
{

  unsigned char tmp;
  //cs4231_devc *devc = NULL; // TODO: Allocate this
  unsigned char tmp1 = 0xff, tmp2 = 0xff;
  int optiC930 = 0;		/* OPTi 82C930 flag */
  int interwave = 0;
  int ad1847_flag = 0;
  int cs4248_flag = 0;
  int cmi8330_flag = 0;
  extern int cs423x_ctl_port;

  int i;

  DDB (cmn_err (CE_CONT, "cs4231_detect(%x)\n", io_base));

  if (ad_flags)
    {
      if (*ad_flags == 0x12345678)
	{
	  interwave = 1;
	  *ad_flags = 0;
	}

      if (*ad_flags == 0x12345677)
	{
	  cs4248_flag = 1;
	  *ad_flags = 0;
	}

      if (*ad_flags == 0x12345676)
	{
	  cmi8330_flag = 1;
	  *ad_flags = 0;
	}
    }

  devc->base = io_base;
  devc->ctrl_base = cs423x_ctl_port;
  cs423x_ctl_port = 0;
  devc->irq_ok = 0;
  devc->do_poll = 1;
  devc->timer_running = 0;
  devc->MCE_bit = 0x40;
  devc->irq = 0;
  devc->open_mode = 0;
  devc->chip_name = devc->name = "CS4231";
  devc->model = MD_4231;	/* CS4231 or CS4248 */
  devc->levels = NULL;
  devc->spdif_present = 0;
  devc->osdev = osdev;
  devc->debug_flag = 0;

  /*
   * Check that the I/O address is in use.
   *
   * The bit 0x80 of the base I/O port is known to be 0 after the
   * chip has performed its power on initialization. Just assume
   * this has happened before the OS is starting.
   *
   * If the I/O address is unused, it typically returns 0xff.
   */

  if (INB (devc->osdev, devc->base) == 0xff)
    {
      DDB (cmn_err
	   (CE_CONT,
	    "cs4231_detect: The base I/O address appears to be dead\n"));
    }

#if 1
/*
 * Wait for the device to stop initialization
 */
  DDB (cmn_err (CE_CONT, "cs4231_detect() - step 0\n"));

  for (i = 0; i < 10000000; i++)
    {
      unsigned char x = INB (devc->osdev, devc->base);
      if (x == 0xff || !(x & 0x80))
	break;
    }

#endif

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step A\n"));

  if (INB (devc->osdev, devc->base) == 0x80)	/* Not ready. Let's wait */
    ad_leave_MCE (devc);

  if ((INB (devc->osdev, devc->base) & 0x80) != 0x00)	/* Not a CS4231 */
    {
      DDB (cmn_err (CE_WARN, "cs4231 detect error - step A (%02x)\n",
		    (int) INB (devc->osdev, devc->base)));
      return 0;
    }

  /*
   * Test if it's possible to change contents of the indirect registers.
   * Registers 0 and 1 are ADC volume registers. The bit 0x10 is read only
   * so try to avoid using it.
   */

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step B\n"));
  ad_write (devc, 0, 0xaa);
  ad_write (devc, 1, 0x45);	/* 0x55 with bit 0x10 clear */
  tenmicrosec (devc->osdev);

  if ((tmp1 = ad_read (devc, 0)) != 0xaa
      || (tmp2 = ad_read (devc, 1)) != 0x45)
    {
      if (tmp2 == 0x65)		/* AD1847 has couple of bits hardcoded to 1 */
	ad1847_flag = 1;
      else
	{
	  DDB (cmn_err
	       (CE_WARN, "cs4231 detect error - step B (%x/%x)\n", tmp1,
		tmp2));
#if 1
	  if (tmp1 == 0x8a && tmp2 == 0xff)	/* AZT2320 ????? */
	    {
	      DDB (cmn_err (CE_CONT, "Ignoring error\n"));
	    }
	  else
#endif
	    return 0;
	}
    }

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step C\n"));
  ad_write (devc, 0, 0x45);
  ad_write (devc, 1, 0xaa);
  tenmicrosec (devc->osdev);

  if ((tmp1 = ad_read (devc, 0)) != 0x45
      || (tmp2 = ad_read (devc, 1)) != 0xaa)
    {
      if (tmp2 == 0x8a)		/* AD1847 has few bits hardcoded to 1 */
	ad1847_flag = 1;
      else
	{
	  DDB (cmn_err
	       (CE_WARN, "cs4231 detect error - step C (%x/%x)\n", tmp1,
		tmp2));
#if 1
	  if (tmp1 == 0x65 && tmp2 == 0xff)	/* AZT2320 ????? */
	    {
	      DDB (cmn_err (CE_CONT, "Ignoring error\n"));
	    }
	  else
#endif
	    return 0;
	}
    }

  /*
   * The indirect register I12 has some read only bits. Lets
   * try to change them.
   */
  DDB (cmn_err (CE_CONT, "cs4231_detect() - step D\n"));
  tmp = ad_read (devc, 12);
  ad_write (devc, 12, (~tmp) & 0x0f);

  if ((tmp & 0x0f) != ((tmp1 = ad_read (devc, 12)) & 0x0f))
    {
      DDB (cmn_err (CE_WARN, "cs4231 detect error - step D (%x)\n", tmp1));
      return 0;
    }

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

  for (i = 0; i < 16; i++)
    if ((tmp1 = ad_read (devc, i)) != (tmp2 = ad_read (devc, i + 16)))
      {
	DDB (cmn_err
	     (CE_CONT, "cs4231 detect step F(%d/%x/%x) - OPTi chip???\n", i,
	      tmp1, tmp2));
	break;
      }
#endif

  /*
   * Try to switch the chip to mode2 (CS4231) by setting the MODE2 bit (0x40).
   * The bit 0x80 is always 1 in CS4248 and CS4231.
   */

  DDB (cmn_err (CE_CONT, "cs4231_detect() - step G\n"));

  if (ad_flags && *ad_flags == 400)
    *ad_flags = 0;
  else
    ad_write (devc, 12, 0x40);	/* Set mode2, clear 0x80 */

  if (ad_flags)
    *ad_flags = 0;

  tmp1 = ad_read (devc, 12);
  if (tmp1 & 0x80)
    {
      devc->chip_name = "CS4248";	/* Our best knowledge just now */
    }

  if (optiC930 || (tmp1 & 0xc0) == (0x80 | 0x40))
    {
      /*
       *      CS4231 detected - is it?
       *
       *      Verify that setting I0 doesn't change I16.
       */
      DDB (cmn_err (CE_CONT, "cs4231_detect() - step H\n"));
      ad_write (devc, 16, 0);	/* Set I16 to known value */

      ad_write (devc, 0, 0x45);
      if ((tmp1 = ad_read (devc, 16)) != 0x45)	/* No change -> CS4231? */
	{

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

	  DDB (cmn_err (CE_CONT, "cs4231_detect() - step I\n"));
	  tmp1 = ad_read (devc, 25);	/* Original bits */
	  ad_write (devc, 25, ~tmp1);	/* Invert all bits */
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

	      id = ad_read (devc, 25) & 0xe7;
	      full_id = ad_read (devc, 25);
	      if (id == 0x80)	/* Device busy??? */
		id = ad_read (devc, 25) & 0xe7;
	      if (id == 0x80)	/* Device still busy??? */
		id = ad_read (devc, 25) & 0xe7;
	      DDB (cmn_err
		   (CE_CONT, "cs4231_detect() - step J (%02x/%02x)\n", id,
		    ad_read (devc, 25)));

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
	  ad_write (devc, 25, tmp1);	/* Restore bits */

	  DDB (cmn_err (CE_CONT, "cs4231_detect() - step K\n"));
	}
    }

  DDB (cmn_err (CE_CONT, "cs4231_detect() - Detected OK\n"));

  return 1;
}

void
cs4231_init (char *name, int io_base, int irq, int dma_playback,
	     int dma_capture, int share_dma, oss_device_t * osdev)
{
  /*
   * NOTE! If irq < 0, there is another driver which has allocated the IRQ
   *   so that this driver doesn't need to allocate/deallocate it.
   *   The actually used IRQ is ABS(irq).
   */


  int my_dev, my_mixer;
  char dev_name[100];

  // cs4231_devc *devc = NULL;

  cs4231_port_info *portc = NULL;

  devc->irq = (irq > 0) ? irq : 0;
  devc->open_mode = 0;
  devc->timer_ticks = 0;
  devc->dma1 = dma_playback;
  devc->dma2 = dma_capture;
  devc->audio_flags = ADEV_AUTOMODE;
  devc->playback_dev = devc->record_dev = 0;
  if (name != NULL)
    devc->name = name;
  devc->osdev = osdev;

  if (name != NULL && name[0] != 0)
    sprintf (dev_name, "%s (%s)", name, devc->chip_name);
  else
    sprintf (dev_name, "Generic audio codec (%s)", devc->chip_name);

  //conf_printf2 (dev_name, devc->base, devc->irq, dma_playback, dma_capture);

  if (devc->model > MD_1848)
    {
      if (devc->dma1 == devc->dma2 || devc->dma2 == -1 || devc->dma1 == -1)
	devc->audio_flags &= ~ADEV_DUPLEX;
      else
	devc->audio_flags |= ADEV_DUPLEX;
    }

  if ((my_dev = sound_install_audiodrv (AUDIO_DRIVER_VERSION,
					devc->osdev,
					dev_name,
					&cs4231_audio_driver,
					sizeof (audiodrv_t),
					devc->audio_flags,
					ad_format_mask[devc->model],
					devc,
					dma_playback, dma_capture, -1)) < 0)
    {
      return;
    }

  PERMANENT_MALLOC (cs4231_port_info *, portc,
		    sizeof (cs4231_port_info), NULL);
  audio_engines[my_dev]->portc = portc;
  audio_engines[my_dev]->min_block = 512;
  memset ((char *) portc, 0, sizeof (*portc));

  cs4231_init_hw (devc);
#ifdef sparc
  if (devc->levels[31] == 0)
    devc->levels[31] = AUDIO_SPEAKER;
  devc->mixer_output_port = devc->levels[31];
#ifdef sparc
  cs4231_mixer_output_port = devc->levels[31];
#endif
  cs4231_control (CS4231_SET_OUTPUTS, devc->levels[31]);
#endif

  if (irq > 0)
    {
      if (snd_set_irq_handler (devc->irq, cs4231intr, devc->name,
			       devc->osdev, NULL) < 0)
	{
	  cmn_err (CE_WARN, "IRQ in use\n");
	}

#ifdef sparc
/* The ACP chip used in SparcStations doesn't support codec IRQ's */
#else
      if (devc->model != MD_1848)
	{
	  int x;
	  unsigned char tmp = ad_read (devc, 16);

	  devc->timer_ticks = 0;

	  ad_write (devc, 21, 0x00);	/* Timer MSB */
	  ad_write (devc, 20, 0x10);	/* Timer LSB */

	  ad_write (devc, 16, tmp | 0x40);	/* Enable timer */
	  for (x = 0; x < 100000 && devc->timer_ticks == 0; x++);
	  ad_write (devc, 16, tmp & ~0x40);	/* Disable timer */

	  if (devc->timer_ticks == 0)
	    {
	      cmn_err (CE_WARN, "Interrupt test failed (IRQ%d)\n", devc->irq);
	    }
	  else
	    {
	      DDB (cmn_err (CE_CONT, "Interrupt test OK\n"));
	      devc->irq_ok = 1;
	    }
	}
      else
#endif
	devc->irq_ok = 1;	/* Couldn't test. assume it's OK */
    }

  if (!share_dma)
    {
      if (ALLOCATE_DMA_CHN (dma_playback, devc->name, devc->osdev))
	cmn_err (CE_WARN, "Can't allocate DMA%d\n", dma_playback);

      if (dma_capture != dma_playback)
	if (ALLOCATE_DMA_CHN (dma_capture, devc->name, devc->osdev))
	  cmn_err (CE_WARN, "Can't allocate DMA%d\n", dma_capture);
    }

  if ((my_mixer = sound_install_mixer (MIXER_DRIVER_VERSION,
				       dev_name,
				       &cs4231_mixer_operations,
				       sizeof (mixer_driver_t), devc)) >= 0)
    {
      audio_engines[my_dev]->mixer_dev = my_mixer;
      cs4231_mixer_reset (devc);
    }
#if 0
  test_it (devc);
#endif

/*
 * In case this is a full duplex device we install another (daughter)
 * audio device file which permits simultaneous access by different programs
 */

  if (devc->audio_flags & ADEV_DUPLEX)
    {
      int parent = my_dev;

      sprintf (dev_name, "Shadow of audio device #%d", parent);

      if ((my_dev = sound_install_audiodrv (AUDIO_DRIVER_VERSION,
					    devc->osdev,
					    dev_name,
					    &cs4231_audio_driver,
					    sizeof (audiodrv_t),
					    devc->audio_flags | ADEV_SHADOW,
					    ad_format_mask[devc->model],
					    devc,
					    dma_playback,
					    dma_capture, parent)) < 0)
	{
	  return;
	}

      PERMANENT_MALLOC (cs4231_port_info *, portc,
			sizeof (cs4231_port_info), NULL);
      audio_engines[my_dev]->portc = portc;
      audio_engines[my_dev]->mixer_dev = my_mixer;
      audio_engines[my_dev]->min_block = 512;
      memset ((char *) portc, 0, sizeof (*portc));
    }
}

void
cs4231_unload (int io_base, int irq, int dma_playback, int dma_capture,
	       int share_dma)
{
#if 0
  int i, dev = 0;
  cs4231_devc *devc = NULL;

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
  cs4231_devc *devc = osdev->devc;
  int dev;
  int alt_stat = 0xff;
  unsigned char c930_stat = 0;
  int cnt = 0;
  int serviced = 0;

  dev = irq2dev[irq];

  if (dev < 0 || dev >= num_audio_engines)
    {
      for (irq = 0; irq < 17; irq++)
	if (irq2dev[irq] != -1)
	  break;

      if (irq > 15)
	{
	  /* cmn_err(CE_CONT, "Bogus interrupt %d\n", irq); */
	  return 0;
	}

      dev = irq2dev[irq];
      devc = (cs4231_devc *) audio_engines[dev]->devc;
    }
  else
    devc = (cs4231_devc *) audio_engines[dev]->devc;

  devc->do_poll = 0;

interrupt_again:		/* Jump back here if int status doesn't reset */

  status = INB (devc->osdev, io_Status (devc));

  if (status == 0x80)
    cmn_err (CE_CONT, "cs4231intr: Why?\n");
  else
    serviced = 1;
  if (devc->model == MD_1848)
    OUTB (devc->osdev, 0, io_Status (devc));	/* Clear interrupt status */

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
	  DMABUF_inputintr (devc->record_dev);
	}

      if (devc->open_mode & OPEN_WRITE && devc->audio_mode & PCM_ENABLE_OUTPUT
	  && alt_stat & 0x10)
	{
	  DMABUF_outputintr (devc->playback_dev, 1);
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
  if (INB (devc->osdev, io_Status (devc)) & 0x01 && cnt++ < 4)
    {
      goto interrupt_again;
    }
#endif
  return serviced;
}

int
oss_audiocs_attach (oss_device_t * osdev)
{
	// TODO: Not implemented yet
	return 0;
}

int
oss_audiocs_detach (oss_device_t * osdev)
{
	// TODO: Not implemented yet
	return 0;
}
