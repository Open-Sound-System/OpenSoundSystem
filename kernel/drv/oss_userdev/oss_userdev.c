/*
 * Purpose: Kernel space support module for user land audio/mixer drivers
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

#include "oss_userdev_cfg.h"

#define MAX_RATE 	192000
#define MAX_CHANNELS	64
#define SUPPORTED_FORMATS	(AFMT_S16_NE|AFMT_S32_NE)

typedef struct _userdev_devc_t userdev_devc_t;
typedef struct _userdev_portc_t userdev_portc_t;

struct _userdev_portc_t
{
  userdev_devc_t *devc;
  userdev_portc_t *peer;
  int audio_dev;
  int open_mode;
  int port_type;
#define PT_CLIENT	1
#define PT_SERVER	2

  /* State variables */
  int input_triggered, output_triggered;
  oss_wait_queue_t *wq;
};

struct _userdev_devc_t
{
  oss_device_t *osdev;
  oss_mutex_t mutex;

  userdev_devc_t *next_instance;

  int rate;
  int channels;
  unsigned int fmt, fmt_bytes;
  timeout_id_t timeout_id;

  userdev_portc_t client_portc;
  userdev_portc_t server_portc;
};

static oss_device_t *userdev_osdev = NULL;

/*
 * Global device lists and the mutex that protects them.
 */
static oss_mutex_t userdev_global_mutex;
static userdev_devc_t *active_device_list = NULL;

static void
transfer_audio (userdev_portc_t * server_portc, dmap_t * dmap_from,
		dmap_t * dmap_to)
{
  int l = dmap_from->fragment_size;
  unsigned char *fromp, *top;

  if (dmap_to->fragment_size != l)
    {
      cmn_err (CE_WARN, "Fragment size mismatch (%d != %d)\n",
	       dmap_to->fragment_size, l);

      /* Perform emergency stop */
      server_portc->input_triggered = 0;
      server_portc->output_triggered = 0;
      server_portc->peer->input_triggered = 0;
      server_portc->peer->output_triggered = 0;
      return;
    }

  fromp =
    dmap_from->dmabuf + (dmap_from->byte_counter % dmap_from->bytes_in_use);
  top = dmap_to->dmabuf + (dmap_to->byte_counter % dmap_to->bytes_in_use);

  memcpy (top, fromp, l);

}

static void
handle_input (userdev_portc_t * server_portc)
{
  userdev_portc_t *client_portc = server_portc->peer;

  if (client_portc->output_triggered)
    {
      transfer_audio (server_portc,
		      audio_engines[client_portc->audio_dev]->dmap_out,
		      audio_engines[server_portc->audio_dev]->dmap_in);
      oss_audio_outputintr (client_portc->audio_dev, 0);
    }

  oss_audio_inputintr (server_portc->audio_dev, 0);
}

static void
handle_output (userdev_portc_t * server_portc)
{
  userdev_portc_t *client_portc = server_portc->peer;

  if (client_portc->input_triggered)
    {
      transfer_audio (server_portc,
		      audio_engines[server_portc->audio_dev]->dmap_out,
		      audio_engines[client_portc->audio_dev]->dmap_in);
      oss_audio_inputintr (client_portc->audio_dev, 0);
    }

  oss_audio_outputintr (server_portc->audio_dev, 0);
}

static void
userdev_cb (void *pc)
{
/*
 * This timer callback routine will get called 100 times/second. It handles
 * movement of audio data between the client and server sides.
 */
  userdev_portc_t *server_portc = pc;
  userdev_devc_t *devc = server_portc->devc;
  int tmout = OSS_HZ / 100;

  if (tmout < 1)
    tmout = 1;

  devc->timeout_id = 0;	/* No longer valid */

  if (server_portc->input_triggered)
    handle_input (server_portc);

  if (server_portc->output_triggered)
    handle_output (server_portc);

  /* Retrigger timer callback */
  if (server_portc->input_triggered || server_portc->output_triggered)
    devc->timeout_id = timeout (userdev_cb, server_portc, tmout);
}

