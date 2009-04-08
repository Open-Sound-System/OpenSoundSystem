/*
 * Purpose: Sources for the ossplay audio player and for the ossrecord
 *          audio recorder shipped with OSS.
 *
 * Description:
 * OSSPlay is a audio file player that supports most commonly used uncompressed
 * audio formats (.wav, .snd, .au, .aiff). It doesn't play compressed formats
 * such as MP3.
 * OSSRecord is a simple file recorder. It can write simple file formats
 * (.wav, .au, .aiff).
 *
 * This file contains the audio backend and misc. functions.
 *
 * This program is bit old and it uses some OSS features that may no longer be
 * required.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include "ossplay_decode.h"
#include "ossplay_parser.h"
#include "ossplay_wparser.h"

#include <signal.h>
#include <strings.h>
#include <unistd.h>

unsigned int amplification = 100;
int eflag = 0, force_speed = 0, force_fmt = 0, force_channels = 0, verbose = 0;
flag from_stdin = 0, int_conv = 0, level_meters = 0, loop = 0, overwrite = 0,
     quiet = 0, raw_file = 0, raw_mode = 0;
double seek_time = 0;
off_t (*ossplay_lseek) (int, off_t, int) = lseek;

char script[512] = "";
unsigned int nfiles = 1;
unsigned long long datalimit = 0;
fctypes_t type = WAVE_FILE;

static void describe_error (void);
static void find_devname (char *, const char *);
static fctypes_t select_container (const char *);
static int select_format (const char *, int);
static void ossplay_usage (const char *);
static void ossrecord_usage (const char *);
static void ossplay_getint (int);
static void print_play_verbose_info (const unsigned char *, ssize_t, void *);
static void print_record_verbose_info (const unsigned char *, ssize_t, void *);

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
	         "You can start it by executing the soundon command as\n"
	         "super user (root).\n");
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
  char *devmixer;

  if ((devmixer=getenv("OSS_MIXERDEV"))==NULL)
     devmixer = "/dev/mixer";

  if (sscanf (num, "%d", &dev) != 1)
    {
      print_msg (ERRORM, "Invalid audio device number '%s'\n", num);
      exit (E_SETUP_ERROR);
    }

  if ((mixer_fd = open (devmixer, O_RDWR, 0)) == -1)
    {
      perror_msg (devmixer);
      print_msg (WARNM, "Warning: Defaulting to /dev/dsp%s\n", num);
      snprintf (devname, OSS_DEVNODE_SIZE, "/dev/dsp%s", num);
      return;
    }

  ai.dev = dev;

  if (ioctl (mixer_fd, SNDCTL_AUDIOINFO, &ai) == -1)
    {
      perror_msg ("SNDCTL_AUDIOINFO");
      print_msg (WARNM, "Warning: Defaulting to /dev/dsp%s\n", num);
      snprintf (devname, OSS_DEVNODE_SIZE, "/dev/dsp%s", num);
      close (mixer_fd);
      return;
    }

  strncpy (devname, ai.devnode, OSS_DEVNODE_SIZE);

  close (mixer_fd);
  return;
}

const char *
filepart (const char *name)
{
  const char * s = name;

  if (name == NULL) return "";

  while (*name)
    {
      if (name[0] == '/' && name[1] != '\0')
	s = name + 1;
      name++;
    }

  return s;
}

float
format2bits (int format)
{
  switch (format)
    {
      case AFMT_CR_ADPCM_2: return 2;
      case AFMT_CR_ADPCM_3: return 2.66F;
      case AFMT_CR_ADPCM_4:
      case AFMT_MS_IMA_ADPCM_3BITS: return 3;
      case AFMT_MAC_IMA_ADPCM:
      case AFMT_MS_IMA_ADPCM:
      case AFMT_IMA_ADPCM:
      case AFMT_MS_ADPCM:
      case AFMT_FIBO_DELTA:
      case AFMT_EXP_DELTA: return 4;
      case AFMT_MU_LAW:
      case AFMT_A_LAW:
      case AFMT_U8:
      case AFMT_S8: return 8;
      case AFMT_VORBIS:
      case AFMT_MPEG:
      case AFMT_S16_LE:
      case AFMT_S16_BE:
      case AFMT_U16_LE:
      case AFMT_U16_BE: return 16;
      case AFMT_S24_PACKED:
      case AFMT_S24_PACKED_BE: return 24;
      case AFMT_S24_LE:
      case AFMT_S24_BE:
      case AFMT_SPDIF_RAW:
      case AFMT_FLOAT32_LE:
      case AFMT_FLOAT32_BE:
      case AFMT_S32_LE:
      case AFMT_S32_BE: return 32;
      case AFMT_DOUBLE64_LE:
      case AFMT_DOUBLE64_BE: return 64;
      case AFMT_FLOAT: return sizeof (float) * 8;
      case AFMT_QUERY:
      default: return 0;
   }
}

void
close_device (dspdev_t * dsp)
{
  if (dsp->fd == -1) return;
  close (dsp->fd);
  dsp->fd = -1;
}

void
open_device (dspdev_t * dsp)
{
  char *devdsp;

  if (dsp->fd >= 0)
     close_device (dsp);

  dsp->format = 0; dsp->channels = 0; dsp->speed = 0;

  if ((devdsp=getenv("OSS_AUDIODEV"))==NULL)
     devdsp = "/dev/dsp";

  if (raw_mode)
    dsp->flags |= O_EXCL;	/* Disable redirection to the virtual mixer */

  if (dsp->dname[0] == '\0') strcpy (dsp->dname, devdsp);

  if ((dsp->fd = open (dsp->dname, dsp->flags, 0)) == -1)
    {
      perror_msg (dsp->dname);
      describe_error ();
      exit (E_SETUP_ERROR);
    }

  if (raw_mode)
    {
      /*
       * Disable sample rate/format conversions.
       */
      int tmp = 0;
      ioctl (dsp->fd, SNDCTL_DSP_COOKEDMODE, &tmp);
    }
}

