/*
 * Purpose: Driver for C-Media CMI8788 PCI audio controller.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006-2007. All rights reserved.

#include "cmi8788_cfg.h"
#include <oss_pci.h>
#include <uart401.h>
#include <ac97.h>

#define CMEDIA_VENDOR_ID	0x13F6
#define CMEDIA_CMI8788		0x8788
/*
 * CM8338 registers definition
 */

#define RECA_ADDR		(devc->base+0x00)
#define RECA_SIZE		(devc->base+0x04)
#define RECA_FRAG		(devc->base+0x06)
#define RECB_ADDR		(devc->base+0x08)
#define RECB_SIZE		(devc->base+0x0C)
#define RECB_FRAG		(devc->base+0x0E)
#define RECC_ADDR		(devc->base+0x10)
#define RECC_SIZE		(devc->base+0x14)
#define RECC_FRAG		(devc->base+0x16)
#define SPDIF_ADDR		(devc->base+0x18)
#define SPDIF_SIZE		(devc->base+0x1C)
#define SPDIF_FRAG		(devc->base+0x1E)
#define MULTICH_ADDR		(devc->base+0x20)
#define MULTICH_SIZE		(devc->base+0x24)
#define MULTICH_FRAG		(devc->base+0x28)
#define FPOUT_ADDR		(devc->base+0x30)
#define FPOUT_SIZE		(devc->base+0x34)
#define FPOUT_FRAG		(devc->base+0x36)

#define DMA_START		(devc->base+0x40)
#define CHAN_RESET		(devc->base+0x42)
#define MULTICH_MODE		(devc->base+0x43)
#define IRQ_MASK		(devc->base+0x44)
#define IRQ_STAT		(devc->base+0x46)
#define MISC_REG		(devc->base+0x48)
#define REC_FORMAT		(devc->base+0x4A)
#define PLAY_FORMAT		(devc->base+0x4B)
#define REC_MODE		(devc->base+0x4C)
#define FUNCTION		(devc->base+0x50)

#define I2S_MULTICH_FORMAT	(devc->base+0x60)
#define I2S_ADC1_FORMAT		(devc->base+0x62)
#define I2S_ADC2_FORMAT		(devc->base+0x64)
#define I2S_ADC3_FORMAT		(devc->base+0x66)

#define SPDIF_FUNC		(devc->base+0x70)
#define SPDIFOUT_CHAN_STAT	(devc->base+0x74)
#define SPDIFIN_CHAN_STAT	(devc->base+0x78)

#define TWO_WIRE_ADDR		(devc->base+0x90)
#define TWO_WIRE_MAP		(devc->base+0x91)
#define TWO_WIRE_DATA		(devc->base+0x92)
#define TWO_WIRE_CTRL		(devc->base+0x94)

#define SPI_CONTROL		(devc->base+0x98)
#define SPI_DATA		(devc->base+0x99)

#define MPU401_DATA		(devc->base+0xA0)
#define MPU401_COMMAND		(devc->base+0xA1)
#define MPU401_CONTROL		(devc->base+0xA2)

#define GPI_DATA		(devc->base+0xA4)
#define GPI_IRQ_MASK		(devc->base+0xA5)
#define GPIO_DATA		(devc->base+0xA6)
#define GPIO_CONTROL		(devc->base+0xA8)
#define GPIO_IRQ_MASK		(devc->base+0xAA)
#define DEVICE_SENSE		(devc->base+0xAC)

#define PLAY_ROUTING		(devc->base+0xC0)
#define REC_ROUTING		(devc->base+0xC2)
#define REC_MONITOR		(devc->base+0xC3)
#define MONITOR_ROUTING		(devc->base+0xC4)

#define AC97_CTRL		(devc->base+0xD0)
#define AC97_INTR_MASK		(devc->base+0xD2)
#define AC97_INTR_STAT		(devc->base+0xD3)
#define AC97_OUT_CHAN_CONFIG	(devc->base+0xD4)
#define AC97_IN_CHAN_CONFIG	(devc->base+0xD8)
#define AC97_CMD_DATA		(devc->base+0xDC)

#define CODEC_VERSION		(devc->base+0xE4)
#define CTRL_VERSION		(devc->base+0xE6)

/* defs for AKM 4396 DAC */
#define AK4396_CTL1        0x00
#define AK4396_CTL2        0x01
#define AK4396_CTL3        0x02
#define AK4396_LchATTCtl   0x03
#define AK4396_RchATTCtl   0x04

#define UNUSED_CMI9780_CONTROLS ( \
        SOUND_MASK_VOLUME | \
        SOUND_MASK_PCM | \
        SOUND_MASK_REARVOL | \
        SOUND_MASK_CENTERVOL | \
        SOUND_MASK_SIDEVOL | \
        SOUND_MASK_SPEAKER | \
        SOUND_MASK_ALTPCM | \
        SOUND_MASK_VIDEO | \
        SOUND_MASK_DEPTH | \
        SOUND_MASK_MONO | \
        SOUND_MASK_PHONE \
        )

typedef struct cmi8788_portc
{
  int speed, bits, channels;
  int open_mode;
  int trigger_bits;
  int audio_enabled;
  int audiodev;
  int port_type;
#define ADEV_MULTICH	0
#define ADEV_FRONTPANEL	2
#define ADEV_SPDIF	3
  int play_dma_start, rec_dma_start;
  int play_irq_mask, rec_irq_mask;
  int play_chan_reset, rec_chan_reset;
  int min_rate, max_rate, min_chan, max_chan;
}
cmi8788_portc;

#define MAX_PORTC 4

typedef struct cmi8788_devc
{
  oss_device_t *osdev;
  oss_native_word base;
  int fm_attached;
  int irq;
  int dma_len;
  volatile unsigned char intr_mask;
  int model;
#define MDL_CMI8788		1
  char *chip_name;

  /* Audio parameters */
  oss_mutex_t mutex;
  oss_mutex_t low_mutex;
  int open_mode;
  cmi8788_portc portc[MAX_PORTC];

  /* Mixer */
  ac97_devc ac97devc, fp_ac97devc;
  int ac97_mixer_dev, fp_mixer_dev, spi_mixer_dev;
  int playvol[4];
  /* uart401 */
  oss_midi_inputbyte_t midi_input_intr;
  int midi_opened, midi_disabled;
  volatile unsigned char input_byte;
  int midi_dev;
  int mpu_attached;

}
cmi8788_devc;

static void cmi8788uartintr (cmi8788_devc * devc);
static int reset_cmi8788uart (cmi8788_devc * devc);
static void enter_uart_mode (cmi8788_devc * devc);

static int
ac97_read (void *devc_, int reg)
{
  cmi8788_devc *devc = devc_;
  oss_native_word flags;
  int val, data;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
  val = 0L;
  val |= 0 << 24;		/*codec 0 */
  val |= 1 << 23;		/*ac97 read the reg address */
  val |= reg << 16;
  OUTL (devc->osdev, val, AC97_CMD_DATA);
  oss_udelay (100);
  data = INL (devc->osdev, AC97_CMD_DATA) & 0xFFFF;
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return data;
}

static int
ac97_write (void *devc_, int reg, int data)
{
  cmi8788_devc *devc = devc_;
  oss_native_word flags;
  int val;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  val = 0L;
  val |= 0 << 24;		/*on board codec */
  val |= 0 << 23;		/*ac97 write operation */
  val |= reg << 16;
  val |= data & 0xFFFF;
  OUTL (devc->osdev, val, AC97_CMD_DATA);
  oss_udelay (100);
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return 1;
}

static int
fp_ac97_read (void *devc_, int reg)
{
  cmi8788_devc *devc = devc_;
  oss_native_word flags;
  int data, val;
  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
  val = 0L;
  val |= 1 << 24;		/*fp codec1 */
  val |= 1 << 23;		/*ac97 read the reg address */
  val |= reg << 16;
  OUTL (devc->osdev, val, AC97_CMD_DATA);
  oss_udelay (100);
  data = INL (devc->osdev, AC97_CMD_DATA) & 0xFFFF;
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return data;
}

static int
fp_ac97_write (void *devc_, int reg, int data)
{
  cmi8788_devc *devc = devc_;
  oss_native_word flags;
  int val;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  val = 0L;
  val |= 1 << 24;		/*fp codec1 */
  val |= 0 << 23;		/*ac97 write operation */
  val |= reg << 16;
  val |= data & 0xFFFF;
  OUTL (devc->osdev, val, AC97_CMD_DATA);
  oss_udelay (100);
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return 1;
}