static int
userdev_check_input (int dev)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  if (!portc->peer->output_triggered)
    {
      return -ECONNRESET;
    }
  return 0;
}

static int
userdev_check_output (int dev)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;

  if (!portc->peer->input_triggered)
    {
      return -ECONNRESET;
    }

  if (portc->peer->open_mode == 0)
    return -EIO;
  return 0;
}

static void
setup_sample_format (userdev_portc_t * portc)
{
  adev_t *adev;
  userdev_devc_t *devc = portc->devc;
  int fragsize, frame_size;

  frame_size = devc->channels * devc->fmt_bytes;
  if (frame_size == 0)
    frame_size = 4;

  fragsize = (devc->rate * frame_size) / 100;	/* Number of bytes/fragment (100Hz) */
  devc->rate = fragsize * 100 / frame_size;

/* Setup the server side */
  adev = audio_engines[portc->audio_dev];
  adev->min_block = adev->max_block = fragsize;

/* Setup the client side */
  adev = audio_engines[portc->peer->audio_dev];
  adev->min_block = adev->max_block = fragsize;

  adev->max_rate = adev->min_rate = devc->rate;
  adev->iformat_mask = devc->fmt;
  adev->oformat_mask = devc->fmt;
  adev->xformat_mask = devc->fmt;
  adev->min_channels = adev->max_channels = devc->channels;
}

static int
userdev_server_set_rate (int dev, int arg)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  if (arg == 0)
    return devc->rate;

  if (portc->peer->input_triggered || portc->peer->output_triggered)
    return devc->rate;

  if (arg < 5000)
    arg = 5000;
  if (arg > MAX_RATE)
    arg = MAX_RATE;

  /* Force the sample rate to be multiple of 100 */
  arg = (arg / 100) * 100;

  devc->rate = arg;

  setup_sample_format (portc);

  return devc->rate = arg;
}

/*ARGSUSED*/
static int
userdev_client_set_rate (int dev, int arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;

  return devc->rate;
}

static short
userdev_server_set_channels (int dev, short arg)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  if (arg == 0)
    return devc->channels;

  if (portc->peer->input_triggered || portc->peer->output_triggered)
    return devc->channels;

  if (arg < 1)
    arg = 1;
  if (arg > MAX_CHANNELS)
    arg = MAX_CHANNELS;

  devc->channels = arg;

  setup_sample_format (portc);

  return devc->channels;
}

/*ARGSUSED*/
static short
userdev_client_set_channels (int dev, short arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;

  return devc->channels;	/* Server side channels */
}

static unsigned int
userdev_server_set_format (int dev, unsigned int arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;
  userdev_portc_t *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return devc->fmt;

  if (portc->peer->input_triggered || portc->peer->output_triggered)
    return devc->fmt;

  switch (arg)
    {
    case AFMT_S16_NE:
      devc->fmt_bytes = 2;
      break;

    case AFMT_S32_NE:
      devc->fmt_bytes = 4;
      break;

    default:			/* Unsupported format */
      arg = AFMT_S16_NE;
      devc->fmt_bytes = 2;

    }

  devc->fmt = arg;

  setup_sample_format (portc);

  return devc->fmt;
}

/*ARGSUSED*/
static unsigned int
userdev_client_set_format (int dev, unsigned int arg)
{
  userdev_devc_t *devc = audio_engines[dev]->devc;

  return devc->fmt;	/* Server side sample format */
}

static void userdev_trigger (int dev, int state);

static void
userdev_reset (int dev)
{
  userdev_trigger (dev, 0);
}

/*ARGSUSED*/
static int
userdev_server_open (int dev, int mode, int open_flags)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
  adev_t *adev;

  if ((mode & OPEN_READ) && (mode & OPEN_WRITE))
    return -EACCES;

  if (portc == NULL || portc->peer == NULL)
    return -ENXIO;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (portc->open_mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  portc->open_mode = mode;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

/*
 * Update client device flags
 */
  adev = audio_engines[portc->peer->audio_dev];
  adev->flags &= ~(ADEV_NOINPUT | ADEV_NOOUTPUT);
  if (!(mode & OPEN_READ))
    adev->flags |= ADEV_NOOUTPUT;
  if (!(mode & OPEN_WRITE))
    adev->flags |= ADEV_NOINPUT;
  adev->enabled = 1;		/* Enable client side */

  return 0;
}

