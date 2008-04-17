/*
 * Purpose: SoftOSS recording driver
 *
 * recording doesn't work reliably and it has been disabled (the new vmix
 * driver can do recording).
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2005. All rights reserved.

#include "softoss_cfg.h"
#include "softoss.h"

recvoice_info softoss_recvoices[MAX_AVOICE] = { {0}
};

void
start_record_engine (softoss_devc * devc)
{
  adev_p adev;
  dmap_p master_dmap;

  if (!devc->duplex_mode)
    return;

  adev = audio_engines[devc->input_master];
  master_dmap = adev->dmap_in;

  if (devc->tmp_recbuf != NULL)
    KERNEL_FREE (devc->tmp_recbuf);

  switch (devc->bits)
    {
    case 16:
      devc->rec_nsamples = master_dmap->fragment_size / 2;
      break;

    case 24:
      devc->rec_nsamples = master_dmap->fragment_size / 3;
      break;

    case 32:
      devc->rec_nsamples = master_dmap->fragment_size / 4;
      break;

    default:
      cmn_err (CE_PANIC, "Unsupported recording sample format\n");
      return;
    }

  devc->rec_nsamples /= adev->hw_parms.channels;

  devc->tmp_recbuf = KERNEL_MALLOC (devc->rec_nsamples * sizeof (int));
}

void
stop_record_engine (softoss_devc * devc)
{
  if (!devc->duplex_mode)
    return;

  if (devc->tmp_recbuf != NULL)
    KERNEL_FREE (devc->tmp_recbuf);
  devc->tmp_recbuf = NULL;
}

#ifdef SINE_DEBUG
static int
sin_gen (void)
{

  static int phase = 0, v;

  static short sinebuf[48] = {
    0, 4276, 8480, 12539, 16383, 19947, 23169, 25995,
    28377, 30272, 31650, 32486, 32767, 32486, 31650, 30272,
    28377, 25995, 23169, 19947, 16383, 12539, 8480, 4276,
    0, -4276, -8480, -12539, -16383, -19947, -23169, -25995,
    -28377, -30272, -31650, -32486, -32767, -32486, -31650, -30272,
    -28377, -25995, -23169, -19947, -16383, -12539, -8480, -4276
  };
  v = sinebuf[phase];
  phase = (phase + 1) % 48;

  return v;
}
#endif

static __inline__ short
swap16 (short v)
{
  unsigned short s = (unsigned short) v;

  s = ((s & 0xff) << 8) | ((s >> 8) & 0xff);

  return (short) s;
}

static __inline__ int
swap32 (int v)
{
  unsigned int x = (unsigned int) v;

  x = ((x & 0x000000ff) << 24) |
    ((x & 0x0000ff00) << 8) |
    ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24);
  return (int) x;
}

void
softoss_handle_input (softoss_devc * devc)
{
  int voice;
  int p;

  adev_p adev;
  dmap_p master_dmap;
  unsigned char *devbuf;
  int *buf;
  int i, n, nc;
  register int v;

  if (!devc->duplex_mode || devc->opened_inputs < 1)
    return;

  if (devc->input_master < 0 || devc->input_master >= num_audio_engines)
    return;

  adev = audio_engines[devc->input_master];
  master_dmap = adev->dmap_in;

  if (master_dmap == NULL)
    return;

  if (master_dmap->byte_counter <= (master_dmap->user_counter + master_dmap->fragment_size))	/* Buffer empty */
    return;

  p = (master_dmap->user_counter % master_dmap->bytes_in_use);
  devbuf = (unsigned char *) (master_dmap->dmabuf + p);
  master_dmap->user_counter += master_dmap->fragment_size;

/*
 * Convert the incoming samples to AFMT_S24_NE
 */

  buf = devc->tmp_recbuf;
  nc = devc->channels;
  if (nc > 2)
    nc = 2;
  n = devc->rec_nsamples * nc;

  switch (devc->bits)
    {
    case 16:
      {
	if (devc->chendian)
	  {
	    for (i = 0; i < n; i++)
	      {
		v = *(short *) devbuf;
		devbuf += 2;
		*buf++ = swap16 (v) * 256;
	      }
	  }
	else
	  {
	    for (i = 0; i < n; i++)
	      {
		v = *(short *) devbuf;
		devbuf += 2;
		*buf++ = v * 256;
	      }
	  }
      }
      break;

    case 24:
      {
	for (i = 0; i < n; i++)
	  {
	    v = devbuf[2] | (devbuf[1] << 8) | (devbuf[0] << 16);
	    devbuf += 3;
	    *buf++ = v;
	  }
      }
      break;

    case 32:
      {
	if (devc->chendian)
	  {
	    for (i = 0; i < n; i++)
	      {
		v = *(int *) devbuf;
		*buf++ = swap32 (v) / 256;
		devbuf += 4;
	      }
	  }
	else
	  {
	    for (i = 0; i < n; i++)
	      {
		v = *(int *) devbuf;
		*buf++ = v / 256;
		devbuf += 4;
	      }
	  }
      }
      break;
    }

  for (voice = 0; voice < devc->maxvoice; voice++)
    if (recvoice_active[voice])
      {
	dmap_t *dmap;
	recvoice_info *v;

	v = &softoss_recvoices[voice];

	dmap = audio_engines[v->audiodev]->dmap_in;

	v->mixer (devc, v, devc->tmp_recbuf, devc->rec_nsamples, dmap);
	oss_audio_inputintr (v->audiodev, AINTR_NO_POINTER_UPDATES);
      }
}

/*ARGSUSED*/
void
recmix_16_mono_ne (softoss_devc * devc, recvoice_info * v, int *devbuf,
		   int devsize, dmap_t * dmap)
{
  short *buf;
  int i, len, ds, pos;

  pos = (int) (dmap->byte_counter % dmap->bytes_in_use);

  while (devsize > 0)
    {
      ds = devsize;

      len = ds * 2 * sizeof (*buf);

      if (pos + len > dmap->bytes_in_use)
	len = dmap->bytes_in_use - pos;

      ds = len / (2 * sizeof (*buf));
      buf = (short *) (dmap->dmabuf + pos);

      for (i = 0; i < ds; i++)
	{
	  *buf++ = devbuf[0] / 256;

	  devbuf += v->channels;
	}

      oss_audio_inc_byte_counter (dmap, len);
      devsize -= ds;

      pos = 0;
    }
}

/*ARGSUSED*/
void
recmix_16_stereo_ne (softoss_devc * devc, recvoice_info * v, int *devbuf,
		     int devsize, dmap_t * dmap)
{
  short *buf;
  int i, len, ds, pos;

  pos = (int) (dmap->byte_counter % dmap->bytes_in_use);

  while (devsize > 0)
    {
      ds = devsize;

      len = ds * 2 * sizeof (*buf);

      if (pos + len > dmap->bytes_in_use)
	len = dmap->bytes_in_use - pos;

      ds = len / (2 * sizeof (*buf));
      buf = (short *) (dmap->dmabuf + pos);

      for (i = 0; i < ds; i++)
	{
#ifdef SINE_DEBUG
	  *buf++ = sin_gen ();
	  *buf++ = 0;
#else
	  *buf++ = (short) (devbuf[0] / 256);
	  *buf++ = (short) (devbuf[1] / 256);
#endif

	  devbuf += v->channels;
	}

      oss_audio_inc_byte_counter (dmap, len);
      devsize -= ds;

      pos = 0;
    }
}
