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

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>

#undef  MPEG_SUPPORT

/* Magic numbers used in Sun and NeXT audio files (.au/.snd) */
#define SUN_MAGIC 	0x2e736e64	/* Really '.snd' */
#define SUN_INV_MAGIC	0x646e732e	/* '.snd' upside-down */
#define DEC_MAGIC	0x2e736400	/* Really '\0ds.' (for DEC) */
#define DEC_INV_MAGIC	0x0064732e	/* '\0ds.' upside-down */

/* Magic numbers for file formats based on IFF */
#define FORM_MAGIC	0x464f524d	/* 'FORM' */
#define AIFF_MAGIC	0x41494646	/* 'AIFF' */
#define AIFC_MAGIC	0x41494643	/* 'AIFC' */
#define _8SVX_MAGIC	0x38535658	/* '8SVX' */
#define _16SV_MAGIC	0x31365356	/* '16SV' */
#define MAUD_MAGIC	0x4D415544	/* 'MAUD' */

/* Magic numbers for .wav files */
#define RIFF_MAGIC	0x52494646	/* 'RIFF' */
#define RIFX_MAGIC	0x52494658	/* 'RIFX' */
#define WAVE_MAGIC	0x57415645	/* 'WAVE' */

/* Beginning of magic for Creative .voc files */
#define Crea_MAGIC	0x43726561	/* 'Crea' */

enum {
  AIFF_FILE,
  AIFC_FILE,
  WAVE_FILE,
  WAVE_FILE_BE,
  _8SVX_FILE,
  _16SV_FILE,
  MAUD_FILE
};

#define DEFAULT_CHANNELS	1
#define DEFAULT_FORMAT		AFMT_U8
#define DEFAULT_SPEED		11025
int force_speed = -1, force_bits = -1, force_channels = -1, amplification = 1;
int audiofd = 0, quitflag = 0, quiet = 0;
char audio_devname[32] = "/dev/dsp";

static int prev_speed = 0, prev_bits = 0, prev_channels = 0;
static int raw_mode = 0, verbose = 0, exitstatus = 0;
#ifdef MPEG_SUPPORT
static int mpeg_enabled = 0;
#endif

static char current_filename[64] = "";
static char *playtgt = NULL;

#ifdef MPEG_SUPPORT
extern void play_mpeg (char *, int, unsigned char *, int);
#endif

static void describe_error (void);
static char * filepart (char *);
static void find_devname (char *, char *);
static void select_playtgt (char *);
static void open_device (void);
static off_t (*oss_lseek) (int, off_t, int) = lseek;
static off_t oss_lseek_stdin (int, off_t, int);
static void play_iff (char *, int, unsigned char *, int);
static void play_au (char *, int, unsigned char *, int);
static void play_file (char *);
static void play_voc (char *, int, unsigned char *, int);
static void print_verbose (int, int, int);
static void usage (char *);