/*ARGSUSED*/
static int
userdev_client_open (int dev, int mode, int open_flags)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  if (portc == NULL || portc->peer == NULL)
    return -ENXIO;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (portc->open_mode)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }

  portc->open_mode = mode;

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

/*ARGSUSED*/
static void
userdev_server_close (int dev, int mode)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  audio_engines[portc->peer->audio_dev]->enabled = 0;	/* Disable client side */
  portc->open_mode = 0;

  /* Stop the client side */
  portc->peer->input_triggered = 0;
  portc->peer->output_triggered = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static void
userdev_client_close (int dev, int mode)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->open_mode = 0;

  /* Stop the server side */
  portc->peer->input_triggered = 0;
  portc->peer->output_triggered = 0;


  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static int
userdev_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  switch (cmd)
    {
    case SNDCTL_GETLABEL:
      {
	/*
	 * Return an empty string so that this feature can be tested.
	 * Complete functionality is to be implemented later.
	 */
	oss_label_t *s = (oss_label_t *) arg;
	memset (s, 0, sizeof (oss_label_t));
	return 0;
      }
      break;

    case SNDCTL_GETSONG:
      {
	/*
	 * Return an empty string so that this feature can be tested.
	 * Complete functionality is to be implemented later.
	 */
	oss_longname_t *s = (oss_longname_t *) arg;
	memset (s, 0, sizeof (oss_longname_t));
	return 0;
      }
      break;
    }

  return -EINVAL;
}

/*ARGSUSED*/
static void
userdev_output_block (int dev, oss_native_word buf, int count, int fragsize,
			int intrflag)
{
}

/*ARGSUSED*/
static void
userdev_start_input (int dev, oss_native_word buf, int count, int fragsize,
		       int intrflag)
{
}

static void
userdev_trigger (int dev, int state)
{
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  if (portc->open_mode & OPEN_READ)	/* Handle input */
    {
      portc->input_triggered = !!(state & OPEN_READ);
      if (!portc->input_triggered)
	portc->peer->output_triggered = 0;
    }

  if (portc->open_mode & OPEN_WRITE)	/* Handle output */
    {
      portc->output_triggered = !!(state & OPEN_WRITE);
      if (!portc->output_triggered)
	portc->peer->input_triggered = 0;
    }

  if (portc->output_triggered || portc->input_triggered)	/* Something is going on */
    {
      int tmout = OSS_HZ / 100;

      if (tmout < 1)
	tmout = 1;

      if (portc->port_type != PT_SERVER)
	portc = portc->peer;	/* Switch to the server side */

      if (portc->output_triggered || portc->input_triggered)	/* Something is going on */
	if (devc->timeout_id == 0)
	  devc->timeout_id = timeout (userdev_cb, portc, tmout);
    }
  else
    {
      if (portc->port_type == PT_SERVER)
	if (devc->timeout_id != 0)
	  {
	    untimeout (devc->timeout_id);
	    devc->timeout_id = 0;
	  }
    }
}

