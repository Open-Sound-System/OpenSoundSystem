/* Purpose: Simple wave file (.wav) recorder for OSS
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <soundcard.h>
#include "ossrecord.h"
#include "ossrecord_encode.h"

int audio_fd, channels = -1, datalimit = -1, format = -1, speed = -1,
    type = WAVE_FILE;
char dspdev[512] = "/dev/dsp"; /* Must be at least OSS_DEVNODE_SIZE */
char script[512] = "";
int amplification = 100, level_meters = 0, nfiles = 1, raw_mode = 0,
    reclevel = 0, verbose = 0;
static const char * recsrc = NULL;
static const format_t format_a[] = {
  {"S8",		AFMT_S8},
  {"U8",		AFMT_U8},
  {"S16_LE",		AFMT_S16_LE},
  {"S16_BE",		AFMT_S16_BE},
  {"S24_LE",		AFMT_S24_LE},
  {"S24_BE",		AFMT_S24_BE},
  {"S32_LE",		AFMT_S32_LE},
  {"S32_BE",		AFMT_S32_BE},
  {"MU_LAW",		AFMT_MU_LAW},
  {"S24_PACKED",	AFMT_S24_PACKED},
  {NULL,		0}
};

static const container_t container_a[] = {
  {"WAV",		WAVE_FILE,	AFMT_S16_LE,	2,	48000},
  {"AU",		AU_FILE,	AFMT_S8,	1,	8000},
  {"RAW",		RAW_FILE,	AFMT_S16_LE,	2,	44100},
  {NULL,		0}
};

static void fatal_signal (int);
static void find_devname (char *, const char *);
static void intr (int);
static int select_container (const char *);
static int select_format (const char *);
static void select_recsrc (const char *);
static void usage (const char *);

static int
select_container (const char * optstr)
{
/*
 * Handling of the -F command line option (force container format).
 *
 * Empty or "?" shows the supported container format names.
 */
  int i;

  if ((!strcmp(optstr, "?")) || (*optstr == '\0'))
    {
      fprintf (stdout, "\nSupported format names are:\n\n");
      for (i = 0; container_a[i].name != NULL; i++)
        fprintf (stdout, "%s ", container_a[i].name);
      fprintf (stdout, "\n");
      exit (0);
    }

  for (i = 0; container_a[i].name != NULL; i++)
    if (!strcasecmp(container_a[i].name, optstr))
      return container_a[i].type;

  fprintf (stderr, "Unsupported container format name '%s'!\n", optstr);
  exit (-1);
}

static int
select_format (const char * optstr)
{
/*
 * Handling of the -f command line option (force output format).
 *
 * Empty or "?" shows the supported format names.
 */
  int i;

  if ((!strcmp(optstr, "?")) || (*optstr == '\0'))
    {
      fprintf (stdout, "\nSupported format names are:\n\n");
      for (i = 0; format_a[i].name != NULL; i++)
        fprintf (stdout, "%s ", format_a[i].name);
      fprintf (stdout, "\n");
      exit (0);
    }

  for (i = 0; format_a[i].name != NULL; i++)
    if (!strcasecmp(format_a[i].name, optstr))
      return format_a[i].fmt;

  fprintf (stderr, "Unsupported format name '%s'!\n", optstr);
  exit (-1);
}

static void
select_recsrc (const char * srcname)
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

void
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

