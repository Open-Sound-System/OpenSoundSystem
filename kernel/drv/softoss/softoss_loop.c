/*
 * Purpose: Low level mixing routines for softoss
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000. All rights reserved.

#include "softoss_cfg.h"

#define MY_BUFFSIZE (64*1024)

#include "softoss.h"

/*ARGSUSED*/
static int
softloop_set_rate (int dev, int arg)
{
  softoss_devc *devc = audio_engines[dev]->devc;
  audio_engines[dev]->fixed_rate = devc->speed;
  audio_engines[dev]->min_rate = devc->speed;
  audio_engines[dev]->max_rate = devc->speed;
  return devc->speed;
}

/*ARGSUSED*/
static short
softloop_set_channels (int dev, short arg)
{
  return 2;
}

/*ARGSUSED*/
static unsigned int
softloop_set_format (int dev, unsigned int arg)
{
  softoss_devc *devc = audio_engines[dev]->devc;

  return devc->hw_format;
}

/*ARGSUSED*/
static int
softloop_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  return -EINVAL;
}

static void softloop_trigger (int dev, int state);

static void
softloop_reset (int dev)
{
  softloop_trigger (dev, 0);
}

void
softloop_callback (softoss_devc * devc)
{
  dmap_t *dmap;
  unsigned char *buf, *tbuf;
  int i, len;
  int dev = devc->masterdev;

  dmap = audio_engines[dev]->dmap_out;

  buf = &dmap->dmabuf[dmap_get_qtail (dmap) * dmap->fragment_size];
  len = dmap->fragment_size;

  for (i = 0; i < devc->nr_loops; i++)
    {
      softloop_portc *portc = &devc->loop_portc[i];
      dmap = audio_engines[portc->audio_dev]->dmap_in;

      if (!portc->is_triggered)
	continue;

      if (dmap->fragment_size != len)
	{
	  dmap->fragment_size = len;
	  dmap->nfrags = dmap->buffsize / dmap->fragment_size;
	  dmap->bytes_in_use = dmap->fragment_size * dmap->nfrags;
	}

      tbuf = &dmap->dmabuf[dmap_get_qtail (dmap) * dmap->fragment_size];

      memcpy (tbuf, buf, len);

      oss_audio_inputintr (portc->audio_dev, 0);
    }
}

/*ARGSUSED*/
static int
softloop_open (int dev, int mode, int open_flags)
{
  softloop_portc *portc = audio_engines[dev]->portc;
  softoss_devc *devc = audio_engines[dev]->devc;
  oss_native_word flags;
  int err;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if ((err = softoss_open_audiodev ()) < 0)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return err;
    }

  if (portc->is_opened)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  portc->is_opened = 1;
  portc->is_prepared = 0;
  portc->is_triggered = 0;
  portc->speed = 8000;
  portc->channels = 1;
  portc->fmt = devc->hw_format;
  audio_engines[dev]->oformat_mask = devc->hw_format;
  audio_engines[dev]->iformat_mask = devc->hw_format;

  devc->nr_opened_audio_engines++;
  audio_engines[dev]->min_rate = devc->speed;
  audio_engines[dev]->max_rate = devc->speed;
  audio_engines[dev]->rate_source =
    audio_engines[devc->masterdev]->rate_source;
  audio_engines[dev]->min_block = audio_engines[dev]->max_block =
    audio_engines[devc->masterdev]->dmap_out->fragment_size;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  softoss_start_engine (devc);
  return 0;
}