/*ARGSUSED*/
static int
userdev_server_prepare_for_input (int dev, int bsize, int bcount)
{
  oss_native_word flags;
  unsigned int status;

  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->input_triggered = 0;

  /*
   * Wake the client which may be in waiting in close() 
   */
  oss_wakeup (portc->peer->wq, &devc->mutex, &flags, POLLOUT | POLLWRNORM);

  if (!(portc->peer->open_mode & OPEN_WRITE))
    {
      /* Sleep until the client side becomes ready */
      oss_sleep (portc->wq, &devc->mutex, 0, &flags, &status);
      if (status & WK_SIGNAL)
	{
	  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
	  return -EINTR;
	}
    }
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_server_prepare_for_output (int dev, int bsize, int bcount)
{
  oss_native_word flags;
  unsigned int status;

  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->output_triggered = 0;

  /*
   * Wake the client which may be in waiting in close() 
   */
  oss_wakeup (portc->peer->wq, &devc->mutex, &flags, POLLIN | POLLRDNORM);

  if (!(portc->peer->open_mode & OPEN_READ))
    {
      /* Sleep until the client side becomes ready */
      oss_sleep (portc->wq, &devc->mutex, 0, &flags, &status);
      if (status & WK_SIGNAL)
	{
	  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
	  return -EINTR;
	}
    }
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_client_prepare_for_input (int dev, int bsize, int bcount)
{
  oss_native_word flags;
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  unsigned int status;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->input_triggered = 0;
  /* Wake the server side */
  oss_wakeup (portc->peer->wq, &devc->mutex, &flags,
	      POLLIN | POLLRDNORM);

  /*
   * Delay a moment so that the server side gets chance to reinit itself
   * for next file/stream.
   */
  oss_sleep (portc->wq, &devc->mutex, OSS_HZ, &flags, &status);
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_client_prepare_for_output (int dev, int bsize, int bcount)
{
  oss_native_word flags;
  userdev_portc_t *portc = audio_engines[dev]->portc;
  userdev_devc_t *devc = audio_engines[dev]->devc;
  unsigned int status;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->output_triggered = 0;
  /* Wake the server side */
  oss_wakeup (portc->peer->wq, &devc->mutex, &flags,
	      POLLIN | POLLRDNORM);

  /*
   * Delay a moment so that the server side gets chance to reinit itself
   * for next file/stream.
   */
  oss_sleep (portc->wq, &devc->mutex, OSS_HZ, &flags, &status);
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 0;
}

/*ARGSUSED*/
static int
userdev_alloc_buffer (int dev, dmap_t * dmap, int direction)
{
#define MY_BUFFSIZE (64*1024)
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
userdev_free_buffer (int dev, dmap_t * dmap, int direction)
{
  if (dmap->dmabuf == NULL)
    return 0;
  KERNEL_FREE (dmap->dmabuf);

  dmap->dmabuf = NULL;
  return 0;
}

#if 0
static int
userdev_get_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
}
#endif

static audiodrv_t userdev_server_driver = {
  userdev_server_open,
  userdev_server_close,
  userdev_output_block,
  userdev_start_input,
  userdev_ioctl,
  userdev_server_prepare_for_input,
  userdev_server_prepare_for_output,
  userdev_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  userdev_trigger,
  userdev_server_set_rate,
  userdev_server_set_format,
  userdev_server_set_channels,
  NULL,
  NULL,
  userdev_check_input,
  userdev_check_output,
  userdev_alloc_buffer,
  userdev_free_buffer,
  NULL,
  NULL,
  NULL				/* userdev_get_buffer_pointer */
};

static audiodrv_t userdev_client_driver = {
  userdev_client_open,
  userdev_client_close,
  userdev_output_block,
  userdev_start_input,
  userdev_ioctl,
  userdev_client_prepare_for_input,
  userdev_client_prepare_for_output,
  userdev_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  userdev_trigger,
  userdev_client_set_rate,
  userdev_client_set_format,
  userdev_client_set_channels,
  NULL,
  NULL,
  userdev_check_input,
  userdev_check_output,
  userdev_alloc_buffer,
  userdev_free_buffer,
  NULL,
  NULL,
  NULL				/* userdev_get_buffer_pointer */
};


static int
install_server (userdev_devc_t * devc)
{
  userdev_portc_t *portc = &devc->server_portc;
  int adev;

  int opts =
    ADEV_STEREOONLY | ADEV_16BITONLY | ADEV_VIRTUAL |
    ADEV_FIXEDRATE | ADEV_SPECIAL;

  memset (portc, 0, sizeof (*portc));

  portc->devc = devc;
  if ((portc->wq = oss_create_wait_queue (devc->osdev, "userdev")) == NULL)
    {
      cmn_err (CE_WARN, "Cannot create userdev wait queue\n");
      return -EIO;
    }

  portc->port_type = PT_SERVER;

  if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				    devc->osdev,
				    devc->osdev,
				    "User space audio device server side",
				    &userdev_server_driver,
				    sizeof (audiodrv_t),
				    opts, SUPPORTED_FORMATS, devc, -1)) < 0)
    {
      return adev;
    }

  audio_engines[adev]->portc = portc;
  audio_engines[adev]->min_rate = 5000;
  audio_engines[adev]->max_rate = MAX_RATE;
  audio_engines[adev]->min_channels = 1;
  audio_engines[adev]->caps |= PCM_CAP_HIDDEN;
  audio_engines[adev]->max_channels = MAX_CHANNELS;;

  portc->audio_dev = adev;

  return 0;
}


