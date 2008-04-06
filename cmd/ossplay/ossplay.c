/*
 * Purpose: Sources for the ossplay audio player shipped with OSS
 *
 * Description:
 * This is a audio file player that supports most commonly used uncompressed
 * audio formats (.wav, .snd, .au, .aiff). It doesn't play compressed formats
 * such as MP3.
 *
 * This program is bit old and it uses some OSS features that may no longer be
 * required.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include "ossplay.h"
#include "decode.h"
#include "parser.h"

#include <signal.h>

int force_speed = -1, force_fmt = 0, force_channels = -1, amplification = 100;
int audiofd = 0, quitflag = 0, quiet = 0, verbose = 0;
int raw_mode = 0, exitstatus = 0, loop = 0;
char audio_devname[32] = "/dev/dsp";
char current_songname[64] = "";

static int prev_speed = 0, prev_fmt = 0, prev_channels = 0;
static char *playtgt = NULL;

static const format_t format_a[] = {
  {"S8",		AFMT_S8,		AFMT_S16_NE},
  {"U8",		AFMT_U8,		AFMT_S16_NE},
  {"S16_LE",		AFMT_S16_LE,
#if AFMT_S16_LE == AFMT_S16_OE
  AFMT_S16_BE},
#else
  0},
#endif
  {"S16_BE",		AFMT_S16_BE,
#if AFMT_S16_BE == AFMT_S16_OE
  AFMT_S16_LE},
#else
  0},
#endif
  {"U16_LE",		AFMT_U16_LE,		0},
  {"U16_BE",		AFMT_U16_BE,		0},
  {"S24_LE",		AFMT_S24_LE,		0},
  {"S24_BE",		AFMT_S24_BE,		0},
  {"S32_LE",		AFMT_S32_LE,
#if AFMT_S32_LE == AFMT_S32_OE
  AFMT_S32_BE},
#else
  0},
#endif
  {"S32_BE",		AFMT_S32_BE,
#if AFMT_S32_BE == AFMT_S32_OE
  AFMT_S32_LE},
#else
  0},
#endif
  {"A_LAW",		AFMT_A_LAW,		AFMT_S16_NE},
  {"MU_LAW",		AFMT_MU_LAW,		AFMT_S16_NE},
  {"IMA_ADPCM",		AFMT_IMA_ADPCM,		0},
  {"MS_ADPCM",		AFMT_MS_ADPCM,		0},
  {"CR_ADPCM_2",	AFMT_CR_ADPCM_2,	0},
  {"CR_ADPCM_3",	AFMT_CR_ADPCM_3,	0},
  {"CR_ADPCM_4",	AFMT_CR_ADPCM_4,	0},
  {"FLOAT",		AFMT_FLOAT,		0},
  {"S24_PACKED",	AFMT_S24_PACKED,	0},
  {"SPDIF_RAW",		AFMT_SPDIF_RAW,		0},
  {"FIBO_DELTA",	AFMT_FIBO_DELTA,	0},
  {"EXP_DELTA",		AFMT_EXP_DELTA,		0},
  {NULL,		0,			0}
};

static void cleanup (void);
static void describe_error (void);
static void find_devname (char *, const char *);
static int select_format (const char *);
static void select_playtgt (const char *);
static void open_device (void);
static void usage (const char *);

const char *
filepart (const char *name)
{
  const char * s = name;

  while (*name)
    {
      if (name[0] == '/' && name[1] != '\0')
	s = name + 1;
      name++;
    }

  return s;
}

int
le_int (const unsigned char * p, int l)
{
  int i, val;

  val = 0;

  for (i = l - 1; i >= 0; i--)
    {
      val = (val << 8) | p[i];
    }

  return val;
}

int
be_int (const unsigned char * p, int l)
{
  int i, val;

  val = 0;

  for (i = 0; i < l; i++)
    {
      val = (val << 8) | p[i];
    }

  return val;
}

static void
usage (const char * prog)
{
  print_msg (HELPM, "Usage: %s [options...] filename...\n", prog);
  print_msg (HELPM, "  Options:  -v             Verbose output.\n");
  print_msg (HELPM, "            -q             No informative printouts.\n");
  print_msg (HELPM, "            -d<devname>    Change output device.\n");
  print_msg (HELPM, "            -g<gain>       Change gain.\n");
  print_msg (HELPM, "            -s<rate>       Change playback rate.\n");
  print_msg (HELPM, "            -f<fmt>|?      Change/Query input format.\n");
  print_msg (HELPM, "            -c<channels>   Change number of channels.\n");
  print_msg (HELPM, "            -o<playtgt>|?  Select/Query output target.\n");
  print_msg (HELPM, "            -l             Loop playback indefinitely.\n");
  print_msg (HELPM,
             "            -R             Open sound device in raw mode.\n");
  exit (-1);
}

static void
describe_error (void)
{
  switch (errno)
    {
    case ENXIO:
    case ENODEV:
      print_msg (ERRORM, "\nThe device file was found in /dev but\n"
	         "there is no driver for it currently loaded.\n"
	         "\n"
	         "You can start it by executing the /usr/local/bin/soundon\n"
	         "command as super user (root).\n");
      break;

    case ENOSPC:
      print_msg (ERRORM, "\nThe soundcard driver was not installed\n"
	         "properly. The system is out of DMA compatible memory.\n"
	         "Please reboot your system and try again.\n");

      break;

    case ENOENT:
      print_msg (ERRORM, "\nThe sound device file is missing from /dev.\n"
	         "You should try re-installing OSS.\n");
      break;

    case EBUSY:
      print_msg (ERRORM,
	         "\nThere is some other application using this audio device.\n"
	         "Exit it and try again.\n");
      print_msg (ERRORM,
	         "You can possibly find out the conflicting application by"
                 "looking\n",
	         "at the printout produced by command 'ossinfo -a -v1'\n");
      break;

    default:;
    }
}

static void
open_device (void)
{
  int flags = O_WRONLY;

  if (raw_mode)
    flags |= O_EXCL;		/* Disable redirection to the virtual mixer */

  if ((audiofd = open (audio_devname, flags, 0)) == -1)
    {
      perror_msg (audio_devname);
      describe_error ();
      exit (-1);
    }

  atexit (cleanup);

  if (raw_mode)
    {
      /*
       * Disable sample rate/format conversions.
       */
      int tmp = 0;
      ioctl (audiofd, SNDCTL_DSP_COOKEDMODE, &tmp);
    }
}