static void
ossplay_usage (const char * prog)
{
  print_msg (HELPM, "Usage: %s [options] filename(s)\n", prog?prog:"ossplay");
  print_msg (HELPM, "  Options:  -v             Verbose output.\n");
  print_msg (HELPM, "            -q             No informative printouts.\n");
  print_msg (HELPM, "            -d<devname>    Change output device.\n");
  print_msg (HELPM, "            -g<gain>       Change gain.\n");
  print_msg (HELPM, "            -s<rate>       Change playback rate.\n");
  print_msg (HELPM, "            -f<fmt>|?      Change/Query input format.\n");
  print_msg (HELPM, "            -c<channels>   Change number of channels.\n");
  print_msg (HELPM, "            -o<playtgt>|?  Select/Query output target.\n");
  print_msg (HELPM, "            -l             Loop playback indefinitely.\n");
  print_msg (HELPM, "            -F             Treat all input as raw PCM.\n");
  print_msg (HELPM, "            -S<secs>       Start playing from offset.\n");
  print_msg (HELPM,
             "            -R             Open sound device in raw mode.\n");
  exit (E_USAGE);
}

static void
ossrecord_usage (const char * prog)
{
  print_msg (HELPM, "Usage: %s [options] filename\n", prog?prog:"ossrecord");
  print_msg (HELPM, "  Options:  -v             Verbose output.\n");
  print_msg (HELPM, "            -d<device>     Change input device.\n");
  print_msg (HELPM, "            -c<channels>   Change number of channels\n");
  print_msg (HELPM, "            -L<level>      Change recording level.\n");
  print_msg (HELPM,
             "            -g<gain>       Change gain percentage.\n");
  print_msg (HELPM, "            -s<rate>       Change recording rate.\n");
  print_msg (HELPM, "            -f<fmt|?>      Change/Query sample format.\n");
  print_msg (HELPM,
             "            -F<cnt|?>      Change/Query container format.\n");
  print_msg (HELPM, "            -l             Display level meters.\n");
  print_msg (HELPM,
             "            -i<recsrc|?>   Select/Query recording source.\n");
  print_msg (HELPM,
             "            -m<nfiles>     Repeat recording <nfiles> times.\n");
  print_msg (HELPM,
             "            -r<command>    Run <command> after recording.\n");
  print_msg (HELPM,
             "            -t<maxsecs>    Record no more than <maxsecs> in a"
             " single recording.\n");
  print_msg (HELPM,
             "            -R             Open sound device in raw mode.\n");
  print_msg (HELPM, "            -O             Allow overwrite.\n");
  exit (E_USAGE);
}