/*ARGSUSED*/
static void
softloop_close (int dev, int mode)
{
  softloop_portc *portc = audio_engines[dev]->portc;
  softoss_devc *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->is_opened = 0;
  devc->nr_opened_audio_engines--;
  if (devc->nr_opened_audio_engines == 0 && devc->softoss_opened == 0)
    softoss_close_audiodev ();
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static void
softloop_output_block (int dev, oss_native_word buf, int count, int fragsize,
		       int intrflag)
{
}

/*ARGSUSED*/
static void
softloop_start_input (int dev, oss_native_word buf, int count, int fragsize,
		      int intrflag)
{
}

static void
softloop_trigger (int dev, int state)
{
  softloop_portc *portc = audio_engines[dev]->portc;

  if (state & PCM_ENABLE_INPUT)
    portc->is_triggered = 1;
  else
    portc->is_triggered = 0;
}

/*ARGSUSED*/
static int
softloop_prepare_for_input (int dev, int bsize, int bcount)
{
  return 0;
}

/*ARGSUSED*/
static int
softloop_prepare_for_output (int dev, int bsize, int bcount)
{
  return -EIO;
}

/*ARGSUSED*/
static int
softloop_alloc_buffer (int dev, dmap_t * dmap, int direction)
{
  if (dmap->dmabuf != NULL)
    return 0;
  dmap->dmabuf_phys = 0;	/* Not mmap() capable */
  dmap->dmabuf = KERNEL_MALLOC (MY_BUFFSIZE);
  if (dmap->dmabuf == NULL)
    return -ENOSPC;
  dmap->buffsize = MY_BUFFSIZE;

  return 0;
}

/*ARGSUSED*/
static int
softloop_free_buffer (int dev, dmap_t * dmap, int direction)
{
  if (dmap->dmabuf == NULL)
    return 0;
  KERNEL_FREE (dmap->dmabuf);

  dmap->dmabuf = NULL;
  return 0;
}

#if 0
static int
softloop_get_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
}
#endif

static int
softloop_check_input (int dev)
{
  softoss_devc *devc = audio_engines[dev]->devc;
  dmap_p dmap = audio_engines[dev]->dmap_in;

  if (dmap == NULL)
    return -EIO;

  if (devc->engine_state != ES_STOPPED)
    {
      return 0;
    }

  memset (dmap->dmabuf, 0, dmap->bytes_in_use);
  oss_audio_inputintr (dev, 0);

  return 0;
}

static audiodrv_t softloop_driver = {
  softloop_open,
  softloop_close,
  softloop_output_block,
  softloop_start_input,
  softloop_ioctl,
  softloop_prepare_for_input,
  softloop_prepare_for_output,
  softloop_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  softloop_trigger,
  softloop_set_rate,
  softloop_set_format,
  softloop_set_channels,
  NULL,
  NULL,
  softloop_check_input,
  NULL,
  softloop_alloc_buffer,
  softloop_free_buffer,
  NULL,
  NULL,
  NULL				/* softloop_get_buffer_pointer */
};

void
softoss_install_loop (softoss_devc * devc)
{
  int n, adev, opts;
  char tmp[64];

  opts =
    ADEV_STEREOONLY | ADEV_16BITONLY | ADEV_VIRTUAL | ADEV_NOOUTPUT |
    ADEV_FIXEDRATE;

  devc->nr_loops = 0;

  if (softoss_loopdevs <= 0)
    {
      softoss_loopdevs = 0;
      return;
    }

  if (softoss_loopdevs > MAX_SOFTLOOP_DEV)
    softoss_loopdevs = MAX_SOFTLOOP_DEV;
  create_new_card ("loop", "SoftOSS/Virtual mixer loopback");

  for (n = 0; n < softoss_loopdevs; n++)
    {
      softloop_portc *portc;

      if (n > 0)
	opts |= ADEV_SHADOW;

      strcpy (tmp, "Virtual Mixer Loopback Record");

      if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
					devc->osdev,
					devc->master_osdev,
					tmp,
					&softloop_driver,
					sizeof (audiodrv_t),
					opts, AFMT_S16_LE, devc, -1)) < 0)
	{
	  return;
	}

      portc = &devc->loop_portc[n];

      audio_engines[adev]->portc = portc;
      audio_engines[adev]->min_rate = 5000;
      audio_engines[adev]->max_rate = 48000;
      audio_engines[adev]->caps |= PCM_CAP_FREERATE;

      portc->audio_dev = adev;
      portc->is_opened = 0;
      portc->is_prepared = 0;
      portc->is_triggered = 0;
      portc->speed = 8000;
      portc->channels = 1;
      portc->fmt = AFMT_S16_LE;

      devc->nr_loops = n + 1;
    }
}
