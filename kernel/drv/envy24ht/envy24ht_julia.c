/*
 * Purpose: Low level routines for the ESI (Egosys) Juli@ card
 *
 * The Juli@ mixer driver is incomplete and doesn't work yet.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2005. All rights reserved.

#include "envy24ht_cfg.h"

#include "spdif.h"
#include "envy24ht.h"

#define AK4358_ADDRESS 0x11
#define AK4114_ADDRESS 0x10

#if 0
static unsigned char
i2c_read (envy24ht_devc * devc, unsigned char addr, unsigned char pos)
{
  int i;
  unsigned char data;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);
  OUTB (devc->osdev, pos, devc->ccs_base + 0x11);	/* Offset */
  OUTB (devc->osdev, addr << 1, devc->ccs_base + 0x10);	/* Read address  */

  for (i = 0; i < 2000; i++)
    {
      unsigned char status = INB (devc->osdev, devc->ccs_base + 0x13);
      if (!(status & 1))
	break;

    }

  oss_udelay (1);
  data = INB (devc->osdev, devc->ccs_base + 0x12);
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);

  return data;
}
#endif

static void
i2c_write (envy24ht_devc * devc, unsigned char addr, unsigned char pos,
	   unsigned char data)
{
  int i;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  for (i = 0; i < 2000; i++)
    {
      unsigned char status = INB (devc->osdev, devc->ccs_base + 0x13);
      if (!(status & 1))
	break;

    }

  OUTB (devc->osdev, pos, devc->ccs_base + 0x11);	/* Offset */
  OUTB (devc->osdev, data, devc->ccs_base + 0x12);	/* Data */
  OUTB (devc->osdev, (addr << 1) | 1, devc->ccs_base + 0x10);	/* Write address  */

  for (i = 0; i < 2000; i++)
    {
      unsigned char status = INB (devc->osdev, devc->ccs_base + 0x13);
      if (!(status & 1))
	break;

    }
  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);

  if ((addr == AK4358_ADDRESS) && (pos <= 0x0c))
    devc->m_DACVolume[pos] = data;
}

static void
GPIOWrite (envy24ht_devc * devc, int pos, int bit)
{
  int data = INW (devc->osdev, devc->ccs_base + 0x14);

  bit = (bit != 0);

  data &= ~(1 << pos);
  data |= (bit << pos);

  OUTW (devc->osdev, data, devc->ccs_base + 0x14);
}

static int
set_dac (envy24ht_devc * devc, int reg, int level)
{
  if (level < 0)
    level = 0;
  if (level > 0x7f)
    level = 0x7f;

  i2c_write (devc, AK4358_ADDRESS, reg, level | 0x80);

  return level;
}

static struct { 
    int rate, spdifin, clks;
} rate_sel[] = {
    {32000, 0x30, 0x08},
    {44100, 0x00, 0x09},
    {48000, 0x20, 0x0A},
    {88200, 0x80, 0x05},
    {96000, 0xA0, 0x06},
    {176400, 0xC0, 0x01},
    {192000, 0xE0, 0x02},
    {16000, -1, 0x0C},
    {22050, -1, 0x0D},
    {24000, -1, 0x0E},
    {64000, -1, 0x04},
    {128000, -1, 0x00},
    {-1, -1, -1}
};

#if 0
static int
julia_get_spdifin_rate (envy24ht_devc *devc)
{
  int i;
  int spdifin = i2c_read (devc, AK4114_ADDRESS, 7);
  
  for (i = 0; rate_sel[i].rate; i++)
    if (rate_sel[i].spdifin == spdifin)
	return rate_sel[i].rate;
  
  return 0;
}
#endif

static void
julia_Monitor (envy24ht_devc * devc, int bMonitor, int num)
{
  switch (num)
    {
    case 0:	/* MUTE */
	if (bMonitor)
    	  {
	    i2c_write (devc, AK4358_ADDRESS, 1, 0x03);
	    GPIOWrite (devc, 15, 1);
	  } else {
    	    i2c_write (devc, AK4358_ADDRESS, 1, 0x01);
	    GPIOWrite (devc, 15, 0);
	  }
	break;

    case 1:	/* LINEIN */
	if (bMonitor)
    	    GPIOWrite (devc, 13, 1);
	else
    	    GPIOWrite (devc, 13, 0);
	break;

    case 2:	/* SPDIFOUT */
	if (bMonitor)
    	    GPIOWrite (devc, 11, 1);
	else
    	    GPIOWrite (devc, 11, 0);
	break;
    case 3:	/* SPDIFIN */
	if (bMonitor)
    	    GPIOWrite (devc, 12, 1);
	else
    	    GPIOWrite (devc, 12, 0);
	break;
    }
  devc->monitor[num] = bMonitor;
}