static off_t
oss_lseek_stdin (int fd, off_t off, int w)
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
le_int (unsigned char *p, int l)
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
be_int (unsigned char *p, int l)
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
usage (char *prog)
{
  fprintf (stderr, "Usage: %s [options...] filename...\n", prog);
  fprintf (stderr, "  Options:  -v             Verbose output\n");
  fprintf (stderr, "            -q             No informative printouts\n");
  fprintf (stderr, "            -d<devname>    Change output device.\n");
  fprintf (stderr,
	   "            -s<rate>       Change playback rate.\n");
  fprintf (stderr,
	   "            -b<bits>       Change number of bits.\n");
  fprintf (stderr,
	   "            -c<channels>   Change number of channels.\n");
  fprintf (stderr,
           "            -o<playtgt>|?  Select/Query output target.\n");
  fprintf (stderr,
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
      fprintf (stderr, "\nThe device file was found in /dev but\n"
	       "there is no driver for it currently loaded.\n"
	       "\n"
	       "You can start it by executing the /usr/local/bin/soundon\n"
	       "command as super user (root).\n");
      break;

    case ENOSPC:
      fprintf (stderr, "\nThe soundcard driver was not installed\n"
	       "properly. The system is out of DMA compatible memory.\n"
	       "Please reboot your system and try again.\n");

      break;

    case ENOENT:
      fprintf (stderr, "\nThe sound device file is missing from /dev.\n"
	       "You should try re-installing OSS.\n");
      break;

    case EBUSY:
      fprintf (stderr,
	       "\nThere is some other application using this audio device.\n"
	       "Exit it and try again.\n");
      fprintf (stderr,
	       "You can possibly find out the conflicting application by looking\n"
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
      perror (audio_devname);
      describe_error ();
      exit (-1);
    }

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
find_devname (char *devname, char *num)
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

/*ARGSUSED*/
int
setup_device (int fd, int format, int channels, int speed)
{
  int tmp;

  if (speed != prev_speed || format != prev_bits || channels != prev_channels)
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
  ioctl (audiofd, SNDCTL_SETSONG, current_filename);	/* No error checking */

  prev_speed = speed;
  prev_channels = channels;
  prev_bits = format;

  tmp = APF_NORMAL;
  ioctl (audiofd, SNDCTL_DSP_PROFILE, &tmp);

  tmp = format;

  if (verbose > 4)
    fprintf (stdout, "Setup device %d/%d/%d\n", channels, format, speed);

  if (ioctl (audiofd, SNDCTL_DSP_SETFMT, &tmp) == -1)
    {
      perror (audio_devname);
      fprintf (stderr, "Failed to select bits/sample\n");
      return 0;
    }

  if (tmp != format)
    {
      fprintf (stderr, "%s doesn't support this audio format (%x/%x).\n",
	       audio_devname, format, tmp);
      return 0;
    }

  tmp = channels;

  if (ioctl (audiofd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
    {
      perror (audio_devname);
      fprintf (stderr, "Failed to select number of channels.\n");
      return 0;
    }

  if (tmp != channels)
    {
      fprintf (stderr, "%s doesn't support %d channels.\n",
	       audio_devname, channels);
      return 0;
    }

  tmp = speed;

  if (ioctl (audiofd, SNDCTL_DSP_SPEED, &tmp) == -1)
    {
      perror (audio_devname);
      fprintf (stderr, "Failed to select sampling rate.\n");
      return 0;
    }

  if (tmp != speed && !quiet)
    {
      fprintf (stderr, "Warning: Playback using %d Hz (file %d Hz)\n",
	       tmp, speed);
    }

  return 1;
}

static void 
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
  fprintf (stdout, "%s/%s/%d Hz\n", fmt, chn, speed);
}

/*ARGSUSED*/
static void
play_au (char *filename, int fd, unsigned char *hdr, int l)
{
  int channels = 1, format = AFMT_S8, speed = 11025;
  unsigned int filelen, fmt = 0, i, p = 24, an_len = 0;

  p = be_int (hdr + 4, 4);
  filelen = be_int (hdr + 8, 4);
  fmt = be_int (hdr + 12, 4);
  speed = be_int (hdr + 16, 4);
  channels = be_int (hdr + 20, 4);

  if (verbose > 2)
    {
      if (filelen == (unsigned int)-1)
        fprintf (stdout, "%s: Filelen: unspecified\n", filename);
      else
        fprintf (stdout, "%s: Filelen: %u\n", filename, filelen);
    }
  if (verbose > 3) fprintf (stderr, "%s: Offset: %u\n", filename, p);
  if (filelen == (unsigned int)-1) filelen = UINT_MAX;

  switch (fmt)
    {
    case 1:
      format = AFMT_MU_LAW;
      break;

    case 2:
      format = AFMT_S8;
      break;

    case 3:
      format = AFMT_S16_BE;
      break;

    case 4:
      format = AFMT_S24_BE;
      break;

    case 5:
      format = AFMT_S32_BE;
      break;

    case 6:
    case 7:
      fprintf (stderr, "%s: Floating point encoded .au files are not supported",
               filename);
      break;

    case 23:
    case 24:
    case 25:
    case 26:
      fprintf (stderr, "%s: G.72x ADPCM encoded .au files are not supported",
               filename);
      break;

    case 27:
      format = AFMT_A_LAW;
      break;

    default:
      fprintf (stderr, "%s: Unknown encoding %d.\n", filename, fmt);
      return;
    }

  if (verbose)
    {
      fprintf (stdout, "Playing .au file %s, ", filename);
      print_verbose (format, channels, speed);

      if ((verbose > 1) && (p > 24))
	{
          if (p > 1048) an_len = 1024;
          else an_len = p - 24;
          if (read (fd, hdr, an_len) < an_len)
            {
              fprintf (stderr, "%s: Can't read to pos %u\n", filename,
                       an_len + 24);
              return;
            }
	  fprintf (stdout, "%s: Annotations: ", filename);
	  for (i = 0; i < an_len; i++)
	    fprintf (stdout, "%c", isprint (hdr[i])?hdr[i]:'.');
	  fprintf (stdout, "\n");
	}
    }

  if (oss_lseek (fd, p - l - an_len, SEEK_CUR) == -1)
    {
      perror (filename);
      fprintf (stderr, "Can't seek to the data chunk\n");
      return;
    }

  decode_sound (fd, filelen, format, channels, speed, NULL);
}

/*ARGSUSED*/
static void
play_iff (char *filename, int fd, unsigned char *buf, int type)
{
/*
 * Generalized IFF parser - handles WAV, AIFF, AIFC, 8SVX, 16SV and MAUD.
 */
  enum
  {
    COMM_BIT,
    SSND_BIT,
    FVER_BIT
  };
#define COMM_FOUND (1 << COMM_BIT)
#define SSND_FOUND (1 << SSND_BIT)
#define FVER_FOUND (1 << FVER_BIT)

#define LIST_HUNK 0x4c495354

#define FVER_HUNK 0x46564552
#define COMM_HUNK 0x434F4D4D
#define SSND_HUNK 0x53534E44
#define ANNO_HUNK 0x414E4E4F
#define NAME_HUNK 0x4E414D45
#define AUTH_HUNK 0x41555448
#define COPY_HUNK 0x28632920

#define VHDR_HUNK 0x56484452
#define BODY_HUNK 0x424f4459
#define CHAN_HUNK 0x4348414e

#define MDAT_HUNK 0x4D444154
#define MHDR_HUNK 0x4D484452

#define alaw_FMT 0x616C6177
#define ALAW_FMT 0x414C4157
#define ulaw_FMT 0x756C6177
#define ULAW_FMT 0x554C4157
#define sowt_FMT 0x736F7774
#define twos_FMT 0x74776F73
#define fl32_FMT 0x666C3332
#define FL32_FMT 0x464C3332
#define ima4_FMT 0x696D6134
#define NONE_FMT 0x4E4F4E45
#define raw_FMT  0x72617720
#define in24_FMT 0x696e3234
#define ni24_FMT 0x6e693234
#define in32_FMT 0x696E3332
#define ni32_FMT 0x6E693332

#define fmt_HUNK 0x666d7420
#define data_HUNK 0x64617461
#define INFO_HUNK 0x494e464f

#define ASEEK(fd, offset, n) \
  do { \
    if (oss_lseek (fd, offset, SEEK_CUR) == -1) \
      { \
        fprintf (stderr, "%s: error: cannot seek to end of " #n " chunk.\n", \
                 filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
  } while (0)

#define AREAD(fd, buf, len, n) \
  do { \
    if (chunk_size < len) \
      { \
        fprintf (stderr, \
                 "%s: error: chunk " #n " size is too small.\n", filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
    if (read(fd, buf, len) < len) \
      { \
        fprintf (stderr, "%s: error: cannot read " #n " chunk.\n", filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
    ASEEK (fd, chunk_size - len, n); \
  } while (0)

#define BITS2SFORMAT(endian) \
  do { \
    switch (bits) \
      { \
         case 8: format = AFMT_S8; break; \
         case 16: format = AFMT_S16_##endian; break; \
         case 24: format = AFMT_S24_##endian; break; \
         case 32: format = AFMT_S32_##endian; break; \
         default: format = AFMT_S16_##endian; break; \
     } break; \
  } while (0)

  int channels = 1, bits = 8, format, speed = 11025;
  long double COMM_rate;
  unsigned int chunk_id, chunk_size = 18, csize = 12, found = 0, offset = 0,
               sound_loc = 0, sound_size = 0, timestamp, total_size;
  int (*ne_int) (unsigned char *p, int l) = be_int;

  msadpcm_values_t msadpcm_val = {
    256, 496, 7, {
      {256, 0},
      {512, -256},
      {0, 0},
      {192, 64},
      {240, 0},
      {460, -208},
      {392, -232} }
  };

  if (type == _8SVX_FILE) format = AFMT_S8;
  else format = AFMT_S16_BE;
  if (type == WAVE_FILE) ne_int = le_int;

  total_size = ne_int (buf + 4, 4);
  if (verbose > 2)
    fprintf (stdout, "Filelen = %u\n", total_size);
  do
    {
      if (read (fd, buf, 8) < 8)
        {
          fprintf (stderr, "%s: Cannot read chunk header at pos %u\n",
                           filename, csize);
          if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta;
          return;
        }
      chunk_id = be_int (buf, 4);
      chunk_size = ne_int (buf + 4, 4);
      if (chunk_size % 2) chunk_size += 1;

      switch (chunk_id)
        {
          /* AIFF / AIFC chunks */
          case COMM_HUNK:
            if (found & COMM_FOUND)
              {
                fprintf (stderr,
                         "%s: error: COMM hunk not singular!\n", filename);
                return;
              }
            if (type == AIFC_FILE) AREAD (fd, buf, 22, COMM);
            else AREAD (fd, buf, 18, COMM);
            found |= COMM_FOUND;

            channels = be_int (buf, 2);
#if 0
            num_frames = be_int (buf + 2, 4); /* ossplay doesn't use this */
#endif
            bits = be_int (buf + 6, 2);
            bits += bits % 8;
            BITS2SFORMAT (BE);
#if AFMT_S16_NE == AFMT_S16_LE
            {
              unsigned char tmp, *s = buf + 8; int i;

              for (i=0; i < 5; i++)
                {
                  tmp = s[i]; s[i] = s[9-i]; s[9-i] = tmp;
                }
            }
#endif
            memcpy (&COMM_rate, buf + 8, sizeof (COMM_rate));
            speed = (int)COMM_rate;

            if (type != AIFC_FILE)
              {
                csize += chunk_size + 8;
                continue;
              }

            switch (be_int (buf + 18, 4))
              {
                case NONE_FMT: break;
                case twos_FMT: format = AFMT_S16_BE; break;
                case in24_FMT: format = AFMT_S24_BE; break;
                case in32_FMT: format = AFMT_S32_BE; break;
                case ni24_FMT: format = AFMT_S24_LE; break;
                case ni32_FMT: format = AFMT_S32_LE; break;
                case sowt_FMT: BITS2SFORMAT (LE); break;
                /* Apple Docs refer to this as AFMT_S16_LE only, but some
                   programs misinterpret this. */
                case raw_FMT:
                /* Apple Docs refer to this as AFMT_U8 only, but some programs
                   misinterpret this. */
                  switch (bits)
                    {
                      case 8: format = AFMT_U8; break;
                      case 16: format = AFMT_U16_BE; break;
                      default: format = AFMT_U8; break;
                    } break;
                case alaw_FMT:
                case ALAW_FMT: format = AFMT_A_LAW; break;
                case ulaw_FMT:
                case ULAW_FMT: format = AFMT_MU_LAW; break;
                case ima4_FMT: format = AFMT_IMA_ADPCM; break;
#if 0
                case fl32_FMT:
                case FL32_FMT: format = AFMT_FLOAT; break;
#endif
                default:
                  fprintf (stderr,
                           "%s: error: %c%c%c%c compression is not supported\n",
                           filename, *(buf + 18), *(buf + 19),
                           *(buf + 20), *(buf + 21));
                  return;
              }
              break;
          case SSND_HUNK:
            if (found & SSND_FOUND)
              {
                fprintf (stderr,
                         "%s: error: SSND hunk not singular!\n", filename);
                return;
              }
            if (chunk_size < 8)
              {
                fprintf (stderr,
                         "%s: error: impossibly small SSND hunk\n", filename);
                return;
              }
            if (read (fd, buf, 8) < 8)
              {
                fprintf (stderr, "%s: error: cannot read SSND chunk.\n",
                         filename);
                return;
              }
            found |= SSND_FOUND;

            offset = be_int (buf, 4);
#if 0
            block_size = be_int (buf + 4, 4); /* ossplay doesn't use this */
#endif
            sound_loc = csize + 16 + offset;
            sound_size = chunk_size - 8;

            if ((!strcmp (filename, "-")) && (oss_lseek == oss_lseek_stdin))
              goto stdinext;
            ASEEK (fd, chunk_size - 8, SSND);
            break;
          case FVER_HUNK:
            AREAD (fd, buf, 4, FVER);
            timestamp = be_int (buf, 4);
            found |= FVER_FOUND;
            break;

          /* 8SVX / 16SV chunks */
          case VHDR_HUNK:
            AREAD (fd, buf, 16, VHDR);
            speed = be_int (buf + 12, 2);
            if (type == _8SVX_FILE) switch (buf[15])
              {
                case 0: format = AFMT_S8; break;
                case 1: format = AFMT_FIBO_DELTA; break;
                case 2: format = AFMT_EXP_DELTA; break;
                default:
                 fprintf (stderr, "%s: Unsupported compression %d\n",
                          filename, buf[15]);
                return;
              }
            found |= COMM_FOUND;
            break;
          case data_HUNK: /* WAVE chunk */
            if (verbose > 3)
              fprintf (stdout, "DATA chunk. Offs = %u, "
                       "len = %u\n", csize+8, chunk_size);
            sound_loc = csize + 8;
          case MDAT_HUNK: /* MAUD chunk */
          case BODY_HUNK:
            sound_size = chunk_size;
            if (chunk_id != data_HUNK) sound_loc = csize + 4;
            found |= SSND_FOUND;
            if ((!strcmp (filename, "-")) &&
                (oss_lseek == oss_lseek_stdin))
              goto stdinext;
            ASEEK (fd, chunk_size, BODY);
            break;
          case CHAN_HUNK:
            AREAD (fd, buf, 4, CHAN);
            channels = be_int (buf, 4);
            channels = (channels & 0x01) +
                       ((channels & 0x02) >> 1) +
                       ((channels & 0x04) >> 2) +
                       ((channels & 0x08) >> 3);
            break;

          /* MAUD chunks */
          case MHDR_HUNK:
            AREAD (fd, buf, 20, MHDR);
            bits = be_int (buf + 4, 2);
            BITS2SFORMAT (BE);
            speed = be_int (buf + 8, 4) / be_int (buf + 12, 2);
            channels = be_int (buf + 16, 2);
            switch (be_int (buf + 18, 2))
              {
                case 0: /* NONE */ break;
                case 2: format = AFMT_A_LAW; break;
                case 3: format = AFMT_MU_LAW; break;
                case 6: format = AFMT_IMA_ADPCM; break;
                default: fprintf(stderr, "%s: format not"
                                         "supported", filename);
              }
            found |= COMM_FOUND;
            break;

          /* WAVE chunks */
          case fmt_HUNK: { int len, i, x;
            if (found & COMM_FOUND)
              {
                fprintf (stderr, "%s: error: fmt hunk not singular!\n",
                         filename);
                return;
              }
            if (chunk_size > 1024) len = 1024;
            else len = chunk_size;
            AREAD (fd, buf, len, fmt);

            format = ne_int (buf, 2);
            if (verbose > 3)
              fprintf (stdout, "FMT chunk: len = %u, fmt = %#x\n",
                       chunk_size, format);

            msadpcm_val.channels = channels = ne_int (buf + 2, 2);
            speed = ne_int (buf + 4, 4);
            msadpcm_val.nBlockAlign = ne_int (buf + 12, 2);
            bits = ne_int (buf + 14, 2);
            bits += bits % 8;

            if (format == 0xFFFE)
              {
                if (chunk_size < 40)
                   {
                     fprintf (stderr, "%s: invallid fmt chunk\n", filename);
                     return;
                   }
                format = ne_int (buf + 24, 2);
              }
            switch (format)
              {
                case 0x1:
                  if (type == WAVE_FILE) BITS2SFORMAT (LE);
                  else BITS2SFORMAT (BE);
                  if (bits == 8) format = AFMT_U8;
                  break;
                case 0x2: format = AFMT_MS_ADPCM; break;
                case 0x6: format = AFMT_A_LAW; break;
                case 0x7: format = AFMT_MU_LAW; break;
                case 0x11: format = AFMT_IMA_ADPCM; break;
#if 0
                case 0x3: format = AFMT_FLOAT; break;
                case 0x50: /* MPEG */
                case 0x55: /* MPEG 3 */
#endif
                default:
                  fprintf (stderr, "%s: Unsupported wave format %#x\n",
                           filename, format);
                  return;
              }
            found |= COMM_FOUND;

            if ((format != AFMT_MS_ADPCM) || (chunk_size < 20)) break;
            msadpcm_val.wSamplesPerBlock = ne_int (buf + 18, 2);
            if (chunk_size < 22) break;
            msadpcm_val.wNumCoeff = ne_int (buf + 20, 2);
            if (msadpcm_val.wNumCoeff > 32) msadpcm_val.wNumCoeff = 32;

            x = 22;

            for (i = 0; (i < msadpcm_val.wNumCoeff) && (x < chunk_size-3); i++)
              {
                msadpcm_val.coeff[i].coeff1 = (short) ne_int (buf + x, 2);
                x += 2;
                msadpcm_val.coeff[i].coeff2 = (short) ne_int (buf + x, 2);
                x += 2;
              }
            } break;

          /* common chunks */
          case NAME_HUNK:
          case AUTH_HUNK:
          case ANNO_HUNK:
          case COPY_HUNK:
            if (verbose > 1)
              {
                int i, len;

                fprintf (stdout, "%s: ", filename);
                if (chunk_size > 1024) len = 1024;
                else len = chunk_size;
                switch (chunk_id)
                  {
                    case NAME_HUNK:
                      fprintf (stdout, "Name: ");
                      AREAD (fd, buf, len, NAME);
                      break;
                    case AUTH_HUNK:
                      fprintf (stdout, "Author: ");
                      AREAD (fd, buf, len, AUTH);
                      break;
                    case COPY_HUNK:
                      fprintf (stdout, "Copyright: ");
                      AREAD (fd, buf, len, COPY);
                      break;
                    case ANNO_HUNK:
                      fprintf (stdout, "Annonations: ");
                      AREAD (fd, buf, len, ANNO);
                      break;
                  }
                for (i = 0; i < len; i++)
                  fprintf (stdout, "%c", isprint (buf[i])?buf[i]:' ');
                fprintf (stdout, "\n");
                break;
              }

          default:
            ASEEK (fd, chunk_size, UNKNOWN);
            break;
       }

      csize += chunk_size + 8;

    } while (csize < total_size);

nexta:
    if ((found & COMM_FOUND) == 0)
      {
        fprintf(stderr, "%s: Couldn't find format chunk!\n", filename);
        return;
      }

    if ((found & SSND_FOUND) == 0)
      {
        fprintf(stderr, "%s: Couldn't find sound chunk!\n", filename);
        return;
      }

    if ((type == AIFC_FILE) && ((found & FVER_FOUND) == 0))
      fprintf(stderr, "%s: Couldn't find AIFFC FVER chunk.\n", filename);

    if (oss_lseek (fd, sound_loc, SEEK_SET) == -1)
      {
        perror (filename);
        fprintf (stderr, "Can't seek in file\n");
        return;
      }

stdinext:
  if (verbose)
    {
      if (type == AIFF_FILE)
        fprintf(stdout, "Playing AIFF file %s, ", filename);
      else if (type == AIFC_FILE)
        fprintf(stdout, "Playing AIFC file %s, ", filename);
      else if ((type == WAVE_FILE) || (type == WAVE_FILE_BE))
        fprintf(stdout, "Playing WAVE file %s, ", filename);
      else if (type == _8SVX_FILE)
        fprintf(stdout, "Playing 8SVX file %s, ", filename);
      else if (type == _16SV_FILE)
        fprintf(stdout, "Playing 16SV file %s, ", filename);
      else
        fprintf(stdout, "Playing MAUD file %s, ", filename);
      print_verbose (format, channels, speed);
    }

  decode_sound (fd, sound_size, format, channels, speed, (void *)&msadpcm_val);

  return;
}

/*ARGSUSED*/
static void
play_voc (char *filename, int fd, unsigned char *hdr, int l)
{
#define VREAD(fd, buf, len) \
  do { \
    if (read (fd, buf, len) < len) \
      { \
        fprintf (stderr, "%s: Can't read %d bytes at pos %d\n", \
                 filename, len, l); \
        return; \
      } \
    pos += len; \
  } while (0)

  unsigned int data_offs, vers, id, len, blklen, fmt, tmp,
               loopcount = 0, loopoffs = 4, pos = l + 7;
  unsigned char buf[256], block[256];
  int speed = 11025, channels = 1, bits = 8, format = AFMT_U8;

  if (read (fd, hdr + 19, 7) < 7)
    {
      fprintf (stderr, "%s: Not a valid .VOC file\n", filename);
      return;
    }

  data_offs = le_int (hdr + 0x14, 2);
  vers = le_int (hdr + 0x16, 2);
  id = le_int (hdr + 0x18, 2);

  if ((((~vers) + 0x1234) & 0xffff) != id)
    {
      fprintf (stderr, "%s: Not a valid .VOC file\n", filename);
      return;
    }

  if (verbose)
    fprintf (stdout, "Playing .VOC file %s\n", filename);

   /*LINTED*/ while (1)
    {
      if (oss_lseek (fd, data_offs - pos, SEEK_CUR) == -1)
        {
          fprintf (stderr, "%s: Can't seek to pos %d\n", filename, data_offs);
          return;
        }
      pos = data_offs + 4;

      if ((tmp = read (fd, buf, 1)) < 1)
        {
          /* Don't warn when read returns 0 - it may be end of file. */
          if (tmp != 0)
            fprintf (stderr, "%s: Can't read 1 byte at pos %d\n", filename, l);
          return;
        }

      if (buf[0] == 0)
	return;			/* End */

      if (read (fd, buf + 1, 3) != 3)
	{
	  fprintf (stderr, "%s: Truncated .VOC file (%d)\n",
		   filename, buf[0]);
	  return;
	}

      blklen = len = le_int (buf + 1, 3);

      if (verbose > 3)
	fprintf (stdout, "%s: %0x: Block type %d, len %d\n",
		 filename, data_offs, buf[0], len);
      switch (buf[0])
	{

	case 1:		/* Sound data block */
	  VREAD (fd, block, 2);

	  tmp = 256 - block[0];	/* Time constant */
	  speed = (1000000 + tmp / 2) / tmp / channels;

	  fmt = block[1];
	  len -= 2;

          switch (fmt)
            {
              case 0: format = AFMT_U8; break;
              case 1: format = AFMT_CR_ADPCM_4; break;
              case 2: format = AFMT_CR_ADPCM_3; break;
              case 3: format = AFMT_CR_ADPCM_2; break;
              case 4: format = AFMT_S16_LE; break;
              case 6: format = AFMT_A_LAW; break;
              case 7: format = AFMT_MU_LAW; break;
              default:
                fprintf (stderr,
                         "%s: encoding %d is not supported\n", filename, fmt);
                return;
            }

	case 2:		/* Continuation data */
          if (decode_sound (fd, len, format, channels, speed, NULL) < 0)
            return;
          pos += len;
	  break;

	case 3:		/* Silence */
	  VREAD (fd, block, 3);
	  len = le_int (block, 2);
	  tmp = 256 - block[2];	/* Time constant */
	  speed = (1000000 + tmp / 2) / tmp;
	  if (!setup_device (fd, AFMT_U8, 1, speed))
	    return;
	  {
	    int i;
	    unsigned char empty[1024];

            memset (empty, 0x80, 1024 * sizeof(unsigned char));

	    while (len > 0)
	      {
		i = 1024;
		if (i > len)
		  i = len;
		write (audiofd, empty, i);

		len -= i;
	      }
            ioctl (audiofd, SNDCTL_DSP_POST, NULL);
	  }
	  break;

        case 5: 	/* Text */
          if (verbose)
            {
              int i;

              if (len > 256) len = 256;
              VREAD (fd, block, len);
              fprintf (stdout, "Text: ");
              for (i = 0; i < len; i++)
                fprintf (stdout, "%c", isprint(block[i])?block[i]:'.');
              fprintf (stdout, "\n");
            }
          break;

	case 6:		/* Loop start */
	  VREAD (fd, block, 2);
	  loopoffs = data_offs + blklen + 4;
	  loopcount = le_int (block, 2);
	  break;

	case 7:		/* End of repeat loop */
	  if (loopcount != 0xffff)
	    loopcount--;

	  /* Set "return" point. Compensate for increment of data_offs. */
	  if (loopcount > 0)
	    data_offs = loopoffs - blklen - 4;

	  break;

	case 8:		/* Sampling parameters */
	  VREAD (fd, block, 4);

          speed = 256000000/(channels * (65536 - le_int (block, 2)));
	  channels = block[3] + 1;
	  fmt = block[2];
          switch (fmt)
            {
              case 0: format = AFMT_U8; break;
              case 1: format = AFMT_CR_ADPCM_4; break;
              case 2: format = AFMT_CR_ADPCM_3; break;
              case 3: format = AFMT_CR_ADPCM_2; break;
              case 4: format = AFMT_S16_LE; break;
              case 6: format = AFMT_A_LAW; break;
              case 7: format = AFMT_MU_LAW; break;
              default:
                fprintf (stderr,
                         "%s: encoding %d is not supported\n", filename, fmt);
                return;
            }
	  break;

	case 9:		/* New format sound data */
	  VREAD (fd, block, 12);

	  len -= 12;

	  speed = le_int (block, 3);
	  bits = block[4];
	  channels = block[5];
	  fmt = le_int (block + 6, 2);

          switch (fmt)
            {
              case 0: format = AFMT_U8; break;
              case 1: format = AFMT_CR_ADPCM_4; break;
              case 2: format = AFMT_CR_ADPCM_3; break;
              case 3: format = AFMT_CR_ADPCM_2; break;
              case 4: format = AFMT_S16_LE; break;
              case 6: format = AFMT_A_LAW; break;
              case 7: format = AFMT_MU_LAW; break;
              default:
                fprintf (stderr,
                         "%s: encoding %d is not supported\n", filename, fmt);
                return;
            }

          if (decode_sound (fd, len, format, channels, speed, NULL) < 0)
            return;
          pos += len;
	  break;
	}

      data_offs += blklen + 4;
    }
}

static char *
filepart (char *name)
{
  char *s = name;

  while (*name)
    {
      if (name[0] == '/' && name[1] != 0)
	s = name + 1;
      name++;
    }

  return s;
}

static void
play_file (char *filename)
{
  int fd, l, i;
  unsigned char buf[1024];
  char *suffix;

  if (!strcmp(filename, "-"))
    {
      FILE *fp;

      fp = fdopen(0, "rb");
      fd = fileno(fp);
      /*
       * Use emulation if stdin is not seekable (e.g. on Linux).
       */
      if (lseek (fd, 0, SEEK_CUR) == -1) oss_lseek = oss_lseek_stdin;
    }
  else fd = open (filename, O_RDONLY, 0);
  if (fd == -1)
    {
      perror (filename);
      exitstatus++;
      return;
    }

  strncpy (current_filename, filepart (filename), sizeof (current_filename));
  current_filename[sizeof (current_filename) - 1] = 0;

  if ((l = read (fd, buf, 12)) == -1)
    {
      perror (filename);
      goto seekerror;
    }

  if (l == 0)
    {
      fprintf (stderr, "%s is empty file.\n", filename);
      goto seekerror;
    }

/*
 * Try to detect the file type - First .wav
 */
  switch (be_int (buf, 4))
    {
      case SUN_MAGIC:
      case DEC_MAGIC:
      case SUN_INV_MAGIC:
      case DEC_INV_MAGIC:
        if ((i = read (fd, buf + 12, 12)) == -1)
          {
            perror (filename);
            goto seekerror;
          }
        l += i;
        if (l < 24) break;
        play_au (filename, fd, buf, l);
        goto done;
      case Crea_MAGIC:
        if ((i = read (fd, buf + 12, 7)) == -1)
          {
            perror (filename);
            goto seekerror;
          }
        l += i;
        if ((l < 19) || (memcmp (buf, "Creative Voice File", 19))) break;
        play_voc (filename, fd, buf, l);
        goto done;
      case RIFF_MAGIC:
        if ((l < 12) || be_int (buf + 8, 4) != WAVE_MAGIC) break;
        play_iff (filename, fd, buf, WAVE_FILE);
        goto done;
      case RIFX_MAGIC:
        if ((l < 12) || be_int (buf + 8, 4) != WAVE_MAGIC) break;
        play_iff (filename, fd, buf, WAVE_FILE_BE);
        goto done;
      case FORM_MAGIC:
        if (l < 12) break;
        switch (be_int (buf + 8, 4))
          {
            case AIFF_MAGIC:
              play_iff (filename, fd, buf, AIFF_FILE);
              goto done;
            case AIFC_MAGIC:
              play_iff (filename, fd, buf, AIFC_FILE);
              goto done;
            case _8SVX_MAGIC:
              play_iff (filename, fd, buf, _8SVX_FILE);
              goto done;
            case _16SV_MAGIC:
              play_iff (filename, fd, buf, _16SV_FILE);
              goto done;
            case MAUD_MAGIC:
              play_iff (filename, fd, buf, MAUD_FILE);
              goto done;
            default: break;
          }
      default: break;
    }

  oss_lseek (fd, 0, SEEK_SET);	/* Start from the beginning */

/*
 *	The file was not identified by it's content. Try using the file name
 *	suffix.
 */

  suffix = strrchr (filename, '.');
  if (suffix == NULL) suffix = filename;

  if (strcmp (suffix, ".au") == 0 || strcmp (suffix, ".AU") == 0)
    {				/* Raw mu-Law data */

      if (verbose)
	fprintf (stdout, "Playing raw mu-Law file %s\n", filename);

      decode_sound (fd, UINT_MAX, AFMT_MU_LAW, 1, 8000, NULL);
      goto done;
    }

  if (strcmp (suffix, ".snd") == 0 || strcmp (suffix, ".SND") == 0)
    {
      if (!quiet)
	fprintf (stderr,
		 "%s: Unknown format. Assuming RAW audio (%d/%d/%d.\n",
		 filename, DEFAULT_SPEED, DEFAULT_FORMAT, DEFAULT_CHANNELS);

      decode_sound (fd, UINT_MAX, DEFAULT_FORMAT, DEFAULT_CHANNELS,
                  DEFAULT_SPEED, NULL);
      goto done;
    }

  if (strcmp (suffix, ".cdr") == 0 || strcmp (suffix, ".CDR") == 0)
    {
      if (verbose)
	fprintf (stdout, "%s: Playing CD-R (cdwrite) file.\n", filename);

      decode_sound (fd, UINT_MAX, AFMT_S16_BE, 2, 44100, NULL);
      goto done;
    }


  if (strcmp (suffix, ".raw") == 0 || strcmp (suffix, ".RAW") == 0)
    {
      if (verbose)
	fprintf (stdout, "%s: Playing RAW file.\n", filename);

      decode_sound (fd, UINT_MAX, DEFAULT_FORMAT, DEFAULT_CHANNELS,
                  DEFAULT_SPEED, NULL);
      goto done;
    }

#ifdef MPEG_SUPPORT
  if (strcmp (suffix, ".mpg") == 0 || strcmp (suffix, ".MPG") == 0 ||
      strcmp (suffix, ".mp2") == 0 || strcmp (suffix, ".MP2") == 0)
    {				/* Mpeg file */
      int tmp;

      if (!mpeg_enabled)
	{
	  fprintf (stderr, "%s: Playing MPEG audio files is not available\n",
		   filename);
	  goto done;
	}

      if (verbose)
	fprintf (stdout, "Playing MPEG audio file %s\n", filename);

      if (!setup_device (fd, AFMT_S16_NE, 2, 44100))
	return;

      tmp = APF_NORMAL;
      ioctl (audiofd, SNDCTL_DSP_PROFILE, &tmp);
      play_mpeg (filename, fd, buf, l);
      goto done;
    }
#endif

  fprintf (stderr, "%s: Unrecognized audio file type.\n", filename);
  exitstatus++;
done:
  close (fd);

  ioctl (audiofd, SNDCTL_DSP_SYNC, NULL);
  return;
seekerror:
  exitstatus++;
  close (fd);
}

static void
select_playtgt (char *playtgt)
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
      perror ("SNDCTL_DSP_GET_PLAYTGT_NAMES");
      exit (-1);
    }

  if (ioctl (audiofd, SNDCTL_DSP_GET_PLAYTGT, &src) == -1)
    {
      perror ("SNDCTL_DSP_GET_PLAYTGT");
      exit (-1);
    }

  if (*playtgt == 0 || strcmp (playtgt, "?") == 0)
    {
      printf ("\nPossible playback targets for the selected device:\n\n");

      for (i = 0; i < ei.nvalues; i++)
	{
	  printf ("\t%s", ei.strings + ei.strindex[i]);
	  if (i == src)
	    printf (" (currently selected)");
	  printf ("\n");
	}
      printf ("\n");
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
	      perror ("SNDCTL_DSP_SET_PLAYTGT");
	      exit (-1);
	    }

	  return;
	}
    }

  fprintf (stderr,
	   "Unknown playback target name '%s' - use -o? to get the list\n",
	   playtgt);
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

int
main (int argc, char **argv)
{
  char *prog;
  extern int optind;
  int i, c;

  prog = argv[0];

  while ((c = getopt (argc, argv, "Rqvhfd:o:b:s:c:a:")) != EOF)
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
	  quiet = 1;
	  verbose = 0;
	  break;

	case 'd':
	  if (*optarg >= '0' && *optarg <= '9')	/* Only device number given */
	    find_devname (audio_devname, optarg);
	  else
	    strcpy (audio_devname, optarg);
	  break;

	case 'o':
	  playtgt = optarg;
	  break;

#ifdef MPEG_SUPPORT
	case 'm':
	  mpeg_enabled = 1;
	  break;
#endif

	case 'b':
	  sscanf (optarg, "%d", &force_bits);
	  break;

	case 's':
	  sscanf (optarg, "%d", &force_speed);
	  break;

	case 'c':
	  sscanf (optarg, "%d", &force_channels);
	  break;

	case 'a':
	  sscanf (optarg, "%d", &amplification);
	  break;

	default:
	  usage (prog);
	}

    }

  argc -= optind - 1;
  argv += optind - 1;

  if (argc < 2)
    usage (prog);

  open_device ();

  if (playtgt != NULL)
    select_playtgt (playtgt);

#ifdef SIGQUIT
  signal (SIGQUIT, get_int);
#endif

  for (i = 1; i < argc; i++)
    {
      play_file (argv[i]);
      quitflag = 0;
    }

  close (audiofd);
  return exitstatus;
}
