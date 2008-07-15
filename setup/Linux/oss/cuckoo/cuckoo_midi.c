/*
 * This software module makes it possible to use Open Sound System for Linux
 * (the _professional_ version) as a low level driver source for ALSA.
 *
 * Copyright (C) 2004 Hannu Savolainen (hannu@voimakentta.net).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/*
 * !!!!!!!!!!!!!!!!!!!! Important  !!!!!!!!!!!!!!!!!!
 *
 * If this file doesn't compile, you must not try to resolve the problem
 * without perfect understanding of internals of Linux kernel, ALSA and
 * Open Sound System.
 *
 * Instead you need to check that you are using the version of this file
 * that matches the versions of ALSA, OSS and Linux you are currently using.
 */

#include "cuckoo.h"
#include <sound/rawmidi.h>

static snd_rawmidi_t *rmidis[256] = { NULL };

static int
cuckoo_uart_input_open (snd_rawmidi_substream_t * substream)
{
#if 0
  mpu401_t *mpu;
  int err;

  mpu =
    snd_magic_cast (mpu401_t, substream->rmidi->private_data, return -ENXIO);
  if (mpu->open_input && (err = mpu->open_input (mpu)) < 0)
    return err;
  if (!test_bit (MPU401_MODE_BIT_OUTPUT, &mpu->mode))
    {
      cuckoo_uart_cmd (mpu, MPU401_RESET, 1);
      cuckoo_uart_cmd (mpu, MPU401_ENTER_UART, 1);
    }
  mpu->substream_input = substream;
  atomic_set (&mpu->rx_loop, 1);
  set_bit (MPU401_MODE_BIT_INPUT, &mpu->mode);
#endif
  return 0;
}

static int
cuckoo_uart_output_open (snd_rawmidi_substream_t * substream)
{
  int dev;

  dev = (int) substream->rmidi->private_data;

  printk ("Output open %d\n", dev);

  return -EIO;
#if 0
  mpu401_t *mpu;
  int err;
  if (mpu->open_output && (err = mpu->open_output (mpu)) < 0)
    return err;
  if (!test_bit (MPU401_MODE_BIT_INPUT, &mpu->mode))
    {
      cuckoo_uart_cmd (mpu, MPU401_RESET, 1);
      cuckoo_uart_cmd (mpu, MPU401_ENTER_UART, 1);
    }
  mpu->substream_output = substream;
  atomic_set (&mpu->tx_loop, 1);
  set_bit (MPU401_MODE_BIT_OUTPUT, &mpu->mode);
#endif
  return 0;
}

static int
cuckoo_uart_input_close (snd_rawmidi_substream_t * substream)
{
#if 0
  mpu401_t *mpu;

  mpu =
    snd_magic_cast (mpu401_t, substream->rmidi->private_data, return -ENXIO);
  clear_bit (MPU401_MODE_BIT_INPUT, &mpu->mode);
  mpu->substream_input = NULL;
  if (!test_bit (MPU401_MODE_BIT_OUTPUT, &mpu->mode))
    cuckoo_uart_cmd (mpu, MPU401_RESET, 0);
  if (mpu->close_input)
    mpu->close_input (mpu);
#endif
  return 0;
}

static int
cuckoo_uart_output_close (snd_rawmidi_substream_t * substream)
{
#if 0
  mpu401_t *mpu;

  mpu =
    snd_magic_cast (mpu401_t, substream->rmidi->private_data, return -ENXIO);
  clear_bit (MPU401_MODE_BIT_OUTPUT, &mpu->mode);
  mpu->substream_output = NULL;
  if (!test_bit (MPU401_MODE_BIT_INPUT, &mpu->mode))
    cuckoo_uart_cmd (mpu, MPU401_RESET, 0);
  if (mpu->close_output)
    mpu->close_output (mpu);
#endif
  return 0;
}