static void
ak4114_init (envy24ht_devc * devc)
{
  /*
   * AK4114 S/PDIF interface initialization
   */
  i2c_write (devc, AK4114_ADDRESS, 0x00, 0x0f);
  i2c_write (devc, AK4114_ADDRESS, 0x01, 0x70);
  i2c_write (devc, AK4114_ADDRESS, 0x02, 0x80);
  i2c_write (devc, AK4114_ADDRESS, 0x03, 0x49);
  i2c_write (devc, AK4114_ADDRESS, 0x04, 0x00);
  i2c_write (devc, AK4114_ADDRESS, 0x05, 0x00);

  i2c_write (devc, AK4114_ADDRESS, 0x0d, 0x41);
  i2c_write (devc, AK4114_ADDRESS, 0x0e, 0x02);
  i2c_write (devc, AK4114_ADDRESS, 0x0f, 0x2c);
  i2c_write (devc, AK4114_ADDRESS, 0x10, 0x00);
  i2c_write (devc, AK4114_ADDRESS, 0x11, 0x00);
}

static void
julia_set_rate (envy24ht_devc * devc)
{
  int i, data;
  
  for (i = 0; rate_sel[i].rate; i++)
    if ((rate_sel[i].rate == devc->speed)||(rate_sel[i].rate == -1)) 
	break;
  
    /* Set rate by GPIO */
  if (rate_sel[i].rate != -1)
    {
	data = INW (devc->osdev, devc->ccs_base + 0x14);
	data &= ~0x0f;
	data |= rate_sel[i].clks;
	OUTW (devc->osdev, data, devc->ccs_base + 0x14);
    }

  i2c_write (devc, AK4358_ADDRESS, 0, 0x06); /* reset ak4358 */

  if (devc->speed <= 48000)
	devc->m_DACVolume[2] = 0x4f; /* DFS=normal-speed */	
  else if (devc->speed <= 96000)
	devc->m_DACVolume[2] = 0x5f; /* DFS=double-speed */
  else
	devc->m_DACVolume[2] = 0x6f; /* DFS=quad-speed */

  OUTB (devc->osdev, 0x80, devc->mt_base + 0x05);   /* RESET */
  OUTB (devc->osdev, 0x00, devc->mt_base + 0x05);
  OUTB (devc->osdev, 0x10, devc->mt_base + 0x01);

    /* Restore ak4358 regs and set DFS */
  for (i = 0; i <= 0x0c; i++)
    i2c_write (devc, AK4358_ADDRESS, i, devc->m_DACVolume[i]);
    /* Restore ak4114 */
  ak4114_init (devc);
}

static int
julia_audio_ioctl (envy24ht_devc * devc, envy24ht_portc * portc, int cmd,
		   int *arg)
{
  int left, right, value;

  switch (cmd)
    {
    case SNDCTL_DSP_GETPLAYVOL:
      if (portc != &devc->play_portc[0])
	return -EINVAL;
      left = (devc->gains[0] & 0xff) * 100 / 0x7f;
      right = ((devc->gains[5] >> 8) & 0xff) * 100 / 0x7f;
      return *arg = (left | (right << 8));
      break;

    case SNDCTL_DSP_SETPLAYVOL:
      if (portc != &devc->play_portc[0])
	return -EINVAL;
      value = *arg;
      left = value & 0xff;
      right = (value >> 8) & 0xff;

      left = (left * 0x7f) / 100;
      right = (right * 0x7f) / 100;
      left = set_dac (devc, 0x04, left);
      right = set_dac (devc, 0x05, right);
      devc->gains[0] = left | (right << 8);
      mixer_devs[devc->mixer_dev]->modify_counter++;
      return 0;
      break;
    }
  return -EINVAL;
}

static int
julia_set_control (int dev, int ctrl, unsigned int cmd, int value)
{
  envy24ht_devc *devc = mixer_devs[dev]->hw_devc;

  if (cmd == SNDCTL_MIX_READ)
    {
        if (ctrl < 0 || ctrl > 3)
	    return -EINVAL;
    
	return devc->monitor[ctrl];
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
    	if (ctrl < 0 || ctrl > 3)
	    return -EINVAL;
	
	value = !!value;
	julia_Monitor (devc, value, ctrl);
	return devc->monitor[ctrl];
    }

  return -EINVAL;
}