static int
install_client (userdev_devc_t * devc)
{
  userdev_portc_t *portc = &devc->client_portc;
  char tmp[64];
  int adev;

  int opts =
    ADEV_STEREOONLY | ADEV_16BITONLY | ADEV_VIRTUAL |
    ADEV_FIXEDRATE | ADEV_SPECIAL | ADEV_LOOP;

  memset (portc, 0, sizeof (*portc));

  portc->devc = devc;
  if ((portc->wq = oss_create_wait_queue (devc->osdev, "userdev")) == NULL)
    {
      cmn_err (CE_WARN, "Cannot create userdev wait queue\n");
      return -EIO;
    }

  portc->port_type = PT_CLIENT;

  if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				    devc->osdev,
				    devc->osdev,
				    "User space audio device",
				    &userdev_client_driver,
				    sizeof (audiodrv_t),
				    opts, SUPPORTED_FORMATS, devc, -1)) < 0)
    {
      return adev;
    }

  audio_engines[adev]->portc = portc;
  audio_engines[adev]->min_rate = 5000;
  audio_engines[adev]->max_rate = MAX_RATE;
  audio_engines[adev]->min_channels = 1;
  audio_engines[adev]->max_channels = MAX_CHANNELS;;
  audio_engines[adev]->enabled = 0;	/* Not enabled until server side is opened */

  portc->audio_dev = adev;

  return 0;
}

static int
create_device_pair(void)
{
  int client_engine, server_engine;
  userdev_devc_t *devc;
  oss_native_word flags;

  if ((devc=PMALLOC(userdev_osdev, sizeof (*devc))) == NULL)
     return -ENOMEM;
  memset(devc, 0, sizeof(*devc));

  devc->osdev = userdev_osdev;
  MUTEX_INIT (devc->osdev, devc->mutex, MH_DRV);

  devc->rate = 48000;
  devc->fmt = AFMT_S16_NE;
  devc->fmt_bytes = 2;
  devc->channels = 2;

  if ((server_engine=install_server (devc)) < 0)
     return server_engine;

  if ((client_engine=install_client (devc)) < 0)
	return client_engine;

  devc->client_portc.peer = &devc->server_portc;
  devc->server_portc.peer = &devc->client_portc;

  /*
   * Insert the device to the list of available devices
   */
  MUTEX_ENTER_IRQDISABLE(userdev_global_mutex, flags);
  devc->next_instance = active_device_list;
  active_device_list = devc;
  MUTEX_ENXIT_IRQRESTORE(userdev_global_mutex, flags);

  return server_engine;
}

static void
delete_device_pair(userdev_devc_t *devc)
{
  MUTEX_CLEANUP(devc->mutex);
}

int
oss_userdev_attach (oss_device_t * osdev)
{
  int i;

  userdev_osdev = osdev;

  osdev->devc = NULL;
  MUTEX_INIT (osdev, userdev_global_mutex, MH_DRV);

  oss_register_device (osdev, "User space audio driver subsystem");

  create_device_pair();

  return 1;
}

int
oss_userdev_detach (oss_device_t * osdev)
{
  userdev_devc_t *devc;
  int i;

  if (oss_disable_device (osdev) < 0)
    return 0;

  devc = active_device_list;

  while (devc != NULL)
  {
	  userdev_devc_t *next = devc->next_instance;

	  delete_device_pair(devc);

	  devc = next;
  }

  oss_unregister_device (osdev);

  MUTEX_CLEANUP(userdev_global_mutex);

  return 1;
}
