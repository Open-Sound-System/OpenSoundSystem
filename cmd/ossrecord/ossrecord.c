/* Purpose: Simple wave file (.wav) recorder for OSS
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>


#include <soundcard.h>
#include <signal.h>
#include <time.h>
#include <errno.h>


#define RIFF         "RIFF"
#define WAVE         "WAVE"
#define FMT          "fmt "
#define DATA         "data"
#define PCM_CODE     1
#define WAVE_MONO    1
#define WAVE_STEREO  2

typedef struct /* __attribute__ ((__packed__)) */
{
  char main_chunk[4];
  unsigned int length;
  char chunk_type[4];

  char sub_chunk[4];
  unsigned int sc_len;
  unsigned short format;
  unsigned short modus;
  unsigned int sample_fq;
  unsigned int byte_p_sec;
  unsigned short block_align;
  unsigned short bit_p_spl;

  char data_chunk[4];
  unsigned int data_length;

}
WaveHeader;



int audio_fd, speed = 48000, bits = 16, channels = 2, datalimit =
  -1, data_size;
char dspdev[32] = "/dev/dsp";
unsigned char audiobuf[512];
WaveHeader wh;
FILE *wave_fp;
int x = -10, ii = 0, time1 = 0;
int verbose = 0;
int level_meters = 0, level = 0;
int amplification=1;
int reclevel = 0;
int nfiles = 1;
char *program;
char *recsrc = NULL;
char *current_filename = "???";
int raw_mode = 0;

char script[128] = "";

static void
select_recsrc (char *srcname)
{
/*
 * Handling of the -i command line option (recording source selection).
 *
 * Empty or "?" shows the available recording sources.
 */
  int i, src;
  oss_mixer_enuminfo ei;

  if (ioctl (audio_fd, SNDCTL_DSP_GET_RECSRC_NAMES, &ei) == -1)
    {
      perror ("SNDCTL_DSP_GET_RECSRC_NAMES");
      exit (-1);
    }

  if (ioctl (audio_fd, SNDCTL_DSP_GET_RECSRC, &src) == -1)
    {
      perror ("SNDCTL_DSP_GET_RECSRC");
      exit (-1);
    }

  if (*srcname == 0 || strcmp (srcname, "?") == 0)
    {
      fprintf (stderr, "\nPossible recording sources for the selected device:\n\n");

      for (i = 0; i < ei.nvalues; i++)
	{
	  fprintf (stderr, "\t%s", ei.strings + ei.strindex[i]);
	  if (i == src)
	    fprintf (stderr, " (currently selected)");
	  fprintf (stderr, "\n");
	}
      fprintf (stderr, "\n");
      exit (0);
    }

  for (i = 0; i < ei.nvalues; i++)
    {
      char *s = ei.strings + ei.strindex[i];
      if (strcmp (s, srcname) == 0)
	{
	  src = i;
	  if (ioctl (audio_fd, SNDCTL_DSP_SET_RECSRC, &src) == -1)
	    {
	      perror ("SNDCTL_DSP_SET_RECSRC");
	      exit (-1);
	    }

	  return;
	}
    }

  fprintf (stderr,
	   "Unknown recording source name '%s' - use -i? to get the list\n",
	   srcname);
  exit (-1);
}

static void
open_audio (void)
{
  int flags = O_RDONLY;

  if (raw_mode)
    flags |= O_EXCL;

  audio_fd = open (dspdev, flags);
  if (audio_fd < 0)
    {
      perror (dspdev);
      exit (-1);
    }

  if (verbose)
     {
  	oss_audioinfo ai;

	ai.dev=-1;

	if (ioctl(audio_fd, SNDCTL_ENGINEINFO, &ai) != -1)
	   fprintf (stderr, "Recording from %s\n", ai.name);
	   
     }

  if (raw_mode)
    {
      /*
       * Disable format conversions. No error checking.
       */
      int tmp = 0;
      ioctl (audio_fd, SNDCTL_DSP_COOKEDMODE, &tmp);
    }

  if (recsrc != NULL)
    select_recsrc (recsrc);
}