static int
spi_write (void *devc_, int codec_num, unsigned char *data)
{
  cmi8788_devc *devc = devc_;
  oss_native_word flags;
  unsigned char val;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  /* write 2-byte data values */
  OUTB (devc->osdev, data[0], SPI_DATA + 0);
  OUTB (devc->osdev, data[1], SPI_DATA + 1);

  /* Latch high, clock=160, Len=2byte, mode=write */
  val = (INB (devc->osdev, SPI_CONTROL) & ~0x7E) | 0x81;

  /* now address which codec you want to send the data to */
  val |= (codec_num << 4) & 0x70;

  /* send the command to write the data */
  OUTB (devc->osdev, val, SPI_CONTROL);

  oss_udelay (100);
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return 1;
}

#if 0
static int
two_wire_write (void *devc_, int codec_num, unsigned char reg,
		unsigned int data)
{
  cmi8788_devc *devc = devc_;
  oss_native_word flags;
  unsigned char status;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
  status = INW (devc->osdev, TWO_WIRE_CTRL);
  if (status & 0x1)
    {
      cmn_err (CE_WARN, "Two-Wire interface busy\n");
      return -EIO;
    }

  /* first write the Register Address into the MAP register */
  OUTB (devc->osdev, reg, TWO_WIRE_MAP);

  /* now write the data */
  OUTW (devc->osdev, data, TWO_WIRE_DATA);

  /* select the codec number to address */
  OUTB (devc->osdev, codec_num << 1 | 0x1, TWO_WIRE_ADDR);

  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return 1;

}

static unsigned int
mix_scale (int vol, int bits)
{
  vol = mix_cvt[vol];
  return (vol * ((1 << bits) - 1) / 100);
}
#endif

static int
cmi8788_set_play_volume (cmi8788_devc * devc, int codec_id, int value)
{
  int left, right;
  unsigned char data[2];

  left = value & 0xff;
  right = (value >> 8) & 0xff;

  devc->playvol[codec_id] = left | (right << 8);

  data[0] = left;
  data[1] = AK4396_LchATTCtl | 0x20;
  spi_write (devc, codec_id, data);

  data[0] = right;
  data[1] = AK4396_RchATTCtl | 0x20;
  spi_write (devc, codec_id, data);

  return devc->playvol[codec_id];
}

static int
cmi8788intr (oss_device_t * osdev)
{
  cmi8788_devc *devc = (cmi8788_devc *) osdev->devc;
  unsigned int intstat;
  int i;
  int serviced = 0;


  intstat = INW (devc->osdev, IRQ_STAT);
  if (intstat != 0)
    serviced = 1;
  else
    return 0;

  for (i = 0; i < MAX_PORTC; i++)
    {
      cmi8788_portc *portc = &devc->portc[i];

      /* Handle Playback Ints */
      if ((intstat & portc->play_irq_mask)
	  && (portc->trigger_bits & PCM_ENABLE_OUTPUT))
	{

	  /* Acknowledge the interrupt by disabling and enabling the irq */
	  OUTW (devc->osdev,
		INW (devc->osdev, IRQ_MASK) & ~portc->play_irq_mask,
		IRQ_MASK);
	  OUTW (devc->osdev,
		INW (devc->osdev, IRQ_MASK) | portc->play_irq_mask, IRQ_MASK);

	  /* process buffer */
	  oss_audio_outputintr (portc->audiodev, 0);
	}

      /* Handle Record Ints */
      if ((intstat & portc->rec_irq_mask)
	  && (portc->trigger_bits & PCM_ENABLE_INPUT))
	{
	  /* disable the interrupt first */
	  OUTW (devc->osdev,
		INW (devc->osdev, IRQ_MASK) & ~portc->rec_irq_mask, IRQ_MASK);
	  /* enable the interrupt mask */
	  OUTW (devc->osdev,
		INW (devc->osdev, IRQ_MASK) | portc->rec_irq_mask, IRQ_MASK);
	  oss_audio_inputintr (portc->audiodev, 0);
	}

    }

  /* MPU interrupt */
  if (intstat & 0x1000)
    {
      cmi8788uartintr (devc);
    }

  return serviced;
}

static int
cmi8788_audio_set_rate (int dev, int arg)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->speed;

  if (arg > portc->max_rate)
    arg = portc->max_rate;
  if (arg < portc->min_rate)
    arg = portc->min_rate;

  portc->speed = arg;
  return portc->speed;
}

static short
cmi8788_audio_set_channels (int dev, short arg)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;

  if (audio_engines[dev]->flags & ADEV_STEREOONLY)
    arg = 2;

  if (arg == 1)
    arg = 2;

  if (arg>8)
     arg=8;
  if ((arg != 2) && (arg != 4) && (arg != 6) && (arg != 8))
    return portc->channels;
  portc->channels = arg;

  return portc->channels;
}

static unsigned int
cmi8788_audio_set_format (int dev, unsigned int arg)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->bits;

  if (audio_engines[dev]->flags & ADEV_16BITONLY)
    arg = AFMT_S16_LE;

  if (!(arg & (AFMT_S16_LE | AFMT_AC3 | AFMT_S32_LE)))
    return portc->bits;
  portc->bits = arg;
  return portc->bits;
}

/*ARGSUSED*/
static int
cmi8788_audio_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  return -EINVAL;
}

static void cmi8788_audio_trigger (int dev, int state);

static void
cmi8788_audio_reset (int dev)
{
  cmi8788_audio_trigger (dev, 0);
}

static void
cmi8788_audio_reset_input (int dev)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;
  cmi8788_audio_trigger (dev, portc->trigger_bits & ~PCM_ENABLE_INPUT);
}

static void
cmi8788_audio_reset_output (int dev)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;
  cmi8788_audio_trigger (dev, portc->trigger_bits & ~PCM_ENABLE_OUTPUT);
}

/*ARGSUSED*/
static int
cmi8788_audio_open (int dev, int mode, int open_flags)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;
  cmi8788_devc *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (portc->open_mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  if (devc->open_mode & mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  devc->open_mode |= mode;

  portc->open_mode = mode;
  portc->audio_enabled &= ~mode;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

static void
cmi8788_audio_close (int dev, int mode)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;
  cmi8788_devc *devc = audio_engines[dev]->devc;

  cmi8788_audio_reset (dev);
  portc->open_mode = 0;
  devc->open_mode &= ~mode;
  portc->audio_enabled &= ~mode;
}

/*ARGSUSED*/
static void
cmi8788_audio_output_block (int dev, oss_native_word buf, int count,
			    int fragsize, int intrflag)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;

  portc->audio_enabled |= PCM_ENABLE_OUTPUT;
  portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
}

/*ARGSUSED*/
static void
cmi8788_audio_start_input (int dev, oss_native_word buf, int count,
			   int fragsize, int intrflag)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;

  portc->audio_enabled |= PCM_ENABLE_INPUT;
  portc->trigger_bits &= ~PCM_ENABLE_INPUT;
}

