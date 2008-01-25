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

#define _delay()	{}
#define BIT(x) (1<<(x))
#define BIT3 BIT(3)

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
}

static unsigned int
GpioReadAll (envy24ht_devc * devc)
{
  return INW (devc->osdev, devc->ccs_base + 0x14);
}

static void
GPIOWriteAll (envy24ht_devc * devc, unsigned int data)
{
  OUTW (devc->osdev, 0xffff, devc->ccs_base + 0x18);	/* GPIO direction */
  OUTW (devc->osdev, 0x0000, devc->ccs_base + 0x16);	/* GPIO write mask */

  OUTW (devc->osdev, data, devc->ccs_base + 0x14);
}

static void
GPIOWrite (envy24ht_devc * devc, int pos, int bit)
{
  int data = GpioReadAll (devc);

  bit = (bit != 0);

  data &= ~(1 << pos);
  data |= (bit << pos);

  GPIOWriteAll (devc, data);
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
static void
julia_Set_48K_Mode (envy24ht_devc * devc)
/*
*****************************************************************************
* Sets Chip and Envy24 for 8kHz-48kHz sample rates.
****************************************************************************/
{
  OUTB (devc->osdev, INB (devc->osdev, devc->mt_base + 2) & ~BIT3,
	devc->mt_base + 2);
  i2c_write (devc, AK4358_ADDRESS, 2, 0x4E);
  i2c_write (devc, AK4358_ADDRESS, 2, 0x4F);
}

static void
julia_Set_96K_Mode (envy24ht_devc * devc)
/*
*****************************************************************************
* Sets CODEC and Envy24 for 60kHz-96kHz sample rates.
****************************************************************************/
{
/* ICE MCLK = 256x. */
  OUTB (devc->osdev, INB (devc->osdev, devc->mt_base + 2) & ~BIT3,
	devc->mt_base + 2);
/* DFS=double-speed, RESET. */
  i2c_write (devc, AK4358_ADDRESS, 2, 0x5E);
/* DFS=double-speed, NORMAL OPERATION. */
  i2c_write (devc, AK4358_ADDRESS, 2, 0x5F);

}

static void
julia_Set_192K_Mode (envy24ht_devc * devc)
/*
*****************************************************************************
* Sets CODEC and Envy24 for 120kHz-192kHz sample rate.
****************************************************************************/
{
/* ICE MCLK = 128x. */
  OUTB (devc->osdev, INB (devc->osdev, devc->mt_base + 2) | BIT3,
	devc->mt_base + 2);
  _delay ();
/*----- SET THE D/A. */
/* DFS=quad-speed, RESET. */
  i2c_write (devc, AK4358_ADDRESS, 2, 0x6E);
  _delay ();
/* DFS=quad-speed, NORMAL OPERATION. */
  i2c_write (devc, AK4358_ADDRESS, 2, 0x6F);

  /* SPDIF */
  i2c_write (devc, AK4114_ADDRESS, 0x00, 0x0d);
  i2c_write (devc, AK4114_ADDRESS, 0x00, 0x0f);

}

static void
julia_set_rate (envy24ht_devc * devc)
{
  int tmp;

  tmp = INB (devc->osdev, devc->mt_base + 0x02);
  if (devc->speed <= 48000)
    {
      julia_Set_48K_Mode (devc);
      OUTB (devc->osdev, tmp & ~BIT (3), devc->mt_base + 0x02);
      return;
    }

  if (devc->speed <= 96000)
    {
      julia_Set_96K_Mode (devc);

      return;
    }

  julia_Set_192K_Mode (devc);
  OUTB (devc->osdev, tmp | BIT (3), devc->mt_base + 0x02);
}

static void
julia_card_init (envy24ht_devc * devc)
{

  cmn_err (CE_CONT, "julia_card_init()\n");

  OUTW (devc->osdev, 0xffff, devc->ccs_base + 0x18);	/* GPIO direction */
  OUTW (devc->osdev, 0x0000, devc->ccs_base + 0x16);	/* GPIO write mask */
  OUTW (devc->osdev, 0xffff, devc->ccs_base + 0x14);	/* Initial bit state */

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
  i2c_write (devc, AK4114_ADDRESS, 0x10, 0x00);

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

  GPIOWrite (devc, 8, 0);
  GPIOWrite (devc, 9, 0);
  GPIOWrite (devc, 10, 0);
}

 /*ARGSUSED*/ static int
julia_mixer_init (envy24ht_devc * devc, int dev, int group)
{
  cmn_err (CE_CONT, "julia_mixer_init()\n");
  return 0;
}

envy24ht_auxdrv_t envy24ht_julia_auxdrv = {
  julia_card_init,
  julia_mixer_init,
  julia_set_rate
};