const char *
sample_format_name (int sformat)
{
  int i;

  for (i = 0; format_a[i].fmt != 0; i++)
    if (format_a[i].fmt == sformat)
      return format_a[i].name;

  return "";
}

static fctypes_t
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
      print_msg (STARTM, "\nSupported container format names are:\n\n");
      for (i = 0; container_a[i].name != NULL; i++)
        print_msg (CONTM, "%s ", container_a[i].name);
      print_msg (ENDM, "\n");
      exit (0);
    }

  for (i = 0; container_a[i].name != NULL; i++)
    if (!strcasecmp(container_a[i].name, optstr))
      return container_a[i].type;

  print_msg (ERRORM, "Unsupported container format name '%s'!\n", optstr);
  exit (E_USAGE);
}

static int
select_format (const char * optstr, int dir)
{
/*
 * Handling of the -f command line option (force input format).
 *
 * Empty or "?" shows the supported format names.
 */
  int i;

  if ((!strcmp(optstr, "?")) || (*optstr == '\0'))
    {
      print_msg (STARTM, "\nSupported format names are:\n\n");
      for (i = 0; format_a[i].name != NULL; i++)
        if (dir & format_a[i].dir)
          print_msg (CONTM, "%s ", format_a[i].name);
      print_msg (ENDM, "\n");
      exit (0);
    }

  for (i = 0; format_a[i].name != NULL; i++)
    if ((format_a[i].dir & dir) && (!strcasecmp(format_a[i].name, optstr)))
      return format_a[i].fmt;

  print_msg (ERRORM, "Unsupported format name '%s'!\n", optstr);
  exit (E_USAGE);
}

void
select_playtgt (dspdev_t * dsp)
{
/*
 * Handling of the -o command line option (playback target selection).
 *
 * Empty or "?" shows the available playback sources.
 */
  int i, src;
  oss_mixer_enuminfo ei;

  if (ioctl (dsp->fd, SNDCTL_DSP_GET_PLAYTGT_NAMES, &ei) == -1)
    {
      perror_msg ("SNDCTL_DSP_GET_PLAYTGT_NAMES");
      exit (E_SETUP_ERROR);
    }

  if (ioctl (dsp->fd, SNDCTL_DSP_GET_PLAYTGT, &src) == -1)
    {
      perror_msg ("SNDCTL_DSP_GET_PLAYTGT");
      exit (E_SETUP_ERROR);
    }

  if ((dsp->playtgt[0] == '\0') || (strcmp (dsp->playtgt, "?") == 0))
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
      if (strcmp (s, dsp->playtgt) == 0)
	{
	  src = i;
	  if (ioctl (dsp->fd, SNDCTL_DSP_SET_PLAYTGT, &src) == -1)
	    {
	      perror_msg ("SNDCTL_DSP_SET_PLAYTGT");
	      exit (E_SETUP_ERROR);
	    }

	  return;
	}
    }

  print_msg (ERRORM,
	     "Unknown playback target name '%s' - use -o? to get the list\n",
	     dsp->playtgt);
  exit (E_USAGE);
}