static int
julia_set_ak4358 (int dev, int ctrl, unsigned int cmd, int value)
{
  envy24ht_devc *devc = mixer_devs[dev]->hw_devc;

  if (cmd == SNDCTL_MIX_READ)
    {
      if (ctrl < 0 || ctrl > 4)
	return -EIO;

      return devc->gains[ctrl];
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
      int left, right;

      left = value & 0xff;
      right = (value >> 8) & 0xff;

      switch (ctrl)
	{
	case 0:		/* PCM */
	  left = set_dac (devc, 0x04, left);
	  right = set_dac (devc, 0x05, right);
	  break;
	case 1:		/* LINEIN */
	  left = set_dac (devc, 0x06, left);
	  right = set_dac (devc, 0x07, right);
	  break;
	case 2:		/* SPDIFOUT */
	  left = set_dac (devc, 0x08, left);
	  right = set_dac (devc, 0x09, right);
	  break;
	case 3:		/* SPDIFIN */
	  left = set_dac (devc, 0x0B, left);
	  right = set_dac (devc, 0x0C, right);
	  break;

	default:
	  return -EINVAL;
	}

      value = left | (right << 8);
      return devc->gains[ctrl] = value;
    }

  return -EINVAL;
}

 /*ARGSUSED*/ static int
julia_mixer_init (envy24ht_devc * devc, int dev, int g)
{
  int group = g;
  int err, monitor;

  if ((group = mixer_ext_create_group (dev, g, "VOL")) < 0)
    return group;

  if ((err = mixer_ext_create_control (dev, group,
				       0, julia_set_control,
				       MIXT_ONOFF,
				       "ENVY24_MUTE", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group,
				       0, julia_set_ak4358,
				       MIXT_STEREOSLIDER,
				       "ENVY24_PCM", 0x7f,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group,
				       1, julia_set_ak4358,
				       MIXT_STEREOSLIDER,
				       "ENVY24_LINEIN", 0x7f,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group,
				       2, julia_set_ak4358,
				       MIXT_STEREOSLIDER,
				       "ENVY24_SPDIFOUT", 0x7f,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group,
				       3, julia_set_ak4358,
				       MIXT_STEREOSLIDER,
				       "ENVY24_SPDIFIN", 0x7f,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((monitor = mixer_ext_create_group (dev, g, "MONITOR")) < 0)
    return monitor;

  if ((err = mixer_ext_create_control (dev, monitor,
				       1, julia_set_control,
				       MIXT_ONOFF,
				       "ENVY24_LINEIN", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, monitor,
				       2, julia_set_control,
				       MIXT_ONOFF,
				       "ENVY24_SPDIFOUT", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, monitor,
				       3, julia_set_control,
				       MIXT_ONOFF,
				       "ENVY24_SPDIFIN", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  return 0;
}

static void
julia_card_init (envy24ht_devc * devc)
{
  ak4114_init (devc);
/*
 * AK4358 DAC initialization
 */
  i2c_write (devc, AK4358_ADDRESS, 2, 0x00);
  i2c_write (devc, AK4358_ADDRESS, 2, 0x4E);
  i2c_write (devc, AK4358_ADDRESS, 0, 0x06);
  i2c_write (devc, AK4358_ADDRESS, 1, 0x02);
  i2c_write (devc, AK4358_ADDRESS, 3, 0x01);

  set_dac (devc, 0x04, 0x7f);
  set_dac (devc, 0x05, 0x7f);
  set_dac (devc, 0x06, 0x7f);
  set_dac (devc, 0x07, 0x7f);
  set_dac (devc, 0x08, 0x7f);
  set_dac (devc, 0x09, 0x7f);
  set_dac (devc, 0x0b, 0x7f);
  set_dac (devc, 0x0c, 0x7f);

  i2c_write (devc, AK4358_ADDRESS, 0xA, 0x00);
  i2c_write (devc, AK4358_ADDRESS, 0xD, 0x00);
  i2c_write (devc, AK4358_ADDRESS, 0xE, 0x00);
  i2c_write (devc, AK4358_ADDRESS, 0xF, 0x00);
  i2c_write (devc, AK4358_ADDRESS, 2, 0x4F);

  julia_Monitor (devc, 0, 0); /* Unmute */
}

envy24ht_auxdrv_t envy24ht_julia_auxdrv = {
  julia_card_init,
  julia_mixer_init,
  julia_set_rate,
  NULL,
  NULL,
  NULL,
  julia_audio_ioctl
};