static void decorate (void);

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
  int r, bits, origbits;

  bits = i;
  if (i == 32)
    bits = AFMT_S32_LE;
  if (i == 16)
    bits = AFMT_S16_LE;
  origbits = bits;

  r = ioctl (audio_fd, SNDCTL_DSP_SETFMT, &bits);
  if (r < 0)
    {
      int e = errno;
      perror (dspdev);
      if (e != ENOTTY)
	exit (-1);
    }

  if (bits != origbits)		/* Was not accepted */
    i = bits;

  return i;
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

static int
bswap (int ind)
{
  int outd = 0, i;
  unsigned char *o = (unsigned char *) &outd;

  for (i = 0; i < 4; i++)
    {
      o[i] = ind & 0xff;
      ind >>= 8;
    }

  return outd;
}

static short
bswaps (short ind)
{
  short outd = 0, i;
  unsigned char *o = (unsigned char *) &outd;

  for (i = 0; i < 2; i++)
    {
      o[i] = ind & 0xff;
      ind >>= 8;
    }

  return outd;
}

static void
write_head (void)
{
  memcpy ((char *) &wh.main_chunk, RIFF, 4);
  wh.length = bswap (datalimit + sizeof (WaveHeader) - 8);
  memcpy ((char *) &wh.chunk_type, WAVE, 4);
  memcpy ((char *) &wh.sub_chunk, FMT, 4);
  wh.sc_len = bswap (16);
  wh.format = bswaps (PCM_CODE);
  wh.modus = bswaps (channels);
  wh.sample_fq = bswap (speed);
  wh.block_align = bswaps ((bits / 8) * channels);
  wh.byte_p_sec = bswap (speed * channels * (bits / 8));
  wh.bit_p_spl = bswaps (bits);
  memcpy ((char *) &wh.data_chunk, DATA, 4);
  wh.data_length = bswap (datalimit);
  fwrite (&wh, sizeof (WaveHeader), 1, wave_fp);
}

static void
end ()
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

static void
intr (int i)
{
  if (verbose)
    {
      decorate ();
      fprintf (stderr, "\nStopped (%d).\n", i);
    }
  fprintf (stderr, "\n");
  fflush (stdout);
  end ();
  if (*script)
    {
      fprintf (stderr, "Waiting for the '%s' script(s) to finish - please stand by\n",
	      script);
      while (wait (NULL) != -1);
    }
  exit (0);
}

static void
fatal_signal (int i)
{
  if (verbose)
    {
      decorate ();
      fprintf (stderr, "\nStopped.(signal %d)\n", i);
    }
  end ();
  if (*script)
    {
      fprintf (stderr, "Waiting for the '%s' script(s) to finish - please stand by\n",
	      script);
      while (wait (NULL) != -1);
    }
  exit (0);
}

static void
decorate (void)
{
  int x1, x2, i;
  float secs;
  fprintf (stderr, "\r%s [", current_filename);
  x1 = x;
  x2 = x + 10;

  if (x < 0)
    {
      x1 = 0;
      x2 = x + 10;
      if (x2 < 0)
	x2 = 0;
    }
  if (x > 0)
    {
      x2 = 10;
      x1 = x;
    }

  for (i = 0; i < x1; i++)
    fprintf (stderr, " ");
  for (i = x1; i < x2; i++)
    fprintf (stderr, ".");
  for (i = 0; i < 10 - x2; i++)
    fprintf (stderr, " ");
  secs = (float) (data_size / (speed * (bits / 8) * channels));

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
  if (ii == 0)
    {
      x++;
      if (x >= 10)
	ii = 1;
    }
  else
    {
      x--;
      if (x <= -10)
	ii = 0;
    }
  fflush (stdout);
}

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