static void
find_devname (char * devname, const char * num)
{
/*
 * OSS 4.0 the audio device numbering may be different from the
 * legacy /dev/dsp# numbering reported by /dev/sndstat. Try to find the
 * device name (devnode) that matches the given device number.
 *
 * Prior versions of ossplay simply used the the /dev/dsp# number.
 */
  int dev;
  int mixer_fd;
  oss_audioinfo ai;

  if (sscanf (num, "%d", &dev) != 1)
    {
      print_msg (ERRORM, "Invalid audio device number '%s'\n", num);
      exit (-1);
    }

  if ((mixer_fd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      perror_msg ("/dev/mixer");
      print_msg (WARNM, "Warning: Defaulting to /dev/dsp%s\n", num);
      snprintf (devname, sizeof (devname), "/dev/dsp%s", num);
      return;
    }

  ai.dev = dev;

  if (ioctl (mixer_fd, SNDCTL_AUDIOINFO, &ai) == -1)
    {
      perror_msg ("/dev/mixer SNDCTL_AUDIOINFO");
      print_msg (WARNM, "Warning: Defaulting to /dev/dsp%s\n", num);
      snprintf (devname, sizeof (devname), "/dev/dsp%s", num);
      close (mixer_fd);
      return;
    }

  strncpy (devname, ai.devnode, sizeof (devname));

  close (mixer_fd);
}

/*ARGSUSED*/
int
setup_device (int fd, int format, int channels, int speed)
{
  int tmp;

  if (speed != prev_speed || format != prev_fmt || channels != prev_channels)
    {
#if 0
      ioctl (audiofd, SNDCTL_DSP_SYNC, NULL);
      ioctl (audiofd, SNDCTL_DSP_HALT, NULL);
#else
      close (audiofd);
      open_device ();
      if (playtgt != NULL)
	select_playtgt (playtgt);
#endif
    }

  /*
   * Report the current filename as the song name.
   */
  ioctl (audiofd, SNDCTL_SETSONG, current_songname);	/* No error checking */

  prev_speed = speed;
  prev_channels = channels;
  prev_fmt = format;

  tmp = APF_NORMAL;
  ioctl (audiofd, SNDCTL_DSP_PROFILE, &tmp);

  tmp = format;

  if (verbose > 1)
    print_msg (NORMALM, "Setup device %d/%d/%d\n", channels, format, speed);

  if (ioctl (audiofd, SNDCTL_DSP_SETFMT, &tmp) == -1)
    {
      perror_msg (audio_devname);
      print_msg (ERRORM, "Failed to select bits/sample\n");
      return 0;
    }

  if (tmp != format)
    {
      int i;

      print_msg (ERRORM, "%s doesn't support this audio format (%x/%x).\n",
                 audio_devname, format, tmp);
      for (i = 0; format_a[i].name != NULL; i++)
        if (format_a[i].fmt == format)
          {
            tmp = format_a[i].may_conv;
            if (tmp == 0) return 0;
            print_msg (WARNM, "Converting to format %x\n", tmp);
            return setup_device (fd, tmp, channels, speed);
          }
      return 0;
    }

  tmp = channels;

  if (ioctl (audiofd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
    {
      perror_msg (audio_devname);
      print_msg (ERRORM, "Failed to select number of channels.\n");
      return 0;
    }

  if (tmp != channels)
    {
      print_msg (ERRORM, "%s doesn't support %d channels.\n",
	         audio_devname, channels);
      return 0;
    }

  tmp = speed;

  if (ioctl (audiofd, SNDCTL_DSP_SPEED, &tmp) == -1)
    {
      perror_msg (audio_devname);
      print_msg (ERRORM, "Failed to select sampling rate.\n");
      return 0;
    }

  if (tmp != speed)
    {
      print_msg (WARNM, "Warning: Playback using %d Hz (file %d Hz)\n",
	         tmp, speed);
    }

  return format;
}

void 
print_verbose (int format, int channels, int speed)
{
  char chn[32], *fmt = "";

  if (channels == 1)
    strcpy (chn, "mono");
  else if (channels == 2)
    strcpy (chn, "stereo");
  else
    snprintf (chn, sizeof(chn), "%d channels", channels);

  switch (format)
    {
       case AFMT_QUERY: fmt = "Invallid format"; break;
       case AFMT_IMA_ADPCM: fmt = "ADPCM"; break;
       case AFMT_MS_ADPCM: fmt = "MS-ADPCM"; break;
       case AFMT_MU_LAW: fmt = "mu-law"; break;
       case AFMT_A_LAW: fmt = "A-law"; break;
       case AFMT_U8:
       case AFMT_S8: fmt = "8 bits"; break;
       case AFMT_S16_LE:
       case AFMT_S16_BE:
       case AFMT_U16_LE:
       case AFMT_U16_BE: fmt = "16 bits"; break;
       case AFMT_S24_LE:
       case AFMT_S24_BE:
       case AFMT_S24_PACKED: fmt = "24 bits"; break;
       case AFMT_SPDIF_RAW:
       case AFMT_S32_LE:
       case AFMT_S32_BE: fmt = "32 bits"; break;
       case AFMT_FLOAT: fmt = "float"; break;
       case AFMT_VORBIS: fmt = "vorbis"; break;
       case AFMT_MPEG: fmt = "mpeg"; break;
       case AFMT_FIBO_DELTA: fmt = "fibonacci delta"; break;
       case AFMT_EXP_DELTA: fmt = "exponential delta"; break;
    }
  print_msg (NORMALM, "%s/%s/%d Hz\n", fmt, chn, speed);
}

static void
select_playtgt (const char * playtgt)
{
/*
 * Handling of the -o command line option (playback target selection).
 *
 * Empty or "?" shows the available playback sources.
 */
  int i, src;
  oss_mixer_enuminfo ei;

  if (ioctl (audiofd, SNDCTL_DSP_GET_PLAYTGT_NAMES, &ei) == -1)
    {
      perror_msg ("SNDCTL_DSP_GET_PLAYTGT_NAMES");
      exit (-1);
    }

  if (ioctl (audiofd, SNDCTL_DSP_GET_PLAYTGT, &src) == -1)
    {
      perror_msg ("SNDCTL_DSP_GET_PLAYTGT");
      exit (-1);
    }

  if (*playtgt == 0 || strcmp (playtgt, "?") == 0)
    {
      print_msg (STARTM,
                 "\nPossible playback targets for the selected device:\n\n");

      for (i = 0; i < ei.nvalues; i++)
	{
	  print_msg (CONTM, "\t%s", ei.strings + ei.strindex[i]);
	  if (i == src)
	    print_msg (CONTM, " (currently selected)");
	  print_msg (CONTM, "\n");
	}
      print_msg (ENDM, "\n");
      exit (0);
    }

  for (i = 0; i < ei.nvalues; i++)
    {
      char *s = ei.strings + ei.strindex[i];
      if (strcmp (s, playtgt) == 0)
	{
	  src = i;
	  if (ioctl (audiofd, SNDCTL_DSP_SET_PLAYTGT, &src) == -1)
	    {
	      perror_msg ("SNDCTL_DSP_SET_PLAYTGT");
	      exit (-1);
	    }

	  return;
	}
    }

  print_msg (ERRORM,
	     "Unknown playback target name '%s' - use -o? to get the list\n",
	     playtgt);
  exit (-1);
}

static int
select_format (const char * optstr)
{
/*
 * Handling of the -f command line option (force input format).
 *
 * Empty or "?" shows the supported format names.
 */
  int i;

  if ((*optstr == '?') || (*optstr == '\0'))
    {
      print_msg (STARTM, "Supported format names are:\n");
      for (i = 0; format_a[i].name != NULL; i++)
        print_msg (CONTM, "%s ", format_a[i].name);
      print_msg (ENDM, "\n");
      exit (0);
    }

  for (i = 0; format_a[i].name != NULL; i++)
    if (!strcmp(format_a[i].name, optstr))
      return format_a[i].fmt;

  print_msg (ERRORM, "Unsupported format name '%s'!\n", optstr);
  exit (-1);
}

static void get_int (int signum)
{
#if 0
  if (quitflag == 1)
    {
#ifdef SIGQUIT
      signal (SIGQUIT, SIG_DFL);
      kill (getpid(), SIGQUIT);
#endif
    } 
#endif
  quitflag = 1;
}

static void cleanup (void)
{
  close (audiofd);
}

int
parse_opts (int argc, char ** argv)
{
  char * prog;
  extern int optind;
  int c;

  prog = argv[0];

  while ((c = getopt (argc, argv, "Rc:d:f:g:hlo:qs:v")) != EOF)
    {
      switch (c)
	{
	case 'v':
	  verbose++;
	  break;

	case 'R':
	  raw_mode = 1;
	  break;

	case 'q':
	  quiet++;
	  verbose = 0;
	  break;

	case 'd':
	  if (*optarg >= '0' && *optarg <= '9')	/* Only device number given */
	    find_devname (audio_devname, optarg);
	  else
	    strncpy (audio_devname, optarg, sizeof (audio_devname));
          audio_devname [sizeof (audio_devname)-1] = '\0';
	  break;

	case 'o':
	  playtgt = optarg;
	  break;

#ifdef MPEG_SUPPORT
	case 'm':
	  mpeg_enabled = 1;
	  break;
#endif

	case 'f':
	  force_fmt = select_format (optarg);
	  break;

	case 's':
	  sscanf (optarg, "%d", &force_speed);
	  break;

	case 'c':
	  sscanf (optarg, "%d", &force_channels);
	  break;

	case 'g':
	  sscanf (optarg, "%d", &amplification);
	  break;

        case 'l':
          loop = 1;
          break;

	default:
	  usage (prog);
	}

    }

  argc -= optind - 1;

  open_device ();

  if (playtgt != NULL)
    select_playtgt (playtgt);

  if (argc < 2)
    usage (prog);

#ifdef SIGQUIT
  signal (SIGQUIT, get_int);
#endif

  return optind;
}
