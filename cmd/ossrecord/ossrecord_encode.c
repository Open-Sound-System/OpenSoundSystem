/* Purpose: Simple wave file (.wav) recorder for OSS
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <soundcard.h>
#include <errno.h>
#include "ossrecord.h"
#include "ossrecord_encode.h"
#include "ossrecord_wparser.h"

extern int amplification, level_meters, nfiles, raw_mode, reclevel, verbose;
extern char script[512], dspdev[512];
FILE *wave_fp;
static int data_size;

static unsigned char audiobuf[512];
static const char * current_filename = "???";
static double constant;

static void amplify (unsigned char *, int);
static int set_rate (int);
static int set_format (int);
static int set_channels (int);
static void update_level (void *, int);

static void
amplify (unsigned char *b, int count)
{
  switch (format)
    {
      case AFMT_S16_NE:
        {
          int i, l=count/2;
          short * s=(short *)b;

          for (i=0;i<l;i++) s[i] = s[i] * amplification / 100;
        }
        break;

      case AFMT_S24_NE:
      case AFMT_S32_NE:
        {
          int i, l=count/4;
          int * s=(int *)b;

          for (i=0;i<l;i++) s[i] = s[i] * (long)amplification / 100;
        }
        break;

    }
}

void
decorate (void * inbuf, int l, int toggle)
{
  int x1, x2, i;
  double secs;
  static int dots, direction;

  secs = data_size / constant;
  fprintf (stderr, "\r%s [", current_filename);
  x1 = dots;
  x2 = dots + 10;

  if (toggle)
    {
      if (toggle == -1)
        {
          dots = -11;
          direction = 0;
        }
      if (direction == 0)
        {
          dots++;
          if (dots >= 10) direction = 1;
        }
      else
        {
          dots--;
          if (dots <= -10) direction = 0;
        }
    }
 
  if (dots < 0)
    {
      x1 = 0;
      x2 = dots + 10;
      if (x2 < 0) x2 = 0;
    }
  if (dots >= 0)
    {
      x2 = 10;
      x1 = dots;
    }

  for (i = 0; i < x1; i++)
    fprintf (stderr, " ");
  for (i = x1; i < x2; i++)
    fprintf (stderr, ".");
  for (i = 0; i < 10 - x2; i++)
    fprintf (stderr, " ");

  if (secs < 60.0)
    fprintf (stderr, "] %1.2f secs", secs);
  else
    {
      int hours, mins;

      mins = (int) (secs / 60.0);
      secs -= (mins * 60);

      hours = mins / 60;
      mins = mins % 60;
      fprintf (stderr, "] %02d:%02d:%02.2f", hours, mins, secs);
    }
  if (l && level_meters)
    {
      fprintf (stderr, " ");
      update_level (inbuf, l);
      return;
    }
  if (level_meters) fprintf (stderr, " VU 0");
  fflush (stderr);
}

int
do_record (const char * dspdev, const char * wave_name)
{
  int time1 = 0;

  current_filename = wave_name;

  format = set_format (format);
  channels = set_channels (channels);
  speed = set_rate (speed);
  constant = format2obits (format) * speed * channels / 8;

  if (reclevel != 0)
    {
      int level;

      level = reclevel | (reclevel << 8);

      if (ioctl (audio_fd, SNDCTL_DSP_SETRECVOL, &level) == -1)
	perror ("SNDCTL_DSP_SETRECVOL");
    }

  if (verbose)
    {
      if (channels == 1)
	fprintf (stderr, "Recording wav: Speed %dHz %d bits Mono\n",
                 speed, format2obits(format));
      if (channels == 2)
	fprintf (stderr, "Recording wav: Speed %dHz %d bits Stereo\n",
                 speed, format2obits(format));
      if (channels > 2)
	fprintf (stderr, "Recording wav: Speed %dHz %d bits %d channels\n",
                 speed, format2obits(format), channels);
    }
  data_size = 0;
  time1 = 0;

  if (datalimit != -1)
    datalimit = datalimit * constant;

/* 
 * Write the initial RIFF header (practically unlimited length)
 */
  if (strcmp(wave_name, "-") == 0)
    wave_fp = fdopen (1, "wb");
  else
    wave_fp = fopen (wave_name, "wb");

  if (wave_fp == NULL)
    {
      perror (wave_name);
      exit (-1);
    }
  if (write_head () == -1) exit (-1);

  if (verbose)
    decorate (NULL, 0, -1);
   /*LINTED*/ while (1)
    {
      int l;
      if ((l = read (audio_fd, audiobuf, sizeof (audiobuf))) == -1)
	{
	  if (errno == ECONNRESET)	/* Device disconnected */
	    {
	      end ();
	      return 0;
	    }

	  perror (dspdev);
	  end ();
	  exit (-1);
	}
      if (l == 0)
	{
	  fprintf (stderr, "Unexpected EOF on audio device\n");
	  end ();
	  return 0;
	}

      if ((amplification > 0) && (amplification != 100))
     	 amplify (audiobuf, sizeof (audiobuf));

      fwrite (audiobuf, 1, l, wave_fp);
      if (level_meters)
        {
          if (verbose) decorate (audiobuf, l, 0);
          else update_level (audiobuf, l);
        }

      data_size += sizeof (audiobuf);
      time1 += sizeof (audiobuf);

      if (time1 >= constant / 1)
	{
	  if (verbose)
	    {
	      decorate (audiobuf, l, 1);
	    }
	  time1 = 0;
	}
      if (datalimit != -1 && data_size >= datalimit)
	{
	  break;
	}
    }
  if (verbose)
    {
      decorate (NULL, 0, 1);
      fprintf (stderr, "\nDone.\n");
    }
  end ();
  return 0;
}