static void
update_level (void *inbuf, int l)
{
/*
 * Display a rough recording level meter
 */
  int i, v;
  char tmp[12], template[12] = "-------++!!";

  level = (level / 3) * 2;

  switch (bits)
    {
    case 8:
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

    case 16:
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

    case 32:
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
    }

  v = db_table[level >> 24];

  memset (tmp, ' ', 12);
  tmp[11] = 0;

  tmp[0] = '0';

  for (i = 0; i < v; i++)
    tmp[i] = template[i];

  fprintf (stderr, "\rVU %s", tmp);
  fflush (stdout);
}

void
usage (void)
{
  fprintf
    (stderr,
     "Usage: %s [-s<speed> -b<bits{8|16|32}> -c<channels> -v -l -m<nfiles> "
     "-d<device> -t<maxsecs> -L<level> -i<recsrc>|? -a<amplification>] "
     "filename.wav\n",
     program);
  exit (0);
}

static void
find_devname (char *devname, char *num)
{
/*
 * OSS 4.0 the audio device numbering may be different from the
 * legacy /dev/dsp# numbering reported by /dev/sndstat. Try to find the
 * device name (devnode) that matches the given device number.
 *
 * Prior versions of ossrecord simply used the the /dev/dsp# number.
 */
  int dev;
  int mixer_fd;
  oss_audioinfo ai;

  if (sscanf (num, "%d", &dev) != 1)
    {
      fprintf (stderr, "Invalid audio device number '%s'\n", num);
      exit (-1);
    }

  if ((mixer_fd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      perror ("/dev/mixer");
      fprintf (stderr, "Warning: Defaulting to /dev/dsp%s\n", num);
      sprintf (devname, "/dev/dsp%s", num);
      return;
    }

  ai.dev = dev;

  if (ioctl (mixer_fd, SNDCTL_AUDIOINFO, &ai) == -1)
    {
      perror ("/dev/mixer SNDCTL_AUDIOINFO");
      fprintf (stderr, "Warning: Defaulting to /dev/dsp%s\n", num);
      sprintf (devname, "/dev/dsp%s", num);
      close (mixer_fd);
      return;
    }

  strcpy (devname, ai.devnode);

  close (mixer_fd);
}

static void
amplify(unsigned char *b, int count)
{
	switch (bits)
	{
	case 16:
		{
			int i, l=count/2;
			short *s=(short *)b;

			for (i=0;i<l;i++)
			    s[i] *= amplification;
		}
		break;

	case 32:
		{
			int i, l=count/4;
			int *s=(int *)b;

			for (i=0;i<l;i++)
			    s[i] *= amplification;
		}
		break;

	}
}

int
do_record (char *dspdev, char *wave_name)
{
  current_filename = wave_name;

  if (reclevel != 0)
    {
      int level;

      level = reclevel | (reclevel << 8);

      if (ioctl (audio_fd, SNDCTL_DSP_SETRECVOL, &level) == -1)
	perror ("SNDCTL_DSP_SETRECVOL");
    }

  if (strcmp(wave_name, "-") == 0)
    {
      wave_fp = fdopen (1, "wb");
      verbose = 0;
    }
  else
     wave_fp = fopen (wave_name, "wb");

  if (wave_fp == NULL)
    {
      perror (wave_name);
      exit (-1);
    }

  channels = set_channels (channels);
  speed = set_rate (speed);
  bits = set_format (bits);

  if (verbose)
    {
      if (channels == 1)
	fprintf (stderr, "Recording wav: Speed %dHz %d bits Mono\n", speed, bits);
      if (channels == 2)
	fprintf (stderr, "Recording wav: Speed %dHz %d bits Stereo\n", speed, bits);
      if (channels > 2)
	fprintf (stderr, "Recording wav: Speed %dHz %d bits %d channels\n", speed,
		bits, channels);
    }
  data_size = 0;
  time1 = 0;

  if (datalimit != -1)
    datalimit = datalimit * speed * channels * (bits / 8);

/* 
 * Write the initial RIFF header (practically unlimited length)
 */
  {
    int dl = datalimit;

    if (datalimit <= 0)
      datalimit = 0x7fff0000;
    write_head ();
    datalimit = dl;
  }

  if (level_meters)
    verbose = 0;

  if (verbose)
    decorate ();
   /*LINTED*/ while (1)
    {
      int l;
      if ((l = read (audio_fd, audiobuf, 512)) == -1)
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

      if (amplification != 1)
     	 amplify (audiobuf, 512);

      fwrite (audiobuf, 1, l, wave_fp);
      if (level_meters)
	update_level (audiobuf, l);

      data_size += 512;
      time1 += 512;

      if (time1 >= speed * (bits / 8) * channels / 10)
	{
	  if (verbose)
	    {
	      decorate ();
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
      decorate ();
      fprintf (stderr, "\nDone.\n");
    }
  end ();
  return 0;
}

int
main (int argc, char *argv[])
{
  int c;
  extern char *optarg;
  extern int optind;

  program = (char *) argv[0];
  if (argc < 2)
    usage ();

  while ((c = getopt (argc, argv, "SMRwvlhs:b:d:c:t:L:i:m:r:a:")) != EOF)
    switch (c)
      {
      case 'a':
	amplification = atoi (optarg);
	if (amplification==0)
	   usage ();
      case 's':
	speed = atoi (optarg);
	if (speed == 0)
	  {
	    fprintf (stderr, "Bad sampling rate given\n");
	    exit (-1);
	  }
	if (speed < 1000)
	  speed *= 1000;
	break;

      case 'b':
	bits = atoi (optarg);
	if (bits != 8 && bits != 16 && bits != 32)
	  fprintf (stderr, "Warning: Unsupported bits %d\n", bits);
	break;

      case 'd':
	if (*optarg >= '0' && *optarg <= '9')	/* Just the device number was given */
	  find_devname (dspdev, optarg);
	else
	  strcpy (dspdev, optarg);
	break;

      case 'r':
	strcpy (script, optarg);
	break;

      case 'S':
	channels = 2;
	break;

      case 'M':
	channels = 1;
	break;

      case 'c':
	channels = atoi (optarg);
	break;

      case 'R':
	raw_mode = 1;
	break;

      case 't':
	datalimit = atoi (optarg);
	break;

      case 'm':
	nfiles = atoi (optarg);
	break;

      case 'w':
	break;

      case 'v':
	verbose = 1;
	break;

      case 'l':
	level_meters = 1;
	break;

      case 'h':
	usage ();
	break;

      case 'L':
	reclevel = atoi (optarg);
	if (reclevel < 1 || reclevel > 100)
	  {
	    fprintf (stderr, "%s: Bad recording level '%s'\n", argv[0],
		     optarg);
	    exit (-1);
	  }
	break;

      case 'i':
	recsrc = optarg;
	break;

      default:
	usage ();
      }
  open_audio ();

  if (optind != argc - 1)	/* No file or multiple file names given */
    {
      usage ();
      exit (-1);
    }

  signal (SIGSEGV, fatal_signal);
  signal (SIGPIPE, fatal_signal);
  signal (SIGINT, intr);

  if (nfiles > 1)		/* Record multiple files */
    {
      int tmp_datalimit = datalimit;
      int i;

      for (i = 0; i < nfiles; i++)
	{
	  char tmpname[256];
	  int err;

	  sprintf (tmpname, argv[optind], i + 1);
	  datalimit = tmp_datalimit;
	  if ((err = do_record (dspdev, tmpname)) != 0)
	    exit (err);
	  fprintf (stderr, "\n");
	}
      if (*script)
	{
	  printf
	    ("Waiting for the '%s' script(s) to finish - please stand by\n",
	     script);
	  while (wait (NULL) != -1);
	}

      exit (0);
    }

  return do_record (dspdev, argv[optind]);
}