static void
cmi8788_audio_trigger (int dev, int state)
{
  cmi8788_portc *portc = audio_engines[dev]->portc;
  cmi8788_devc *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (portc->open_mode & OPEN_WRITE)
    {
      if (state & PCM_ENABLE_OUTPUT)
	{
	  if ((portc->audio_enabled & PCM_ENABLE_OUTPUT) &&
	      !(portc->trigger_bits & PCM_ENABLE_OUTPUT))
	    {
	      /* Enable Interrupt */
	      OUTW (devc->osdev,
		    INW (devc->osdev, IRQ_MASK) | portc->play_irq_mask,
		    IRQ_MASK);
	      /* enable the dma */
	      OUTW (devc->osdev,
		    INW (devc->osdev, DMA_START) | portc->play_dma_start,
		    DMA_START);
	      portc->trigger_bits |= PCM_ENABLE_OUTPUT;
	    }
	}
      else
	{
	  if ((portc->audio_enabled & PCM_ENABLE_OUTPUT) &&
	      (portc->trigger_bits & PCM_ENABLE_OUTPUT))
	    {
	      portc->audio_enabled &= ~PCM_ENABLE_OUTPUT;
	      portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;

	      /* disable dma */
	      OUTW (devc->osdev,
		    INW (devc->osdev, DMA_START) & ~portc->play_dma_start,
		    DMA_START);
	      /* Disable Interrupt */
	      OUTW (devc->osdev,
		    INW (devc->osdev, IRQ_MASK) & ~portc->play_irq_mask,
		    IRQ_MASK);
	    }
	}
    }

  if (portc->open_mode & OPEN_READ)
    {
      if (state & PCM_ENABLE_INPUT)
	{
	  if ((portc->audio_enabled & PCM_ENABLE_INPUT) &&
	      !(portc->trigger_bits & PCM_ENABLE_INPUT))
	    {
	      /* Enable Interrupt */
	      OUTW (devc->osdev,
		    INW (devc->osdev, IRQ_MASK) | portc->rec_irq_mask,
		    IRQ_MASK);

	      /* enable the channel */
	      OUTW (devc->osdev,
		    INW (devc->osdev, DMA_START) | portc->rec_dma_start,
		    DMA_START);
	      portc->trigger_bits |= PCM_ENABLE_INPUT;
	    }
	}
      else
	{
	  if ((portc->audio_enabled & PCM_ENABLE_INPUT) &&
	      (portc->trigger_bits & PCM_ENABLE_INPUT))
	    {
	      portc->trigger_bits &= ~PCM_ENABLE_INPUT;
	      portc->audio_enabled &= ~PCM_ENABLE_INPUT;
	      /* disable channel  */
	      OUTW (devc->osdev,
		    INW (devc->osdev, DMA_START) & ~portc->rec_dma_start,
		    DMA_START);

	      /* Disable Interrupt */
	      OUTW (devc->osdev,
		    INW (devc->osdev, IRQ_MASK) & ~portc->rec_irq_mask,
		    IRQ_MASK);

	    }
	}
    }
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
cmi8788_reset_channel (cmi8788_devc * devc, cmi8788_portc * portc,
		       int direction)
{
  if (direction == PCM_ENABLE_OUTPUT)
    {
      /* reset the channel */
      OUTB (devc->osdev,
	    INB (devc->osdev, CHAN_RESET) | portc->play_chan_reset,
	    CHAN_RESET);
      oss_udelay (10);
      OUTB (devc->osdev,
	    INB (devc->osdev, CHAN_RESET) & ~portc->play_chan_reset,
	    CHAN_RESET);
    }

  if (direction == PCM_ENABLE_INPUT)
    {
      /* reset the channel */
      OUTB (devc->osdev,
	    INB (devc->osdev, CHAN_RESET) | portc->rec_chan_reset,
	    CHAN_RESET);
      oss_udelay (10);
      OUTB (devc->osdev,
	    INB (devc->osdev, CHAN_RESET) & ~portc->rec_chan_reset,
	    CHAN_RESET);
    }

}

static int
i2s_calc_rate (int rate)
{
  int i2s_rate;

  switch (rate)
    {
    case 32000:
      i2s_rate = 0;
      break;
    case 44100:
      i2s_rate = 1;
      break;
    case 48000:
      i2s_rate = 2;
      break;
    case 64000:
      i2s_rate = 3;
      break;
    case 88200:
      i2s_rate = 4;
      break;
    case 96000:
      i2s_rate = 5;
      break;
    case 176400:
      i2s_rate = 6;
      break;
    case 192000:
      i2s_rate = 7;
      break;
    default:
      i2s_rate = 2;
      break;
    }

  return i2s_rate;
}

int
i2s_calc_bits (int bits)
{
  int i2s_bits;

  switch (bits)
    {
#if 0
    case AFMT_S24_LE:
      i2s_bits = 0x80;
      break;
#endif
    case AFMT_S32_LE:
      i2s_bits = 0xC0;
      break;
    default:			/* AFMT_S16_LE */
      i2s_bits = 0x00;
      break;
    }

  return i2s_bits;
}

/*ARGSUSED*/
static int
cmi8788_audio_prepare_for_input (int dev, int bsize, int bcount)
{
  cmi8788_devc *devc = audio_engines[dev]->devc;
  cmi8788_portc *portc = audio_engines[dev]->portc;
  dmap_p dmap = audio_engines[dev]->dmap_in;
  oss_native_word flags;
  int channels, bits, i2s_bits, i2s_rate;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  switch (portc->port_type)
    {
    case ADEV_MULTICH:
      {
	portc->rec_dma_start = 0x1;
	portc->rec_irq_mask = 0x1;
	portc->rec_chan_reset = 0x1;
	cmi8788_reset_channel (devc, portc, PCM_ENABLE_INPUT);

	OUTL (devc->osdev, dmap->dmabuf_phys, RECA_ADDR);
	OUTW (devc->osdev, dmap->bytes_in_use / 4 - 1, RECA_SIZE);
	OUTW (devc->osdev, dmap->fragment_size / 4 - 1, RECA_FRAG);

	switch (portc->channels)
	  {
	  case 4:
	    channels = 0x1;
	    break;
	  case 6:
	    channels = 0x2;
	    break;
	  case 8:
	    channels = 0x4;
	    break;
	  default:
	    channels = 0x0;	/* Stereo */
	    break;
	  }

	OUTB (devc->osdev, (INB (devc->osdev, REC_MODE) & ~0x3) | channels,
	      REC_MODE);

	switch (portc->bits)
	  {
#if 0
	    /* The 24 bit format supported by cmi8788 is not AFMT_S24_LE */
	  case AFMT_S24_LE:
	    bits = 0x1;
	    break;
#endif
	  case AFMT_S32_LE:
	    bits = 0x2;
	    break;
	  default:		/* AFMT_S16_LE */
	    bits = 0x0;
	    break;
	  }
	OUTB (devc->osdev, (INB (devc->osdev, REC_FORMAT) & ~0x3) | bits,
	      REC_FORMAT);

	/* set up the i2s bits as well */
	i2s_bits = i2s_calc_bits (portc->bits);
	OUTB (devc->osdev,
	      (INB (devc->osdev, I2S_ADC1_FORMAT) & ~0xC0) | i2s_bits,
	      I2S_ADC1_FORMAT);

	/* setup the i2s speed */
	i2s_rate = i2s_calc_rate (portc->speed);
	OUTB (devc->osdev,
	      (INB (devc->osdev, I2S_ADC1_FORMAT) & ~0x7) | i2s_rate,
	      I2S_ADC1_FORMAT);

	break;
      }

    case ADEV_FRONTPANEL:
      {
	portc->rec_dma_start = 0x2;
	portc->rec_irq_mask = 0x2;
	portc->rec_chan_reset = 0x2;
	cmi8788_reset_channel (devc, portc, PCM_ENABLE_INPUT);

	OUTL (devc->osdev, dmap->dmabuf_phys, RECB_ADDR);
	OUTW (devc->osdev, dmap->bytes_in_use / 4 - 1, RECB_SIZE);
	OUTW (devc->osdev, dmap->fragment_size / 4 - 1, RECB_FRAG);
	ac97_recrate (&devc->fp_ac97devc, portc->speed);

	break;
      }

    case ADEV_SPDIF:
      {
	portc->rec_dma_start = 0x4;
	portc->rec_irq_mask = 0x4;
	portc->rec_chan_reset = 0x4;
	cmi8788_reset_channel (devc, portc, PCM_ENABLE_INPUT);

	OUTL (devc->osdev, dmap->dmabuf_phys, RECC_ADDR);
	OUTW (devc->osdev, dmap->bytes_in_use / 4 - 1, RECC_SIZE);
	OUTW (devc->osdev, dmap->fragment_size / 4 - 1, RECC_FRAG);

	switch (portc->bits)
	  {
#if 0
	  case AFMT_S24_LE:
	    bits = 0x10;
	    break;
#endif
	  case AFMT_S32_LE:
	    bits = 0x20;
	    break;
	  default:		/*  AFMT_S16_LE */
	    bits = 0x0;
	    break;
	  }

	OUTB (devc->osdev, (INB (devc->osdev, REC_FORMAT) & ~0x30) | bits,
	      REC_FORMAT);

	/* setup i2s bits */
	i2s_bits = i2s_calc_bits (portc->bits);
	OUTB (devc->osdev,
	      (INB (devc->osdev, I2S_ADC3_FORMAT) & ~0xC0) | i2s_bits,
	      I2S_ADC3_FORMAT);

	/* setup speed */
	i2s_rate = i2s_calc_rate (portc->speed);
	OUTB (devc->osdev,
	      (INB (devc->osdev, I2S_ADC3_FORMAT) & ~0x7) | i2s_rate,
	      I2S_ADC3_FORMAT);
	break;
      }
    }
  portc->audio_enabled &= ~PCM_ENABLE_INPUT;
  portc->trigger_bits &= ~PCM_ENABLE_INPUT;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

/*ARGSUSED*/
static int
cmi8788_audio_prepare_for_output (int dev, int bsize, int bcount)
{
  cmi8788_devc *devc = audio_engines[dev]->devc;
  cmi8788_portc *portc = audio_engines[dev]->portc;
  dmap_p dmap = audio_engines[dev]->dmap_out;
  oss_native_word flags;
  int i2s_rate, rate, spdif_rate, bits = 0, i2s_bits, channels = 0;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  switch (portc->port_type)
    {
    case ADEV_MULTICH:
      {
	portc->play_dma_start = 0x10;
	portc->play_irq_mask = 0x10;
	portc->play_chan_reset = 0x10;
	cmi8788_reset_channel (devc, portc, PCM_ENABLE_OUTPUT);

	OUTL (devc->osdev, dmap->dmabuf_phys, MULTICH_ADDR);
	OUTL (devc->osdev, dmap->bytes_in_use / 4 - 1, MULTICH_SIZE);
	OUTL (devc->osdev, dmap->fragment_size / 4 - 1, MULTICH_FRAG);

	switch (portc->channels)
	  {
	  case 2:
	    channels = 0;
	    break;
	  case 4:
	    channels = 1;
	    break;
	  case 6:
	    channels = 2;
	    break;
	  case 8:
	    channels = 3;
	    break;
	  }

	OUTB (devc->osdev,
	      (INB (devc->osdev, MULTICH_MODE) & ~0x3) | channels,
	      MULTICH_MODE);

	/* setup bits per sample */
	switch (portc->bits)
	  {
#if 0
	  case AFMT_S24_LE:
	    bits = 4;
	    break;
#endif
	  case AFMT_S32_LE:
	    bits = 8;
	    break;

	  default:		/* AFMT_S16_LE */
	    bits = 0;
	    break;
	  }

	/* set the format bits in play format register */
	OUTB (devc->osdev, (INB (devc->osdev, PLAY_FORMAT) & ~0xC) | bits,
	      PLAY_FORMAT);

	/* setup the i2s bits in the i2s register */
	i2s_bits = i2s_calc_bits (portc->bits);
	OUTB (devc->osdev,
	      (INB (devc->osdev, I2S_MULTICH_FORMAT) & ~0xC0) | i2s_bits,
	      I2S_MULTICH_FORMAT);

	/* setup speed */
	i2s_rate = i2s_calc_rate (portc->speed);
	OUTB (devc->osdev,
	      (INB (devc->osdev, I2S_MULTICH_FORMAT) & ~0x7) | i2s_rate,
	      I2S_MULTICH_FORMAT);

	break;
      }

    case ADEV_FRONTPANEL:
      {
	portc->play_dma_start = 0x20;
	portc->play_irq_mask = 0x20;
	portc->play_chan_reset = 0x20;
	cmi8788_reset_channel (devc, portc, PCM_ENABLE_OUTPUT);

	OUTL (devc->osdev, dmap->dmabuf_phys, FPOUT_ADDR);
	OUTW (devc->osdev, dmap->bytes_in_use / 4 - 1, FPOUT_SIZE);
	OUTW (devc->osdev, dmap->fragment_size / 4 - 1, FPOUT_FRAG);
	ac97_playrate (&devc->fp_ac97devc, portc->speed);
	ac97_spdif_setup (devc->fp_mixer_dev, portc->speed, portc->bits);

	break;
      }

    case ADEV_SPDIF:
      {
	portc->play_dma_start = 0x08;
	portc->play_irq_mask = 0x08;
	portc->play_chan_reset = 0x08;
	cmi8788_reset_channel (devc, portc, PCM_ENABLE_OUTPUT);

	/* STOP SPDIF Out */
	OUTL (devc->osdev, (INL (devc->osdev, SPDIF_FUNC) & ~0x00000002),
	      SPDIF_FUNC);

	/* setup AC3 for 16bit 48Khz, Non-Audio */
	if (portc->bits == AFMT_AC3)
	  {
	    portc->bits = 16;
	    portc->channels = 2;
	    portc->speed = 48000;

	    /* set the PCM/Data bit to Non-Audio */
	    OUTL (devc->osdev,
		  (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x0002) | 0x0002,
		  SPDIFOUT_CHAN_STAT);
	  }
	else
	  OUTL (devc->osdev,
		INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x0002,
		SPDIFOUT_CHAN_STAT);

	OUTL (devc->osdev, dmap->dmabuf_phys, SPDIF_ADDR);
	OUTW (devc->osdev, dmap->bytes_in_use / 4 - 1, SPDIF_SIZE);
	OUTW (devc->osdev, dmap->fragment_size / 4 - 1, SPDIF_FRAG);

	/* setup number of bits/sample */
	switch (portc->bits)
	  {
	  case 16:
	    bits = 0;
	    break;
	  case 24:
	    bits = 1;
	    break;
	  case 32:
	    bits = 2;
	    break;
	  }

	OUTB (devc->osdev, (INB (devc->osdev, PLAY_FORMAT) & ~0x3) | bits,
	      PLAY_FORMAT);

	/* setup sampling rate */
	switch (portc->speed)
	  {
	  case 32000:
	    rate = 0;
	    spdif_rate = 0x3;
	    break;
	  case 44100:
	    rate = 1;
	    spdif_rate = 0x0;
	    break;
	  case 48000:
	    rate = 2;
	    spdif_rate = 0x2;
	    break;
	  case 64000:
	    rate = 3;
	    spdif_rate = 0xb;
	    break;
	  case 88200:
	    rate = 4;
	    spdif_rate = 0x8;
	    break;
	  case 96000:
	    rate = 5;
	    spdif_rate = 0xa;
	    break;
	  case 176400:
	    rate = 6;
	    spdif_rate = 0xc;
	    break;
	  case 192000:
	    rate = 7;
	    spdif_rate = 0xe;
	    break;
	  default:
	    rate = 2;		/* 48000 */
	    spdif_rate = 0x2;
	    break;
	  }

	OUTL (devc->osdev,
	      (INL (devc->osdev, SPDIF_FUNC) & ~0x0f000000) | rate << 24,
	      SPDIF_FUNC);

	/* also program the Channel status */
	OUTL (devc->osdev,
	      (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF000) | spdif_rate
	      << 12, SPDIFOUT_CHAN_STAT);

	/* Enable SPDIF Out */
	OUTL (devc->osdev,
	      (INL (devc->osdev, SPDIF_FUNC) & ~0x00000002) | 0x2,
	      SPDIF_FUNC);

	break;
      }
    }
  portc->audio_enabled &= ~PCM_ENABLE_OUTPUT;
  portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

static int
cmi8788_get_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
  cmi8788_devc *devc = audio_engines[dev]->devc;
  cmi8788_portc *portc = audio_engines[dev]->portc;
  unsigned int ptr = 0;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
  if (direction == PCM_ENABLE_INPUT)
    {
      switch (portc->port_type)
	{
	case ADEV_MULTICH:
	  ptr = INL (devc->osdev, RECA_ADDR);
	  break;
	case ADEV_FRONTPANEL:
	  ptr = INL (devc->osdev, RECB_ADDR);
	  break;
	case ADEV_SPDIF:
	  ptr = INL (devc->osdev, RECC_ADDR);
	  break;
	}
    }

  if (direction == PCM_ENABLE_OUTPUT)
    {
      switch (portc->port_type)
	{
	case ADEV_MULTICH:
	  ptr = INL (devc->osdev, MULTICH_ADDR);
	  break;
	case ADEV_FRONTPANEL:
	  ptr = INL (devc->osdev, FPOUT_ADDR);
	  break;
	case ADEV_SPDIF:
	  ptr = INL (devc->osdev, SPDIF_ADDR);
	  break;
	}
    }

  ptr -= dmap->dmabuf_phys;
  ptr %= dmap->bytes_in_use;
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  // cmn_err (CE_CONT, "ptr=%x\n", ptr);
  return ptr;
}


static audiodrv_t cmi8788_audio_driver = {
  cmi8788_audio_open,
  cmi8788_audio_close,
  cmi8788_audio_output_block,
  cmi8788_audio_start_input,
  cmi8788_audio_ioctl,
  cmi8788_audio_prepare_for_input,
  cmi8788_audio_prepare_for_output,
  cmi8788_audio_reset,
  NULL,
  NULL,
  cmi8788_audio_reset_input,
  cmi8788_audio_reset_output,
  cmi8788_audio_trigger,
  cmi8788_audio_set_rate,
  cmi8788_audio_set_format,
  cmi8788_audio_set_channels,
  NULL,
  NULL,
  NULL,				/* cmi8788_check_input, */
  NULL,				/* cmi8788_check_output, */
  NULL,				/* cmi8788_alloc_buffer */
  NULL,				/* cmi8788_free_buffer */
  NULL,
  NULL,
  cmi8788_get_buffer_pointer
};

#define input_avail(devc) (!(cmi8788uart_status(devc)&INPUT_AVAIL))
#define output_ready(devc)      (!(cmi8788uart_status(devc)&OUTPUT_READY))

static __inline__ int
cmi8788uart_status (cmi8788_devc * devc)
{
  return INB (devc->osdev, MPU401_COMMAND);
}

static void
cmi8788uart_cmd (cmi8788_devc * devc, unsigned char cmd)
{
  OUTB (devc->osdev, cmd, MPU401_COMMAND);
}

static __inline__ int
cmi8788uart_read (cmi8788_devc * devc)
{
  return INB (devc->osdev, MPU401_DATA);
}

static __inline__ void
cmi8788uart_write (cmi8788_devc * devc, unsigned char byte)
{
  OUTB (devc->osdev, byte, MPU401_DATA);
}

#define OUTPUT_READY    0x40
#define INPUT_AVAIL     0x80
#define MPU_ACK         0xFE
#define MPU_RESET       0xFF
#define UART_MODE_ON    0x3F


static void
cmi8788uart_input_loop (cmi8788_devc * devc)
{
  while (input_avail (devc))
    {
      unsigned char c = cmi8788uart_read (devc);

      if (c == MPU_ACK)
	devc->input_byte = c;
      else if (devc->midi_opened & OPEN_READ && devc->midi_input_intr)
	devc->midi_input_intr (devc->midi_dev, c);
    }
}

static void
cmi8788uartintr (cmi8788_devc * devc)
{
  cmi8788uart_input_loop (devc);
}

/*ARGSUSED*/
static int
cmi8788uart_open (int dev, int mode, oss_midi_inputbyte_t inputbyte,
		  oss_midi_inputbuf_t inputbuf,
		  oss_midi_outputintr_t outputintr)
{
  cmi8788_devc *devc = (cmi8788_devc *) midi_devs[dev]->devc;

  if (devc->midi_opened)
    {
      return -EBUSY;
    }

  while (input_avail (devc))
    cmi8788uart_read (devc);

  devc->midi_input_intr = inputbyte;
  devc->midi_opened = mode;
  enter_uart_mode (devc);
  devc->midi_disabled = 0;

  return 0;
}

/*ARGSUSED*/
static void
cmi8788uart_close (int dev, int mode)
{
  cmi8788_devc *devc = (cmi8788_devc *) midi_devs[dev]->devc;
  reset_cmi8788uart (devc);
  oss_udelay (10);
  enter_uart_mode (devc);
  reset_cmi8788uart (devc);
  devc->midi_opened = 0;
}


static int
cmi8788uart_out (int dev, unsigned char midi_byte)
{
  int timeout;
  cmi8788_devc *devc = (cmi8788_devc *) midi_devs[dev]->devc;
  oss_native_word flags;

  /*
   * Test for input since pending input seems to block the output.
   */

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (input_avail (devc))
    cmi8788uart_input_loop (devc);

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  /*
   * Sometimes it takes about 130000 loops before the output becomes ready
   * (After reset). Normally it takes just about 10 loops.
   */

  for (timeout = 130000; timeout > 0 && !output_ready (devc); timeout--);

  if (!output_ready (devc))
    {
      cmn_err (CE_WARN, "UART timeout - Device not responding\n");
      devc->midi_disabled = 1;
      reset_cmi8788uart (devc);
      enter_uart_mode (devc);
      return 1;
    }

  cmi8788uart_write (devc, midi_byte);
  return 1;
}

/*ARGSUSED*/
static int
cmi8788uart_ioctl (int dev, unsigned cmd, ioctl_arg arg)
{
  return -EINVAL;
}

static midi_driver_t cmi8788_midi_driver = {
  cmi8788uart_open,
  cmi8788uart_close,
  cmi8788uart_ioctl,
  cmi8788uart_out,
};

static void
enter_uart_mode (cmi8788_devc * devc)
{
  int ok, timeout;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  for (timeout = 30000; timeout > 0 && !output_ready (devc); timeout--);

  devc->input_byte = 0;
  cmi8788uart_cmd (devc, UART_MODE_ON);

  ok = 0;
  for (timeout = 50000; timeout > 0 && !ok; timeout--)
    if (devc->input_byte == MPU_ACK)
      ok = 1;
    else if (input_avail (devc))
      if (cmi8788uart_read (devc) == MPU_ACK)
	ok = 1;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}


void
attach_cmi8788uart (cmi8788_devc * devc)
{
  enter_uart_mode (devc);
  devc->midi_dev = oss_install_mididev (OSS_MIDI_DRIVER_VERSION, "CMI8788", "CMI8788 UART", &cmi8788_midi_driver, sizeof (midi_driver_t), NULL,	/* &std_midi_synth, */
					0, devc, devc->osdev);
  devc->midi_opened = 0;
}

static int
reset_cmi8788uart (cmi8788_devc * devc)
{
  int ok, timeout, n;

  /*
   * Send the RESET command. Try again if no success at the first time.
   */

  ok = 0;

  for (n = 0; n < 2 && !ok; n++)
    {
      for (timeout = 30000; timeout > 0 && !output_ready (devc); timeout--);

      devc->input_byte = 0;
      cmi8788uart_cmd (devc, MPU_RESET);

      /*
       * Wait at least 25 msec. This method is not accurate so let's make the
       * loop bit longer. Cannot sleep since this is called during boot.
       */

      for (timeout = 50000; timeout > 0 && !ok; timeout--)
	if (devc->input_byte == MPU_ACK)	/* Interrupt */
	  ok = 1;
	else if (input_avail (devc))
	  if (cmi8788uart_read (devc) == MPU_ACK)
	    ok = 1;

    }



  if (ok)
    cmi8788uart_input_loop (devc);	/*
					 * Flush input before enabling interrupts
					 */

  return ok;
}


int
probe_cmi8788uart (cmi8788_devc * devc)
{
  int ok = 0;
  oss_native_word flags;

  DDB (cmn_err (CE_CONT, "Entered probe_cmi8788uart\n"));

  devc->midi_input_intr = NULL;
  devc->midi_opened = 0;
  devc->input_byte = 0;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  ok = reset_cmi8788uart (devc);
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  if (ok)
    {
      DDB (cmn_err (CE_CONT, "Reset UART401 OK\n"));
    }
  else
    {
      DDB (cmn_err
	   (CE_CONT, "Reset UART401 failed (no hardware present?).\n"));
      DDB (cmn_err
	   (CE_CONT, "mpu401 status %02x\n", cmi8788uart_status (devc)));
    }

  DDB (cmn_err (CE_CONT, "cmi8788uart detected OK\n"));
  return ok;
}

void
unload_cmi8788uart (cmi8788_devc * devc)
{
  reset_cmi8788uart (devc);
}


static void
attach_mpu (cmi8788_devc * devc)
{
  devc->mpu_attached = 1;
  attach_cmi8788uart (devc);
}

/*ARGSUSED*/
static int
cmi8788_mixer_ioctl (int dev, int audiodev, unsigned int cmd, ioctl_arg arg)
{
  cmi8788_devc *devc = mixer_devs[dev]->devc;

  if (((cmd >> 8) & 0xff) == 'M')
    {
      int val;

      if (IOC_IS_OUTPUT (cmd))
	switch (cmd & 0xff)
	  {
	  case SOUND_MIXER_RECSRC:
	    return *arg = 0;
	    break;

	  case SOUND_MIXER_PCM:
	    val = *arg;
	    return *arg = cmi8788_set_play_volume (devc, 0, val);
	    break;

	  case SOUND_MIXER_REARVOL:
	    val = *arg;
	    return *arg = cmi8788_set_play_volume (devc, 1, val);
	    break;

	  case SOUND_MIXER_CENTERVOL:
	    val = *arg;
	    return *arg = cmi8788_set_play_volume (devc, 2, val);
	    break;

	  case SOUND_MIXER_SIDEVOL:
	    val = *arg;
	    return *arg = cmi8788_set_play_volume (devc, 3, val);
	    break;

	  default:
	    val = *arg;
	    return *arg = cmi8788_set_play_volume (devc, 0, val);
	    break;
	  }
      else
	switch (cmd & 0xff)	/* Return Parameter */
	  {
	  case SOUND_MIXER_RECSRC:
	    return *arg = 0;
	    break;

	  case SOUND_MIXER_DEVMASK:
	    return *arg =
	      SOUND_MASK_PCM | SOUND_MASK_REARVOL | SOUND_MASK_CENTERVOL |
	      SOUND_MASK_SIDEVOL;
	    break;

	  case SOUND_MIXER_STEREODEVS:
	    return *arg =
	      SOUND_MASK_PCM | SOUND_MASK_REARVOL | SOUND_MASK_CENTERVOL |
	      SOUND_MASK_SIDEVOL;
	    break;

	  case SOUND_MIXER_CAPS:
	    return *arg = SOUND_CAP_EXCL_INPUT;
	    break;

	  case SOUND_MIXER_PCM:
	    return *arg = devc->playvol[0];
	    break;

	  case SOUND_MIXER_REARVOL:
	    return *arg = devc->playvol[1];
	    break;

	  case SOUND_MIXER_CENTERVOL:
	    return *arg = devc->playvol[2];
	    break;

	  case SOUND_MIXER_SIDEVOL:
	    return *arg = devc->playvol[3];
	    break;

	  default:
	    return *arg = devc->playvol[0];
	    break;
	  }
    }
  else
    return *arg = 0;

}

static mixer_driver_t cmi8788_mixer_driver = {
  cmi8788_mixer_ioctl
};

/********************Record/Play ROUTING Control *************************/

static int
cmi8788_ext (int dev, int ctrl, unsigned int cmd, int value)
{
/*
 * Access function for CMPCI mixer extension bits
 */
  cmi8788_devc *devc = mixer_devs[dev]->devc;

  if (cmd == SNDCTL_MIX_READ)
    {
      value = 0;
      switch (ctrl)
	{
	case 0:		/* Record Monitor */
	  value = (INB (devc->osdev, REC_MONITOR) & 0x1) ? 1 : 0;
	  break;
	case 1:		/* Front Panel Monitor */
	  value = (INB (devc->osdev, REC_MONITOR) & 0x4) ? 1 : 0;
	  break;
	case 2:		/* SPDIFIN Monitor */
	  value = (INB (devc->osdev, REC_MONITOR) & 0x10) ? 1 : 0;
	  break;
	case 3:		/* Speaker Spread - just check bit15 to see if it's set */
	  value = (INW (devc->osdev, PLAY_ROUTING) & 0x8000) ? 0 : 1;
	  break;
	case 4:		/* SPDIF IN->OUT Loopback */
	  value = (INW (devc->osdev, SPDIF_FUNC) & 0x4) ? 1 : 0;
	  break;

	default:
	  return -EINVAL;
	}

      return value;
    }


  if (cmd == SNDCTL_MIX_WRITE)
    {
      switch (ctrl)
	{
	case 0:		/* Record Monitor */
	  if (value)
	    OUTB (devc->osdev, INB (devc->osdev, REC_MONITOR) | 0x1,
		  REC_MONITOR);
	  else
	    OUTB (devc->osdev, INB (devc->osdev, REC_MONITOR) & ~0x1,
		  REC_MONITOR);
	  break;

	case 1:		/* Front Panel Record Monitor */
	  if (value)
	    OUTB (devc->osdev, INB (devc->osdev, REC_MONITOR) | 0x4,
		  REC_MONITOR);
	  else
	    OUTB (devc->osdev, INB (devc->osdev, REC_MONITOR) & ~0x4,
		  REC_MONITOR);
	  break;

	case 2:		/* SPDIFIN Monitor */
	  if (value)
	    OUTB (devc->osdev, INB (devc->osdev, REC_MONITOR) | 0x10,
		  REC_MONITOR);
	  else
	    OUTB (devc->osdev, INB (devc->osdev, REC_MONITOR) & ~0x10,
		  REC_MONITOR);
	  break;

	case 3:		/* Speaker Spread (clone front to all channels) */
	  if (value)
	    OUTW (devc->osdev, INW (devc->osdev, PLAY_ROUTING) & 0x00FF,
		  PLAY_ROUTING);
	  else
	    OUTW (devc->osdev,
		  (INW (devc->osdev, PLAY_ROUTING) & 0x00FF) | 0xE400,
		  PLAY_ROUTING);
	  break;

	case 4:		/* SPDIF IN->OUT Loopback */
	  if (value)
	    OUTW (devc->osdev, INW (devc->osdev, SPDIF_FUNC) | 0x4,
		  SPDIF_FUNC);
	  else
	    OUTW (devc->osdev, INW (devc->osdev, SPDIF_FUNC) & ~0x4,
		  SPDIF_FUNC);
	  break;

	default:
	  return -EINVAL;
	}
      return (value);
    }
  return -EINVAL;
}


/********************SPDIFOUT Control *************************/
int
spdifout_ctl (int dev, int ctrl, unsigned int cmd, int value)
{
  int tmp = 0;
  cmi8788_devc *devc = mixer_devs[dev]->devc;

  if (cmd == SNDCTL_MIX_READ)
    {
      value = 0;
      switch (ctrl)
	{
	case SPDIFOUT_ENABLE:	/* SPDIF OUT */
	  value = (INL (devc->osdev, SPDIF_FUNC) & 0x2) ? 1 : 0;
	  break;

	case SPDIFOUT_PRO:	/* Consumer/PRO */
	  value = (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & 0x1) ? 1 : 0;
	  break;

	case SPDIFOUT_AUDIO:	/* PCM/AC3 */
	  value = (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & 0x2) ? 1 : 0;
	  break;

	case SPDIFOUT_COPY:	/* Copy Prot */
	  value = (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & 0x4) ? 1 : 0;
	  break;

	case SPDIFOUT_PREEMPH:	/* Pre emphasis */
	  value = (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & 0x8) ? 1 : 0;
	  break;

	case SPDIFOUT_RATE:	/* Sampling Rate */
	  tmp = (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & 0xf000);
	  switch (tmp)
	    {
	    case 0x0000:	/* 44100 */
	      value = 0;
	      break;
	    case 0x2000:	/* 48000 */
	      value = 1;
	      break;
	    case 0x3000:	/* 32000 */
	      value = 2;
	      break;
	    case 0x8000:	/* 88200 */
	      value = 3;
	      break;
	    case 0xA000:	/* 96000 */
	      value = 4;
	      break;
	    case 0xB000:	/* 64000 */
	      value = 5;
	      break;
	    case 0xC000:	/* 176400 */
	      value = 6;
	      break;
	    case 0xE000:	/* 192000 */
	      value = 7;
	      break;
	    default:
	      cmn_err (CE_WARN, "unsupported SPDIF F/S rate\n");
	      break;
	    }
	  break;

	case SPDIFOUT_VBIT:	/* V Bit */
	  value = (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & 0x10000) ? 1 : 0;
	  break;

	case SPDIFOUT_ADC:	/* Analog In to SPDIF Out */
	  value = (INW (devc->osdev, PLAY_ROUTING) & 0x80) ? 1 : 0;
	  break;

	default:
	  break;
	}

      return value;
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
      switch (ctrl)
	{
	case SPDIFOUT_ENABLE:	/* Enable SPDIF OUT */
	  if (value)
	    OUTL (devc->osdev, INL (devc->osdev, SPDIF_FUNC) | 0x2,
		  SPDIF_FUNC);
	  else
	    OUTL (devc->osdev, INL (devc->osdev, SPDIF_FUNC) & ~0x2,
		  SPDIF_FUNC);
	  break;

	case SPDIFOUT_PRO:	/* consumer/pro audio */
	  if (value)
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) | 0x1,
		  SPDIFOUT_CHAN_STAT);
	  else
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x1,
		  SPDIFOUT_CHAN_STAT);
	  break;

	case SPDIFOUT_AUDIO:	/* PCM/AC3 */
	  if (value)
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) | 0x2,
		  SPDIFOUT_CHAN_STAT);
	  else
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x2,
		  SPDIFOUT_CHAN_STAT);
	  break;

	case SPDIFOUT_COPY:	/* copy prot */
	  if (value)
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) | 0x4,
		  SPDIFOUT_CHAN_STAT);
	  else
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x4,
		  SPDIFOUT_CHAN_STAT);
	  break;

	case SPDIFOUT_PREEMPH:	/* preemphasis */
	  if (value)
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) | 0x8,
		  SPDIFOUT_CHAN_STAT);
	  else
	    OUTL (devc->osdev, INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x8,
		  SPDIFOUT_CHAN_STAT);
	  break;

	case SPDIFOUT_RATE:	/* Frequency */
	  switch (value)
	    {
	    case 0:		/* 44100 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) | 0x0,
		    SPDIFOUT_CHAN_STAT);
	      break;
	    case 1:		/* 48000 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0x2000, SPDIFOUT_CHAN_STAT);
	      break;
	    case 2:		/* 32000 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0x3000, SPDIFOUT_CHAN_STAT);
	      break;
	    case 3:		/* 88000 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0x8000, SPDIFOUT_CHAN_STAT);
	      break;
	    case 4:		/* 96000 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0xA000, SPDIFOUT_CHAN_STAT);
	      break;
	    case 5:		/* 64000 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0xB000, SPDIFOUT_CHAN_STAT);
	      break;
	    case 6:		/* 176400 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0xC000, SPDIFOUT_CHAN_STAT);
	      break;
	    case 7:		/* 192000 */
	      OUTL (devc->osdev,
		    (INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0xF0000) |
		    0xE000, SPDIFOUT_CHAN_STAT);
	      break;

	    default:
	      break;
	    }
	  break;

	case SPDIFOUT_VBIT:	/* V Bit */
	  if (value)
	    OUTL (devc->osdev,
		  INL (devc->osdev, SPDIFOUT_CHAN_STAT) | 0x10000,
		  SPDIFOUT_CHAN_STAT);
	  else
	    OUTL (devc->osdev,
		  INL (devc->osdev, SPDIFOUT_CHAN_STAT) & ~0x10000,
		  SPDIFOUT_CHAN_STAT);
	  break;

	case SPDIFOUT_ADC:	/* Analog In to SPDIF Out */
	  if (value)
	    OUTW (devc->osdev, INW (devc->osdev, PLAY_ROUTING) | 0xA0,
		  PLAY_ROUTING);
	  else
	    OUTW (devc->osdev, INW (devc->osdev, PLAY_ROUTING) & ~0xE0,
		  PLAY_ROUTING);
	  break;

	default:
	  break;
	}

      return (value);
    }

  return -EINVAL;
}