static void
cuckoo_uart_input_trigger (snd_rawmidi_substream_t * substream, int up)
{
#if 0
  unsigned long flags;
  mpu401_t *mpu;
  int max = 64;

  mpu = snd_magic_cast (mpu401_t, substream->rmidi->private_data, return);
  if (up)
    {
      if (!test_and_set_bit (MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode))
	{
	  /* first time - flush FIFO */
	  while (max-- > 0)
	    mpu->read (mpu, MPU401D (mpu));
	  if (mpu->irq < 0)
	    cuckoo_uart_add_timer (mpu, 1);
	}

      /* read data in advance */
      /* prevent double enter via rawmidi->event callback */
      if (atomic_dec_and_test (&mpu->rx_loop))
	{
	  local_irq_save (flags);
	  if (spin_trylock (&mpu->input_lock))
	    {
	      cuckoo_uart_input_read (mpu);
	      spin_unlock (&mpu->input_lock);
	    }
	  local_irq_restore (flags);
	}
      atomic_inc (&mpu->rx_loop);
    }
  else
    {
      if (mpu->irq < 0)
	cuckoo_uart_remove_timer (mpu, 1);
      clear_bit (MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode);
    }
#endif
}

#if 0
static void
cuckoo_uart_input_read (void *mpu)
{
  int max = 128;
  unsigned char byte;

  while (max-- > 0)
    {
      if (cuckoo_input_avail (mpu))
	{
	  byte = mpu->read (mpu, MPU401D (mpu));
	  if (test_bit (MPU401_MODE_BIT_INPUT_TRIGGER, &mpu->mode))
	    snd_rawmidi_receive (mpu->substream_input, &byte, 1);
	}
      else
	{
	  break;		/* input not available */
	}
    }
}
#endif

#if 0
static void
cuckoo_uart_output_write (void *mpu)
{
  unsigned char byte;
  int max = 256, timeout;

  do
    {
      if (snd_rawmidi_transmit_peek (mpu->substream_output, &byte, 1) == 1)
	{
	  for (timeout = 100; timeout > 0; timeout--)
	    {
	      if (cuckoo_output_ready (mpu))
		{
		  mpu->write (mpu, byte, MPU401D (mpu));
		  snd_rawmidi_transmit_ack (mpu->substream_output, 1);
		  break;
		}
	    }
	  if (timeout == 0)
	    break;		/* Tx FIFO full - try again later */
	}
      else
	{
	  cuckoo_uart_remove_timer (mpu, 0);
	  break;		/* no other data - leave the tx loop */
	}
    }
  while (--max > 0);
}
#endif

static void
cuckoo_uart_output_trigger (snd_rawmidi_substream_t * substream, int up)
{
  int dev;

  dev = (int) substream->rmidi->private_data;
  printk ("Output trigger %d\n", dev);
#if 0
  if (up)
    {
      set_bit (MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode);

      /* try to add the timer at each output trigger,
       * since the output timer might have been removed in
       * cuckoo_uart_output_write().
       */
      cuckoo_uart_add_timer (mpu, 0);

      /* output pending data */
      /* prevent double enter via rawmidi->event callback */
      if (atomic_dec_and_test (&mpu->tx_loop))
	{
	  local_irq_save (flags);
	  if (spin_trylock (&mpu->output_lock))
	    {
	      cuckoo_uart_output_write (mpu);
	      spin_unlock (&mpu->output_lock);
	    }
	  local_irq_restore (flags);
	}
      atomic_inc (&mpu->tx_loop);
    }
  else
    {
      cuckoo_uart_remove_timer (mpu, 0);
      clear_bit (MPU401_MODE_BIT_OUTPUT_TRIGGER, &mpu->mode);
    }
#endif
}

static snd_rawmidi_ops_t cuckoo_uart_output = {
  .open = cuckoo_uart_output_open,
  .close = cuckoo_uart_output_close,
  .trigger = cuckoo_uart_output_trigger,
};

static snd_rawmidi_ops_t cuckoo_uart_input = {
  .open = cuckoo_uart_input_open,
  .close = cuckoo_uart_input_close,
  .trigger = cuckoo_uart_input_trigger,
};

#warning We should really find a way to include midi_core.h here instead of copying its structures...