void
select_recsrc (dspdev_t * dsp)
{
/*
 * Handling of the -i command line option (recording source selection).
 *
 * Empty or "?" shows the available recording sources.
 */
  int i, src;
  oss_mixer_enuminfo ei;

  if (ioctl (dsp->fd, SNDCTL_DSP_GET_RECSRC_NAMES, &ei) == -1)
    {
      perror_msg ("SNDCTL_DSP_GET_RECSRC_NAMES");
      exit (E_SETUP_ERROR);
    }

  if (ioctl (dsp->fd, SNDCTL_DSP_GET_RECSRC, &src) == -1)
    {
      perror_msg ("SNDCTL_DSP_GET_RECSRC");
      exit (E_SETUP_ERROR);
    }

  if (dsp->recsrc[0] == '\0' || strcmp (dsp->recsrc, "?") == 0)
    {
      print_msg (STARTM,
                 "\nPossible recording sources for the selected device:\n\n");

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
      if (strcmp (s, dsp->recsrc) == 0)
	{
	  src = i;
	  if (ioctl (dsp->fd, SNDCTL_DSP_SET_RECSRC, &src) == -1)
	    {
	      perror_msg ("SNDCTL_DSP_SET_RECSRC");
	      exit (E_SETUP_ERROR);
	    }
	  return;
	}
    }

  print_msg (ERRORM,
	     "Unknown recording source name '%s' - use -i? to get the list\n",
	     dsp->recsrc);
  exit (E_USAGE);
}