static int
cmi8788_mix_init (int dev)
{
  int group, parent, ctl;

  if ((parent = mixer_ext_create_group (dev, 0, "EXT")) < 0)
    return parent;

/* CREATE MONITOR */
  if ((group = mixer_ext_create_group (dev, parent, "MONITOR")) < 0)
    return group;

  if ((ctl =
       mixer_ext_create_control (dev, group, 0, cmi8788_ext,
				 MIXT_ONOFF, "MULTICHANNEL", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, 1, cmi8788_ext,
				 MIXT_ONOFF, "FRONTPANEL", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, 2, cmi8788_ext,
				 MIXT_ONOFF, "SPDIF", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

/* Create PLAYBACK ROUTING */
  if ((group = mixer_ext_create_group (dev, parent, "OUTPUT_ROUTING")) < 0)
    return group;

  if ((ctl =
       mixer_ext_create_control (dev, group, 3, cmi8788_ext,
				 MIXT_ONOFF, "SPEAKER-SPREAD", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, 4, cmi8788_ext,
				 MIXT_ONOFF, "SPDIF-LOOPBACK", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

/* Create SPDIF OUTPUT */
  if ((group = mixer_ext_create_group (dev, 0, "SPDIF-OUT")) < 0)
    return group;

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_ENABLE,
				 spdifout_ctl, MIXT_ONOFF,
				 "SPDOUT_ENABLE", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_ADC,
				 spdifout_ctl, MIXT_ONOFF,
				 "SPDOUT_ADC/DAC", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_PRO,
				 spdifout_ctl, MIXT_ENUM,
				 "SPDOUT_Pro", 2,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;
  mixer_ext_set_strings (dev, ctl, "Consumer Professional", 0);

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_AUDIO,
				 spdifout_ctl, MIXT_ENUM,
				 "SPDOUT_Audio", 2,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;
  mixer_ext_set_strings (dev, ctl, "Audio Data", 0);

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_COPY,
				 spdifout_ctl, MIXT_ONOFF,
				 "SPDOUT_Copy", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_PREEMPH,
				 spdifout_ctl, MIXT_ONOFF,
				 "SPDOUT_Pre-emph", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_RATE,
				 spdifout_ctl, MIXT_ENUM,
				 "SPDOUT_Rate", 8,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  mixer_ext_set_strings (dev, ctl,
			 "44.1KHz 48KHz 32KHz 88.2KHz 96KHz 64KHz 176.4KHz 192KHz",
			 0);

  if ((ctl =
       mixer_ext_create_control (dev, group, SPDIFOUT_VBIT,
				 spdifout_ctl, MIXT_ONOFF,
				 "SPDOUT_VBit", 1,
				 MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return ctl;

  return 0;
}


static int
init_cmi8788 (cmi8788_devc * devc)
{
  unsigned short sVal;
  unsigned char bVal;
  int i, first_dev = -1, count;
  int default_vol;
/*
 * Init CMI Controller
 */
  sVal = INW (devc->osdev, CTRL_VERSION);
  if (!(sVal & 0x0008))
    {
      bVal = INB (devc->osdev, MISC_REG);
      bVal |= 0x20;
      OUTB (devc->osdev, bVal, MISC_REG);
    }

  bVal = INB (devc->osdev, FUNCTION);
  bVal |= 0x82;			/*reset codec */
  OUTB (devc->osdev, bVal, FUNCTION);

  /* Setup I2S to use 16bit instead of 24Bit */
  OUTW (devc->osdev, 0x010A, I2S_MULTICH_FORMAT);
  OUTW (devc->osdev, 0x010A, I2S_ADC1_FORMAT);
  OUTW (devc->osdev, 0x010A, I2S_ADC2_FORMAT);
  OUTW (devc->osdev, 0x010A, I2S_ADC3_FORMAT);

  /* setup Routing regs (default vals) */
  OUTW (devc->osdev, 0xE400, PLAY_ROUTING);
  OUTB (devc->osdev, 0x00, REC_ROUTING);
  OUTB (devc->osdev, 0x00, REC_MONITOR);
  OUTB (devc->osdev, 0xE4, MONITOR_ROUTING);

  /* install the CMI8788 mixer */
  if ((devc->spi_mixer_dev = oss_install_mixer (OSS_MIXER_DRIVER_VERSION,
						devc->osdev,
						devc->osdev,
						"CMedia CMI8788",
						&cmi8788_mixer_driver,
						sizeof (mixer_driver_t),
						devc)) < 0)
    {
      return 0;
    }

  mixer_devs[devc->spi_mixer_dev]->devc = devc;
  mixer_devs[devc->spi_mixer_dev]->hw_devc = devc;
  mixer_devs[devc->spi_mixer_dev]->priority = 10;	/* Possible default mixer candidate */
  mixer_ext_set_init_fn (devc->spi_mixer_dev, cmi8788_mix_init, 25);

  /* Cold reset onboard AC97 */
  OUTW (devc->osdev, 0x1, AC97_CTRL);
  count = 100;
  while ((INW (devc->osdev, AC97_CTRL) & 0x2) && (count--))
    {
      OUTW (devc->osdev, (INW (devc->osdev, AC97_CTRL) & ~0x2) | 0x2,
	    AC97_CTRL);
      oss_udelay (100);
    }
  if (!count)
    cmn_err (CE_WARN, "CMI8788 AC97 not ready\n");


  sVal = INW (devc->osdev, AC97_CTRL);

  devc->ac97_mixer_dev = devc->fp_mixer_dev = -1;

  /* check if there's an onboard AC97 codec (CODEC 0)  and install the mixer */
  if (sVal & 0x10)
    {
      /* disable CODEC0 OUTPUT */
      OUTW (devc->osdev, INW (devc->osdev, AC97_OUT_CHAN_CONFIG) & ~0xFF00,
	    AC97_OUT_CHAN_CONFIG);

      /* enable CODEC0 INPUT */
      OUTW (devc->osdev, INW (devc->osdev, AC97_IN_CHAN_CONFIG) | 0x0300,
	    AC97_IN_CHAN_CONFIG);

      /* set Playback routing to I2S mode */
      OUTW (devc->osdev, INW (devc->osdev, PLAY_ROUTING) & ~0x10,
	    PLAY_ROUTING);

      /* setup record channel A (MULTICH) and B (Front Panel) in AC97 mode */
      OUTB (devc->osdev, INB (devc->osdev, REC_ROUTING) | 0x18, REC_ROUTING);

      devc->ac97_mixer_dev =
	ac97_install (&devc->ac97devc, "AC97 Input Mixer", ac97_read,
		      ac97_write, devc, devc->osdev);

      if (devc->ac97_mixer_dev >= 0)
	{
	  /* setup the Codec0 as the input mux */
	  ac97_write (devc, 0x70, 0x0300);
	  ac97_write (devc, 0x64, 0x8041);
	  ac97_write (devc, 0x62, 0x180F);
	  ac97_remove_control (&devc->ac97devc, UNUSED_CMI9780_CONTROLS, 0);
	}
    }

  /* check if there's an front panel AC97 codec (CODEC1) and install the mixer */
  if (sVal & 0x20)
    {
      /* enable CODEC1 OUTPUT */
      OUTW (devc->osdev, INW (devc->osdev, AC97_OUT_CHAN_CONFIG) | 0x0033,
	    AC97_OUT_CHAN_CONFIG);
      /* enable CODEC1 INPUT */
      OUTW (devc->osdev, INW (devc->osdev, AC97_IN_CHAN_CONFIG) | 0x0033,
	    AC97_IN_CHAN_CONFIG);

      devc->fp_mixer_dev =
	ac97_install (&devc->fp_ac97devc, "AC97 Mixer (Front Panel)",
		      fp_ac97_read, fp_ac97_write, devc, devc->osdev);

      if (devc->fp_mixer_dev >= 0)
	{
	  /* enable S/PDIF */
	  devc->fp_ac97devc.spdif_slot = SPDIF_SLOT34;
	  ac97_spdifout_ctl (devc->fp_mixer_dev, SPDIFOUT_ENABLE,
			     SNDCTL_MIX_WRITE, 1);
	}
    }


  /* check if MPU401 is enabled in MISC register */
  if (INB (devc->osdev, MISC_REG) & 0x40)
    attach_mpu (devc);

  for (i = 0; i < MAX_PORTC; i++)
    {
      char tmp_name[100];
      cmi8788_portc *portc = &devc->portc[i];
      int caps = ADEV_AUTOMODE;
      int fmt = AFMT_S16_LE;

      switch (i)
	{
	case 0:
	  sprintf (tmp_name, "%s (MultiChannel)", devc->chip_name);
	  caps |= ADEV_DUPLEX;
	  fmt |= AFMT_S32_LE;
	  portc->port_type = ADEV_MULTICH;
	  portc->min_rate = 32000;
	  portc->max_rate = 192000;
	  portc->min_chan = 2;
	  portc->max_chan = 8;
	  break;

	case 1:
	  sprintf (tmp_name, "%s (Multichannel)", devc->chip_name);
	  caps |= ADEV_DUPLEX | ADEV_SHADOW;
	  fmt |= AFMT_S32_LE;
	  portc->port_type = ADEV_MULTICH;
	  portc->min_rate = 32000;
	  portc->max_rate = 192000;
	  portc->min_chan = 2;
	  portc->max_chan = 8;
	  break;

	case 2:
	  /* if there is no front panel AC97, then skip the device */
	  if (devc->fp_mixer_dev == -1)
	    continue;

	  sprintf (tmp_name, "%s (Front Panel)", devc->chip_name);
	  caps |=
	    ADEV_DUPLEX | ADEV_16BITONLY | ADEV_STEREOONLY | ADEV_SPECIAL;
	  fmt |= AFMT_AC3;
	  portc->port_type = ADEV_FRONTPANEL;
	  portc->min_rate = 8000;
	  portc->max_rate = 48000;
	  portc->min_chan = 2;
	  portc->max_chan = 2;
	  break;

	case 3:
	  sprintf (tmp_name, "%s (SPDIF)", devc->chip_name);
	  caps |= ADEV_DUPLEX | ADEV_STEREOONLY | ADEV_SPECIAL;
	  fmt |= AFMT_AC3 | AFMT_S32_LE;
	  portc->port_type = ADEV_SPDIF;
	  portc->min_rate = 32000;
	  portc->max_rate = 192000;
	  portc->min_chan = 2;
	  portc->max_chan = 2;
	  break;
	}

      if ((portc->audiodev =
	   oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION, devc->osdev,
				 devc->osdev, tmp_name,
				 &cmi8788_audio_driver,
				 sizeof (audiodrv_t), caps, fmt,
				 NULL, -1)) < 0)
	{
	  return 0;
	}
      else
	{
	  if (first_dev == -1)
	    first_dev = portc->audiodev;
	  audio_engines[portc->audiodev]->devc = devc;
	  audio_engines[portc->audiodev]->portc = portc;
	  audio_engines[portc->audiodev]->rate_source = first_dev;
	  if (caps & ADEV_FIXEDRATE)
	    {
	      audio_engines[portc->audiodev]->min_rate = 48000;
	      audio_engines[portc->audiodev]->max_rate = 48000;
	      audio_engines[portc->audiodev]->fixed_rate = 48000;
	    }
	  audio_engines[portc->audiodev]->min_rate = portc->min_rate;
	  audio_engines[portc->audiodev]->max_rate = portc->max_rate;

	  audio_engines[portc->audiodev]->caps |= DSP_CAP_FREERATE;
	  audio_engines[portc->audiodev]->min_channels = portc->min_chan;
	  audio_engines[portc->audiodev]->max_channels = portc->max_chan;
	  portc->open_mode = 0;
	  portc->audio_enabled = 0;
	}
    }

  /*
   * Setup the default volumes to 75%
   */
  default_vol = 0x4b4b;
  devc->playvol[0] =
    cmi8788_mixer_ioctl (devc->spi_mixer_dev, first_dev,
			 MIXER_WRITE (SOUND_MIXER_PCM), &default_vol);
  devc->playvol[1] =
    cmi8788_mixer_ioctl (devc->spi_mixer_dev, first_dev,
			 MIXER_WRITE (SOUND_MIXER_REARVOL), &default_vol);
  devc->playvol[2] =
    cmi8788_mixer_ioctl (devc->spi_mixer_dev, first_dev,
			 MIXER_WRITE (SOUND_MIXER_CENTERVOL), &default_vol);
  devc->playvol[3] =
    cmi8788_mixer_ioctl (devc->spi_mixer_dev, first_dev,
			 MIXER_WRITE (SOUND_MIXER_SIDEVOL), &default_vol);

  return 1;
}

int
cmi8788_attach (oss_device_t * osdev)
{
  unsigned char pci_irq_line, pci_revision;
  unsigned short pci_command, vendor, device;
  unsigned int pci_ioaddr;
  int err;
  cmi8788_devc *devc;

  DDB (cmn_err (CE_CONT, "Entered CMEDIA CMI8788 attach routine\n"));
  pci_read_config_word (osdev, PCI_VENDOR_ID, &vendor);
  pci_read_config_word (osdev, PCI_DEVICE_ID, &device);

  if (vendor != CMEDIA_VENDOR_ID || device != CMEDIA_CMI8788)
    return 0;

  pci_read_config_byte (osdev, PCI_REVISION_ID, &pci_revision);
  pci_read_config_word (osdev, PCI_COMMAND, &pci_command);
  pci_read_config_irq (osdev, PCI_INTERRUPT_LINE, &pci_irq_line);
  pci_read_config_dword (osdev, PCI_BASE_ADDRESS_0, &pci_ioaddr);

  DDB (cmn_err (CE_WARN, "CMI8788 I/O base %04x\n", pci_ioaddr));

  if (pci_ioaddr == 0)
    {
      cmn_err (CE_WARN, "I/O address not assigned by BIOS.\n");
      return 0;
    }

  if (pci_irq_line == 0)
    {
      cmn_err (CE_WARN, "IRQ not assigned by BIOS (%d).\n", pci_irq_line);
      return 0;
    }

  if ((devc = PMALLOC (osdev, sizeof (*devc))) == NULL)
    {
      cmn_err (CE_WARN, "Out of memory\n");
      return 0;
    }


  devc->osdev = osdev;
  osdev->devc = devc;
  devc->irq = pci_irq_line;

  /* Map the IO Base address */
  devc->base = MAP_PCI_IOADDR (devc->osdev, 0, pci_ioaddr);

  /* Remove I/O space marker in bit 0. */
  devc->base &= ~3;

  /* set the PCI_COMMAND register to master mode */
  pci_command |= PCI_COMMAND_MASTER | PCI_COMMAND_IO;
  pci_write_config_word (osdev, PCI_COMMAND, pci_command);

  if (device == CMEDIA_CMI8788)
    {
      devc->model = MDL_CMI8788;
      devc->chip_name = "CMedia CMI8788";
    }
  else
    {
      cmn_err (CE_WARN, "Unknown CMI8788 model\n");
      return 0;
    }

  MUTEX_INIT (devc->osdev, devc->mutex, MH_DRV);
  MUTEX_INIT (devc->osdev, devc->low_mutex, MH_DRV + 1);

  oss_register_device (osdev, devc->chip_name);

  if ((err = oss_register_interrupts (devc->osdev, 0, cmi8788intr, NULL)) < 0)
    {
      cmn_err (CE_WARN, "Can't allocate IRQ%d, err=%d\n", pci_irq_line, err);
      return 0;
    }

  return init_cmi8788 (devc);	/* Detected */
}

int
cmi8788_detach (oss_device_t * osdev)
{
  cmi8788_devc *devc = (cmi8788_devc *) osdev->devc;

  if (oss_disable_device (osdev) < 0)
    return 0;

  if (devc->mpu_attached)
    unload_cmi8788uart (devc);

  oss_unregister_interrupts (devc->osdev);

  MUTEX_CLEANUP (devc->mutex);
  MUTEX_CLEANUP (devc->low_mutex);

  oss_unregister_device (osdev);
  return 1;
}