static void
intr (int i)
{
  if (verbose)
    {
      decorate (NULL, 0, 0);
      fprintf (stderr, "\nStopped (%d).\n", i);
    }
  fprintf (stderr, "\n");
  fflush (stderr);
  end ();
  if (*script)
    {
      fprintf (stderr,
               "Waiting for the '%s' script(s) to finish - please stand by\n",
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
      decorate (NULL, 0, 0);
      fprintf (stderr, "\nStopped.(signal %d)\n", i);
    }
  end ();
  if (*script)
    {
      fprintf (stderr,
               "Waiting for the '%s' script(s) to finish - please stand by\n",
               script);
      while (wait (NULL) != -1);
    }
  exit (0);
}

static void
find_devname (char * devname, const char * num)
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
usage (const char * prog)
{
  fprintf (stdout, "Usage: %s [options] filename\n", prog?prog:"ossrecord");
  fprintf (stdout, "  Options:  -v             Verbose output.\n");
  fprintf (stdout, "            -d<device>     Change input device.\n");
  fprintf (stdout, "            -c<channels>   Change number of channels\n");
  fprintf (stdout, "            -L<level>      Change recording level.\n");
  fprintf (stdout,
           "            -g<gain>       Change gain percentage.\n");
  fprintf (stdout, "            -s<rate>       Change recording rate.\n");
  fprintf (stdout, "            -f<fmt|?>      Change/Query sample format.\n");
  fprintf (stdout,
           "            -F<cnt|?>      Change/Query container format.\n");
  fprintf (stdout, "            -l             Display level meters.\n");
  fprintf (stdout,
           "            -i<recsrc|?>   Select/Query recording source.\n");
  fprintf (stdout,
           "            -m<nfiles>     Repeat recording <nfiles> times.\n");
  fprintf (stdout,
           "            -r<command>    Run <command> after recording.\n");
  fprintf (stdout,
           "            -t<maxsecs>    Record no more than <maxsecs> in a"
           " single recording.\n");
  exit (-1);
}

int
main (int argc, char * argv[])
{
  int c;
  extern char * optarg;
  extern int optind;

  if (argc < 2)
    usage (argv[0]);

  while ((c = getopt (argc, argv, "F:L:MRSb:c:d:f:g:hi:lm:r:s:t:wv")) != EOF)
    switch (c)
      {
        case 'F':
          type = select_container (optarg);
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

        case 'M':
          channels = 1;
          break;

        case 'R':
          raw_mode = 1;
          break;

        case 'S':
          channels = 2;
          break;

        case 'b':
          c = atoi (optarg);
          c += c % 8;
          switch (c)
            {
              case 8: format = AFMT_U8; break;
              case 16: format = AFMT_S16_LE; break;
              case 24: format = AFMT_S24_LE; break;
              case 32: format = AFMT_S32_LE; break;
              default:
                fprintf (stderr, "Error: Unsupported number of bits %d\n", c);
                exit (-1);
            }
          break;

        case 'c':
          channels = atoi (optarg);
          break;

        case 'd':
          if (*optarg >= '0' && *optarg <= '9')
            /* Just the device number was given */
            find_devname (dspdev, optarg);
          else
            snprintf (dspdev, sizeof(dspdev), "%s", optarg);
          break;

        case 'f':
          format = select_format (optarg);
          break;

        case 'g':
          amplification = atoi (optarg);
          if (amplification == 0) usage (argv[0]);

        case 'l':
          level_meters = 1;
          break;

        case 'i':
          recsrc = optarg;
          break;

        case 'm':
          nfiles = atoi (optarg);
          break;

        case 's':
          speed = atoi (optarg);
          if (speed == 0)
            {
              fprintf (stderr, "Bad sampling rate given\n");
              exit (-1);
            }
          if (speed < 1000) speed *= 1000;
          break;

        case 'r':
          c = snprintf (script, sizeof (script), "%s", optarg);
          if ((c >= sizeof (script)) || (c < 0))
            {
              fprintf (stderr, "-r argument is too long!\n");
              exit (-1);
            }
          break;

        case 't':
          datalimit = atoi (optarg);
          break;

        case 'w':
          break;

        case 'v':
          verbose = 1;
          break;

        case 'h':
        default:
          usage (argv[0]);
      }

  if (optind != argc - 1)	/* No file or multiple file names given */
    {
      usage (argv[0]);
      exit (-1);
    }

  if (format == -1) format = container_a[type].dformat;
  if (channels == -1) channels = container_a[type].dchannels;
  if (speed == -1) speed = container_a[type].dspeed;
  switch (format)
    {
      case AFMT_U8:
      case AFMT_S16_NE:
      case AFMT_S24_NE:
      case AFMT_S32_NE: break;
      default: level_meters = 0; /* Not implemented */
    }

  signal (SIGSEGV, fatal_signal);
  signal (SIGPIPE, fatal_signal);
  signal (SIGINT, intr);

  open_audio ();

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