int
setup_device (dspdev_t * dsp, int format, int channels, int speed)
{
  int tmp;

  if (dsp->speed != speed || dsp->format != format ||
      dsp->channels != channels || dsp->fd == -1)
    {
#if 0
      ioctl (dsp->fd, SNDCTL_DSP_SYNC, NULL);
      ioctl (dsp->fd, SNDCTL_DSP_HALT, NULL);
#else
      close_device (dsp);
      open_device (dsp);
      if (dsp->playtgt != NULL)	select_playtgt (dsp);
      if (dsp->recsrc != NULL)	select_recsrc (dsp);
#endif
    }
  else
    {
      ioctl (dsp->fd, SNDCTL_SETSONG, dsp->current_songname);
      return 0;
    }

  /*
   * Report the current filename as the song name.
   */
  ioctl (dsp->fd, SNDCTL_SETSONG, dsp->current_songname);

  tmp = APF_NORMAL;
  ioctl (dsp->fd, SNDCTL_DSP_PROFILE, &tmp);

  tmp = format;

  if (verbose > 1)
    print_msg (NORMALM, "Setup device %s/%d/%d\n", sample_format_name (format),
               channels, speed);

  if (ioctl (dsp->fd, SNDCTL_DSP_SETFMT, &tmp) == -1)
    {
      perror_msg (dsp->dname);
      print_msg (ERRORM, "Failed to select bits/sample\n");
      return E_SETUP_ERROR;
    }

  if (tmp != format)
    {
      print_msg (ERRORM, "%s doesn't support this audio format (%x/%x).\n",
                 dsp->dname, format, tmp);
      return E_FORMAT_UNSUPPORTED;
    }

  tmp = channels;

  if (ioctl (dsp->fd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
    {
      perror_msg (dsp->dname);
      print_msg (ERRORM, "Failed to select number of channels.\n");
      return E_SETUP_ERROR;
    }

  if (tmp != channels)
    {
      print_msg (ERRORM, "%s doesn't support %d channels (%d).\n",
	         dsp->dname, channels, tmp);
      return E_CHANNELS_UNSUPPORTED;
    }

  tmp = speed;

  if (ioctl (dsp->fd, SNDCTL_DSP_SPEED, &tmp) == -1)
    {
      perror_msg (dsp->dname);
      print_msg (ERRORM, "Failed to select sampling rate.\n");
      return E_SETUP_ERROR;
    }

  if (tmp != speed)
    {
      print_msg (WARNM, "Warning: Playback using %d Hz (file %d Hz)\n",
	         tmp, speed);
    }

  dsp->speed = speed;
  dsp->channels = channels;
  dsp->format = format;

  if (dsp->reclevel != 0)
    {
      tmp = dsp->reclevel | (dsp->reclevel << 8);

      if (ioctl (dsp->fd, SNDCTL_DSP_SETRECVOL, &tmp) == -1)
        perror ("SNDCTL_DSP_SETRECVOL");
    }

  return 0;
}

static void
ossplay_getint (int signum)
{
#if 0
  if (eflag == signum + 128)
    {
      signal (signum, SIG_DFL);
      kill (getpid(), signum);
    }
#endif
  eflag = signum + 128;
}

off_t
ossplay_lseek_stdin (int fd, off_t off, int w)
{
  off_t i;
  ssize_t bytes_read;
  char buf[BUFSIZ];

  if (w == SEEK_END) return -1;
  if (off < 0) return -1;
  if (off == 0) return 0;
  i = off;
  while (i > 0)
    {
      bytes_read = read(fd, buf, (i > BUFSIZ)?BUFSIZ:i);
      if (bytes_read == -1) return -1;
      else if (bytes_read == 0) return off;
      i -= bytes_read;
    }
  return off;
}

int
ossplay_parse_opts (int argc, char ** argv, dspdev_t * dsp)
{
  extern char * optarg;
  extern int optind;
  int c;

  while ((c = getopt (argc, argv, "FRS:c:d:f:g:hlo:qs:v")) != EOF)
    {
      switch (c)
	{
	case 'v':
	  verbose++;
	  quiet = 0;
	  int_conv = 2;
	  break;

	case 'R':
	  raw_mode = 1;
	  break;

	case 'q':
	  quiet++;
	  verbose = 0;
	  if (int_conv == 2) int_conv = 0;
	  break;

	case 'd':
	  if (*optarg >= '0' && *optarg <= '9')	/* Only device number given */
	    find_devname (dsp->dname, optarg);
	  else
            snprintf (dsp->dname, OSS_DEVNODE_SIZE, "%s", optarg);
	  break;

	case 'o':
          if (!strcmp(optarg, "?"))
            {
              dsp->playtgt = optarg;
              dsp->flags = O_WRONLY;
              open_device (dsp);
              select_playtgt (dsp);
            }
	  dsp->playtgt = optarg;
	  break;

#ifdef MPEG_SUPPORT
	case 'm':
	  mpeg_enabled = 1;
	  break;
#endif

	case 'f':
	  force_fmt = select_format (optarg, CP);
	  break;

	case 's':
	  sscanf (optarg, "%d", &force_speed);
	  break;

	case 'c':
	  sscanf (optarg, "%d", &force_channels);
	  break;

	case 'g':
	  sscanf (optarg, "%u", &amplification);
	  int_conv = 1;
	  break;

        case 'l':
          loop = 1;
          break;

	case 'F':
	  raw_file = 1;
	  break;

	case 'S':
	  sscanf (optarg, "%lf", &seek_time);
	  if (seek_time < 0) seek_time = 0;
	  break;

	default:
	  ossplay_usage (argv[0]);
	}

    }

  if (argc < optind + 1)
    ossplay_usage (argv[0]);

#ifdef SIGQUIT
  signal (SIGQUIT, ossplay_getint);
#endif
  return optind;
}

int
ossrecord_parse_opts (int argc, char ** argv, dspdev_t * dsp)
{
  int c;
  extern char * optarg;
  extern int optind;

  if (argc < 2)
    ossrecord_usage (argv[0]);

  while ((c = getopt (argc, argv, "F:L:MORSb:c:d:f:g:hi:lm:r:s:t:wv")) != EOF)
    switch (c)
      {
        case 'F':
          type = select_container (optarg);
          break;

        case 'L':
          dsp->reclevel = atoi (optarg);
          if (dsp->reclevel < 1 || dsp->reclevel > 100)
            {
              print_msg (ERRORM, "%s: Bad recording level '%s'\n", argv[0],
                         optarg);
              exit (-1);
            }
          break;

        case 'M':
          force_channels = 1;
          break;

        case 'R':
          raw_mode = 1;
          break;

        case 'S':
          force_channels = 2;
          break;

        case 'b':
          c = atoi (optarg);
          c += c % 8; /* WAV format always pads to a multiple of 8 */ 
          switch (c)
            {
              case 8: force_fmt = AFMT_U8; break;
              case 16: force_fmt = AFMT_S16_LE; break;
              case 24: force_fmt = AFMT_S24_PACKED; break;
              case 32: force_fmt = AFMT_S32_LE; break;
              default:
                print_msg (ERRORM, "Error: Unsupported number of bits %d\n", c);
                exit (E_FORMAT_UNSUPPORTED);
            }
          break;

        case 'c':
          sscanf (optarg, "%d", &force_channels);
          break;

        case 'd':
	  if (*optarg >= '0' && *optarg <= '9')	/* Only device number given */
	    find_devname (dsp->dname, optarg);
	  else
            snprintf (dsp->dname, OSS_DEVNODE_SIZE, "%s", optarg);
          break;

        case 'f':
          force_fmt = select_format (optarg, CR);
          break;

        case 'g':
	  sscanf (optarg, "%u", &amplification);
          if (amplification == 0) ossrecord_usage (argv[0]);

        case 'l':
          level_meters = 1;
          break;

        case 'i':
          if (!strcmp(optarg, "?"))
            {
              dsp->recsrc = optarg;
              dsp->flags = O_RDONLY;
              open_device (dsp);
              select_recsrc (dsp);
            }
          dsp->recsrc = optarg;
          break;

        case 'm':
          sscanf (optarg, "%u", &nfiles);
          break;

        case 's':
          sscanf (optarg, "%d", &force_speed);
          if (force_speed == 0)
            {
              print_msg (ERRORM, "Bad sampling rate given\n");
              exit (E_USAGE);
            }
          if (force_speed < 1000) force_speed *= 1000;
          break;

        case 'r':
          c = snprintf (script, sizeof (script), "%s", optarg);
          if ((c >= sizeof (script)) || (c < 0))
            {
              print_msg (ERRORM, "-r argument is too long!\n");
              exit (E_USAGE);
            }
          break;

        case 't':
          sscanf (optarg, "%llu", &datalimit);
          break;

        case 'w':
          break;

        case 'v':
          verbose = 1;
          break;

	case 'O':
	  overwrite = 1;
	  break;

        case 'h':
        default:
          ossrecord_usage (argv[0]);
      }

  if (argc != optind + 1)
  /* No file or multiple file names given */
      ossrecord_usage (argv[0]);

  if (force_fmt == 0) force_fmt = container_a[type].dformat;
  if (force_channels == 0) force_channels = container_a[type].dchannels;
  if (force_speed == 0) force_speed = container_a[type].dspeed;
  switch (force_fmt)
    {
      case AFMT_U8:
      case AFMT_S16_NE:
      case AFMT_S24_NE:
      case AFMT_S32_NE: break;
      default: level_meters = 0; /* Not implemented */
    }

  signal (SIGSEGV, ossplay_getint);
  signal (SIGPIPE, ossplay_getint);
  signal (SIGINT, ossplay_getint);

  if (verbose)
    {
      oss_audioinfo ai;

      ai.dev = -1;

      if (ioctl(dsp->fd, SNDCTL_ENGINEINFO, &ai) != -1)
        print_msg (VERBOSEM, "Recording from %s\n", ai.name);
   }

  return optind;
}

long double
ossplay_ldexpl (long double num, int exp)
{
  /*
   * Very simple emulation of ldexpl to avoid linking to libm, or assuming
   * anything about float representation.
   */
  if (exp > 0)
    {
      while (exp > 31)
        {
          num *= 1UL << 31;
          exp -= 31;
        }
      num *= 1L << exp;
    }
  else if (exp < 0)
    {
      while (exp < -31)
        {
          num /= 1UL << 31;
          exp += 31;
        }
      num /= 1L << -exp;
    }

  return num;
}

static void
print_play_verbose_info (const unsigned char * buf, ssize_t l, void * metadata)
{
/*
 * Display a rough recording level meter, and the elapsed time.
 */

  verbose_values_t * val = (verbose_values_t *)metadata;
  double secs;

  secs = *val->datamark / val->constant;
  if (secs < val->next_sec) return;
  val->next_sec += PLAY_UPDATE_INTERVAL/1000;
  if (val->next_sec > val->tsecs) val->next_sec = val->tsecs;

  print_update (get_db_level (buf, l, val->format), secs, val->tstring);

  return;
}

static void
print_record_verbose_info (const unsigned char * buf, ssize_t l,
                           void * metadata)
{
/*
 * Display a rough recording level meter if enabled, and the elapsed time.
 */

  verbose_values_t * val = (verbose_values_t *)metadata;
  double secs;
  int v = -1, update_secs;

  secs = *val->datamark / val->constant;
  if ((secs < val->next_sec) &&
      (!level_meters || secs < val->next_sec2)) return;
  if (level_meters)
    {
      update_secs = 0;
      val->next_sec += LMETER_UPDATE_INTERVAL/1000;
      v = get_db_level (buf, l, val->format);
      if (secs >= val->next_sec2)
        {
          update_secs = 1;
          val->next_sec2 += REC_UPDATE_INTERVAL/1000;
          if (val->next_sec2 > val->tsecs) val->next_sec2 = val->tsecs;
        }
      else secs = val->next_sec2 - REC_UPDATE_INTERVAL/1000;
    }
  else
    {
      val->next_sec += REC_UPDATE_INTERVAL/1000;
      update_secs = 1;
    }

  if (val->next_sec > val->tsecs) val->next_sec = val->tsecs;
  if (secs > val->tsecs) secs = val->tsecs;

  print_record_update (v, secs, val->tstring, update_secs);

  return;
}

int
play (dspdev_t * dsp, int fd, unsigned long long * datamark,
      unsigned long long bsize, double constant, decoders_queue_t * dec,
      seekfunc_t * seekf)
{
#define EXITPLAY(code) \
  do { \
    ossplay_free (buf); \
    ossplay_free (verbose_meta); \
    clear_update (); \
    ioctl (dsp->fd, SNDCTL_DSP_HALT_OUTPUT, NULL); \
    return (code); \
  } while (0)

  unsigned long long rsize = bsize;
  unsigned long long filesize = *datamark;
  ssize_t outl;
  unsigned char * buf, * obuf, contflag = 0;
  decoders_queue_t * d;
  verbose_values_t * verbose_meta = NULL;

  buf = ossplay_malloc (bsize);

  if (verbose)
    verbose_meta = (void *)setup_verbose (dsp->format, constant, datamark);

  *datamark = 0;

  while (*datamark < filesize)
    {
      if (eflag) EXITPLAY (eflag);

      rsize = bsize;
      if (rsize > filesize - *datamark) rsize = filesize - *datamark;

      if ((seek_time != 0) && (seekf != NULL))
        {
          errors_t ret;

          ret = seekf (fd, datamark, filesize, constant, rsize, dsp->channels);
          if (ret == 0) continue;
          else if (ret == SEEK_CONT_AFTER_DECODE) contflag = 1;
          else EXITPLAY (ret);
        }

      if ((outl = read (fd, buf, rsize)) <= 0)
        {
          if (errno) perror_msg (dsp->dname);
          if ((filesize != UINT_MAX) && (*datamark < filesize) && (!eflag))
            {
              clear_update ();
              print_msg (WARNM, "Sound data ended prematurily!\n");
            }
          if ((outl == 0) && (!eflag)) return 0;
          EXITPLAY (eflag);
        }
      *datamark += outl;

      if (contflag)
        {
          contflag = 0;
          continue;
        }

      obuf = buf; d = dec;
      do
        {
          outl = d->decoder (&(d->outbuf), obuf, outl, d->metadata);
          obuf = d->outbuf;
          d = d->next;
        }
      while (d != NULL);

      if (verbose) print_play_verbose_info (obuf, outl, verbose_meta);
      if (write (dsp->fd, obuf, outl) == -1)
        {
          if ((errno == EINTR) && (eflag)) EXITPLAY (eflag);
          ossplay_free (buf);
          perror_msg ("audio write");
          exit (E_DECODE);
        }
    }

  ossplay_free (buf);
  clear_update ();
  return 0;
}

int
record (dspdev_t * dsp, FILE * wave_fp, const char * filename, double constant,
        unsigned long long datalimit, unsigned long long * data_size,
        decoders_queue_t * dec)
{
#define EXITREC(code) \
  do { \
    ossplay_free (buf); \
    ossplay_free (verbose_meta); \
    clear_update (); \
    if ((eflag) && (verbose)) \
      print_msg (VERBOSEM, "\nStopped (%d).\n", eflag-128); \
    ioctl (dsp->fd, SNDCTL_DSP_HALT_INPUT, NULL); \
    return (code); \
  } while(0)

  ssize_t l, outl;
  decoders_queue_t * d;
  unsigned char * buf, * obuf;
  verbose_values_t * verbose_meta = NULL;

  if (verbose)
    {
      *data_size = datalimit;
      verbose_meta = (void *)setup_verbose (dsp->format, constant, data_size);
      strncpy (verbose_meta->tstring, filename, 20)[19] = 0;
    }

  *data_size = 0;
  buf = ossplay_malloc (RECBUF_SIZE);
   /*LINTED*/ while (1)
    {
//printf("datalimit %llu, *data_size %llu\n", datalimit, *data_size);
      if ((l = read (dsp->fd, buf, RECBUF_SIZE)) < 0)
	{
          if ((errno == EINTR) && (eflag)) EXITREC (eflag);
	  if (errno == ECONNRESET) EXITREC (E_ENCODE); /* Device disconnected */
          perror_msg (dsp->dname);
          EXITREC (E_ENCODE);
	}
      if (l == 0)
	{
	  print_msg (ERRORM, "Unexpected EOF on audio device\n");
          EXITREC (eflag);
	}

      obuf = buf; d = dec; outl = l;
      do
        {
          outl = d->decoder (&(d->outbuf), obuf, outl, d->metadata);
          obuf = d->outbuf;
          d = d->next;
        }
      while (d != NULL);

      if (eflag) EXITREC (eflag);

      if (fwrite (obuf, outl, 1, wave_fp) != 1)
        {
          if ((errno == EINTR) && (eflag)) EXITREC (eflag);
          perror_msg (filename);
          EXITREC (E_ENCODE);
        }

      *data_size += outl;
      if (verbose) print_record_verbose_info (obuf, outl, verbose_meta);

      if ((datalimit != 0) && (*data_size >= datalimit)) break;
    }

  ossplay_free (buf);
  clear_update ();
  print_msg (VERBOSEM, "\nDone.\n");
  return 0;
}

int silence (dspdev_t * dsp, unsigned long long len, int speed)
{
  errors_t ret;
  ssize_t i;
  unsigned char empty[1024];

  ret = setup_device (dsp, AFMT_U8, 1, speed);

  if (ret == E_FORMAT_UNSUPPORTED) 
    {
      len *= 4;
      if ((ret = setup_device (dsp, AFMT_S16_NE, 2, speed))) return ret;
    }
  else if (ret) return ret;

  memset (empty, 0, 1024 * sizeof (unsigned char));

  while (len > 0)
    {
      i = 1024;
      if (i > len) i = len;
      if ((i = write (dsp->fd, empty, i)) < 0) return -1;

      len -= i;
    }

  return 0;
}