void
end (void)
{
  fseek (wave_fp, 0, SEEK_SET);
  datalimit = data_size;
  write_head ();
  fclose (wave_fp);
  close (audio_fd);

  if (*script)
    {
      if (fork () == 0)
	{
	  if (execlp (script, script, current_filename, NULL) == -1)
	    {
	      perror (script);
	      exit (-1);
	    }
	}
    }
  if (nfiles > 1)
    open_audio ();
}

static int
set_rate (int i)
{
  int r;
  r = ioctl (audio_fd, SNDCTL_DSP_SPEED, &i);
  if (r < 0)
    {
      int e = errno;
      perror (dspdev);
      if (e != ENOTTY)
	exit (-1);
    }
  return i;
}

static int
set_format (int i)
{
  int r, fmt;

  fmt = i;

  r = ioctl (audio_fd, SNDCTL_DSP_SETFMT, &fmt);
  if (r < 0)
    {
      int e = errno;
      perror (dspdev);
      if (e != ENOTTY)
	exit (-1);
    }

  if (fmt != i)		/* Was not accepted */
    {
      fprintf (stderr, "format not accepted.\n");
      exit (-1);
    }

  return fmt;
}

static int
set_channels (int i)
{
  int r;
  r = ioctl (audio_fd, SNDCTL_DSP_CHANNELS, &i);
  if (r < 0)
    {
      int e = errno;
      perror (dspdev);
      if (e != ENOTTY)
	exit (-1);
    }
  return i;
}

static void
update_level (void *inbuf, int l)
{
/*
 * Display a rough recording level meter
 */
  int i, v;
  char tmp[12], template[12] = "-------++!!";
  static unsigned char db_table[256] = {	/* Lookup table for log10(ix)*2, ix=0..255 */
    0, 0, 1, 2, 2, 3, 3, 3, 4, 4,
    4, 4, 4, 5, 5, 5, 5, 5, 5, 5,
    5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11
  };
  static int level;

  level = (level / 3) * 2;

  switch (format)
    {
    case AFMT_U8:
      {
	char *p;

	p = inbuf;

	for (i = 0; i < l; i++)
	  {
	    v = ((*p++) - 128) << 24;
	    if (v < 0)
	      v = -v;
	    if (v > level)
	      level = v;
	  }
      }
      break;

    case AFMT_S16_NE:
      {
	short *p;

	p = inbuf;
	l /= 2;

	for (i = 0; i < l; i++)
	  {
	    v = (*p++) << 16;
	    if (v < 0)
	      v = -v;
	    if (v > level)
	      level = v;
	  }
      }
      break;

    case AFMT_S24_NE:
    case AFMT_S32_NE:
      {
	int *p;

	p = inbuf;
	l /= 4;

	for (i = 0; i < l; i++)
	  {
	    v = *p++;
	    if (v < 0)
	      v = -v;
	    if (v > level)
	      level = v;
	  }
      }
      break;
      default: /* NOTREACHED */ return;
    }

  v = db_table[level >> 24];

  memset (tmp, ' ', 12);
  tmp[11] = 0;

  tmp[0] = '0';

  for (i = 0; i < v; i++)
    tmp[i] = template[i];

  if (!verbose) fprintf (stderr, "\r");
  fprintf (stderr, "VU %s", tmp);
  fflush (stderr);
}