struct midi_input_info
{				/* MIDI input scanner variables */
#define MI_MAX	32
  int m_busy;
  unsigned char m_buf[MI_MAX];
  unsigned char m_prev_status;	/* For running status */
  int m_ptr;
#define MST_INIT			0
#define MST_DATA			1
#define MST_SYSEX			2
  int m_state;
  int m_left;
  int m_f1_flag;		/* MTC Quarter frame message flag */
};

typedef struct _midi_operations
{
  int dev;			/* MIDI device number */
  char name[64];
  void *d;
  int caps;
  int open_mode;		/* OPEN_READ | OPEN_WRITE */

  int working_mode;		/* See SNDCTL_MIDI_SETMODE */
  int timebase;
  int tempo;
  struct synth_operations *converter;
  struct midi_input_info in_info;
  struct coproc_operations *coproc;
  void *devc;
  void *os_id;			/* The device ID (dip) given by the system. */
  int enabled;
  int unloaded;
  int is_killed;
  oss_device_t *osdev;
  oss_device_t *master_osdev;	/* osdev struct of the master device (for virtual drivers) */
  int card_number;
  int port_number;
  oss_devnode_t devnode;
  int real_dev;
  char handle[32];
  unsigned long flags;
#define MFLAG_NOSEQUENCER		0x00000001	/* Device not to be opened by the sequencer driver */
#define MFLAG_VIRTUAL			0x00000002
#define MFLAG_SELFTIMING		0x00000004	/* Generates MTC timing itself */
#define MFLAG_CLIENT			0x00000008	/* Client side virtual device */
#define MFLAG_SERVER			0x00000010	/* Client side virtual device */
#define MFLAG_INTERNAL			0x00000020	/* Internal device */
#define MFLAG_EXTERNAL			0x00000040	/* External device */

#define MFLAG_INPUT			0x00000080
#define MFLAG_OUTPUT			0x00000100

#define MFLAG_OUTINTR			0x00000200	/* Supports output interrupts - no polling needed */
#define MFLAG_MTC			0x00000400	/* Device is MTC/SMPTE capable */
#define MFLAG_QUIET			0x00000800	/* No automatic messages */

  void (*input_callback) (int dev, unsigned char midich);
  void (*event_input) (int dev, unsigned char *data, int len);

#ifndef CONFIGURE_C
  oss_mutex_t mutex;
  oss_wait_queue_t *in_wq, *out_wq;
  timeout_id_t out_timeout;
#endif
  pid_t pid;
  int magic;
  char cmd[16];

  int prech_timeout;		/* Wait time for the first input byte */

/*
 * MTC generator
 */

  int mtc_timebase;
  int mtc_phase;		/* Which part should be sent next */
  long long mtc_t0;
  int mtc_prev_t, mtc_codetype, mtc_current;
  timeout_id_t mtc_timeout_id;

/*
 * Event queues
 */

  struct midi_queue_t *in_queue, *out_queue;

/*
 * Timer
 */
  int timer_driver;
  int timer_dev;
  int latency;			/* In usecs, -1=unknown */
  int is_timing_master;
} mididev_t, *mididev_p;
extern int num_mididevs;
extern mididev_p *midi_devs;

int
install_midiport_instances (cuckoo_t * chip, int cardno)
{
  int dev, devix = 0;

  for (dev = 0; dev < num_mididevs; dev++)
    if (midi_devs[dev]->card_number == cardno)
      {
	mididev_p mididev = midi_devs[dev];
	snd_rawmidi_t *rmidi;
	int err;

//printk("Midi device %s\n", mididev->info.name);

	if ((err = snd_rawmidi_new (chip->card, mididev->name, devix,
				    1, 1, &rmidi)) < 0)
	  {
	    printk ("cuckoo: Failed to register rawmidi device, err=%d\n",
		    err);
	    return 0;
	  }

	rmidi->private_data = (void *) dev;
	strcpy (rmidi->name, mididev->name);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	  SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	snd_rawmidi_set_ops (rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			     &cuckoo_uart_output);
	snd_rawmidi_set_ops (rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			     &cuckoo_uart_input);

	devix++;
	rmidis[dev] = rmidi;
      }				// dev

  return 0;
}
