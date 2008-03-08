/*
 * Purpose: Sources for the ossplay audio player shipped with OSS
 *
 * Description:
 * This is a audio file player that supports most commonly used uncompressed
 * audio formats (.wav, .snd, .au). It doesn't play compressed formats
 * such as MP3.
 *
 * This program is bit old and it uses some OSS features that may no longer be
 * required.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>
#include <soundcard.h>
#include <errno.h>

#undef  MPEG_SUPPORT

#define WAVE_FORMAT_PCM			0x0001
#define WAVE_FORMAT_ADPCM		0x0002
#define WAVE_FORMAT_ALAW		0x0006
#define WAVE_FORMAT_MULAW		0x0007
#define WAVE_FORMAT_IMA_ADPCM		0x0011

/* Magic numbers used in Sun and NeXT audio files (.au/.snd) */
#define SUN_MAGIC 	0x2e736e64	/* Really '.snd' */
#define SUN_INV_MAGIC	0x646e732e	/* '.snd' upside-down */
#define DEC_MAGIC	0x2e736400	/* Really '\0ds.' (for DEC) */
#define DEC_INV_MAGIC	0x0064732e	/* '\0ds.' upside-down */

enum {
 AIFF_FILE,
 AIFC_FILE,
 _8SVX_FILE,
 _16SV_FILE,
 MAUD_FILE
};

int prev_speed = 0, prev_bits = 0, prev_channels = 0;
#ifdef MPEG_SUPPORT
int mpeg_enabled = 0;
#endif
int raw_mode = 0;

int default_speed = 11025, default_bits = 8, default_channels = 1;
static char current_filename[64] = "";
static char *playtgt = NULL;

typedef struct
{
  int coeff1, coeff2;
}
adpcm_coeff;

int verbose = 0, quiet = 0, fixedspeed = 0;
int exitstatus = 0;

int audiofd = 0;

char audio_devname[32] = "/dev/dsp";
extern void play_mpeg (char *, int, unsigned char *, int);

static int be_int (unsigned char *, int);
static void describe_error (void);
static void dump_data (int, int);
static void dump_data_24 (int, int);
static void dump_msadpcm (int, int, int, int, int, adpcm_coeff *);
static char * filepart (char *);
static void find_devname (char *, char *);
static int le_int (unsigned char *, int);
static void select_playtgt (char *);
static int setup_device (int, int, int, int);
static void open_device (void);
static off_t (*oss_lseek) (int, off_t, int) = lseek;
static off_t oss_lseek_stdin (int, off_t, int);
static void play_iff (char *, int, unsigned char *, int);
static void play_au (char *, int, unsigned char *, int);
static void play_file (char *);
static void play_ms_adpcm_wave (char *, int, unsigned char *, int);
static void play_wave (char *, int, unsigned char *, int);
static void play_voc (char *, int, unsigned char *, int);
static void print_verbose (int, int, int);
static void usage (char *);

static off_t
oss_lseek_stdin (int fd, off_t off, int w)
{
  off_t i;
  ssize_t bytes_read;
  char buf[BUFSIZ];

  if (off < 0) return -1;
  if (w == SEEK_END) return -1;
  if (off == 0) return 0;
  i = off;
  while (i > 0)
  {
    bytes_read = read(fd, buf, (i > BUFSIZ)?BUFSIZ:i);
    if (bytes_read == -1) return -1;
    i -= bytes_read;
  }
  return off;
}

static int
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

static int
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
	   "            -s<rate>       Change playback rate of unrecognized files.\n");
  fprintf (stderr,
	   "            -b<bits>       Change number of bits for unrecognized files.\n");
  fprintf (stderr,
	   "            -c<channels>   Change number of channels for unrecognized files.\n");
  fprintf (stderr,
           "            -o<playtgt>|?    Select/Query output target.\n");
  fprintf (stderr,
           "            -R               Open sound device in raw mode.\n");
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

static void
dump_data (int fd, int filesize)
{
  int bsize, l;
  unsigned char buf[1024];

  bsize = sizeof (buf);

  while (filesize)
    {
      l = bsize;

      if (l > filesize)
	l = filesize;


      if ((l = read (fd, buf, l)) <= 0)
	{
	  return;
	}

      if (write (audiofd, buf, l) == -1)
	{
	  perror (audio_devname);
	  exit (-1);
	}

      filesize -= l;
    }
}

static void
dump_data_24 (int fd, int filesize)
{
/*
 * Dump 24 bit packed audio data to the device (after converting to 32 bit).
 */
  int bsize, i, l;
  unsigned char buf[1024];
  int outbuf[1024], outlen = 0;

  int sample_s32;

  bsize = sizeof (buf);

  filesize -= filesize % 3;

  while (filesize >= 3)
    {
      l = bsize - bsize % 3;
      if (l > filesize)
	l = filesize;

      if (l < 3)
	break;

      if ((l = read (fd, buf, l)) <= 0)
	{
	  return;
	}

      outlen = 0;

      for (i = 0; i < l; i += 3)
	{
	  unsigned int *u32 = (unsigned int *) &sample_s32;	/* Alias */

	  /* Read the litle endian input samples */
	  *u32 = (buf[i] << 8) | (buf[i + 1] << 16) | (buf[i + 2] << 24);
	  outbuf[outlen++] = sample_s32;

	}

      if (write (audiofd, outbuf, outlen * sizeof (int)) == -1)
	{
	  perror (audio_devname);
	  exit (-1);
	}

      filesize -= l;
    }
}

/*ARGSUSED*/
static int
setup_device (int fd, int channels, int bits, int speed)
{
  int tmp;

  if (verbose > 4)
    fprintf (stderr, "Setup device %d/%d/%d\n", channels, bits, speed);

  if (speed != prev_speed || bits != prev_bits || channels != prev_channels)
    {
#if 0
      ioctl (audiofd, SNDCTL_DSP_SYNC, 0);
      ioctl (audiofd, SNDCTL_DSP_HALT, 0);
#else
      close (audiofd);
      open_device ();
      if (playtgt != NULL)
	select_playtgt (playtgt);
#endif
    }

  /*
   * Report the current filename as the son name.
   */
  ioctl (audiofd, SNDCTL_SETSONG, current_filename);	/* No error checking */

  prev_speed = speed;
  prev_channels = channels;
  prev_bits = bits;

  tmp = APF_NORMAL;
  ioctl (audiofd, SNDCTL_DSP_PROFILE, &tmp);

  tmp = bits;

  if (ioctl (audiofd, SNDCTL_DSP_SETFMT, &tmp) == -1)
    {
      perror (audio_devname);
      fprintf (stderr, "Failed to select bits/sample\n");
      return 0;
    }

  if (tmp != bits)
    {
      fprintf (stderr, "%s doesn't support this audio format (%x/%x).\n",
	       audio_devname, bits, tmp);
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
  char chn[32], fmt[32];

  if (channels == 1)
    strcpy (chn, "mono");
  else if (channels == 2)
    strcpy (chn, "stereo");
  else
    snprintf (chn, sizeof(channels), "%d channels", channels);

  switch (format)
    {
       case AFMT_VORBIS: /* Not yet implemented in OSS drivers */
       case AFMT_QUERY: snprintf(fmt, sizeof(fmt), "Invallid format"); break;
       case AFMT_IMA_ADPCM: snprintf(fmt, sizeof(fmt), "4 bits"); break;
       case AFMT_MU_LAW:
       case AFMT_A_LAW:
       case AFMT_U8:
       case AFMT_S8: snprintf(fmt, sizeof(fmt), "8 bits"); break;
       case AFMT_S16_LE:
       case AFMT_S16_BE:
       case AFMT_MPEG:
       case AFMT_U16_LE:
       case AFMT_U16_BE: snprintf(fmt, sizeof(fmt), "16 bits"); break;
       case AFMT_S24_LE:
       case AFMT_S24_BE:
       case AFMT_S24_PACKED: snprintf(fmt, sizeof(fmt), "24 bits"); break;
       case AFMT_SPDIF_RAW:
       case AFMT_S32_LE:
       case AFMT_S32_BE: snprintf(fmt, sizeof(fmt), "32 bits"); break;
       case AFMT_FLOAT: snprintf(fmt, sizeof(fmt), "float"); break;
    }
  fprintf (stderr, "%s/%s/%d Hz\n", fmt, chn, speed);
}

static void
play_ms_adpcm_wave (char *filename, int fd, unsigned char *hdr, int l)
{
  int i, n, dataleft, x;

  adpcm_coeff coeff[32] = {
    {256, 0},
    {512, -256},
    {0, 0},
    {192, 64},
    {240, 0},
    {460, -208},
    {392, -232}
  };
  int channels = 1;
  int format = AFMT_U8;
  int speed = 11025;
  int p = 12;
  int nBlockAlign = 256;
  int wSamplesPerBlock = 496, wNumCoeff = 7;
  int fmt = -1;

  /* filelen = le_int (&hdr[4], 4); */

  while (p < l - 16 && memcmp (&hdr[p], "data", 4) != 0)
    {
      n = le_int (&hdr[p + 4], 4);

      if (memcmp (&hdr[p], "fmt ", 4) == 0)
	{
	  if (verbose > 4)
	    fprintf (stderr, "Reading FMT chunk again, len = %d\n", n);

	  fmt = le_int (&hdr[p + 8], 2);
	  channels = le_int (&hdr[p + 10], 2);
	  speed = le_int (&hdr[p + 12], 4);
	  nBlockAlign = le_int (&hdr[p + 20], 2);
	  /* bytes_per_sample = le_int (&hdr[p + 20], 2); */

	  format = AFMT_S16_LE;

	  wSamplesPerBlock = le_int (&hdr[p + 26], 2);
	  wNumCoeff = le_int (&hdr[p + 28], 2);

	  x = p + 30;

	  for (i = 0; i < wNumCoeff; i++)
	    {
	      coeff[i].coeff1 = (short) le_int (&hdr[x], 2);
	      x += 2;
	      coeff[i].coeff2 = (short) le_int (&hdr[x], 2);
	      x += 2;
	    }

	  if (verbose > 3)
            {
              fprintf (stderr, "fmt %04x ", fmt);
              print_verbose (format, channels, speed);
            }
	}

      p += n + 8;
    }

  if (p < l - 16 && memcmp (&hdr[p], "data", 4) == 0)
    {

      dataleft = n = le_int (&hdr[p + 4], 4);
      p += 8;

      if (verbose > 3)
	fprintf (stderr, "DATA chunk. Offs = %d, len = %d\n", p, n);

      if (oss_lseek (fd, p, SEEK_SET) == -1)
	{
	  perror (filename);
	  fprintf (stderr, "Can't seek to the data chunk\n");
	  return;
	}

      if (fmt != WAVE_FORMAT_ADPCM)
	{
	  fprintf (stderr, "%s: Unsupported wave format %04x\n", filename,
		   fmt);
	  return;
	}

      if (verbose)
        {
          fprintf (stderr, "Playing MS ADPCM WAV file %s, ", filename);
          print_verbose (format, channels, speed);
        }

      if (!setup_device (fd, channels, format, speed))
	return;

      dump_msadpcm (fd, dataleft, channels, nBlockAlign,
                    wSamplesPerBlock, coeff);
    }
}

static void
dump_msadpcm (int fd, int dataleft, int channels, int nBlockAlign,
              int wSamplesPerBlock, adpcm_coeff *coeff)
{
  int i, n, nib, max, outp = 0;
  unsigned char buf[4096];
  unsigned char outbuf[64 * 1024];
  int AdaptionTable[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
  };

/*
 * Playback procedure
 */
#define OUT_SAMPLE(s) { \
	if (s>32767)s=32767;else if(s<-32768)s=-32768; \
	outbuf[outp++] = (unsigned char)(s & 0xff); \
	outbuf[outp++] = (unsigned char)((s>>8) & 0xff); \
	n+=2; \
	if (outp>=max){write(audiofd, outbuf, outp);outp=0;}\
	}

#define GETNIBBLE \
	((nib==0) ? \
		(buf[x + nib++] >> 4) & 0x0f : \
		buf[x++ + --nib] & 0x0f \
	)

#if 0
/*
 * There is no idea in using SNDCTL_DSP_GETBLKSIZE in applications like this.
 * Using some fixed local buffer size will work equally well.
 */
      if (ioctl (audiofd, SNDCTL_DSP_GETBLKSIZE, &max) == -1)
	perror ("SNDCTL_DSP_GETBLKSIZE");
#else
      max = 1024;
#endif
      outp = 0;

      while (dataleft > nBlockAlign &&
	     read (fd, buf, nBlockAlign) == nBlockAlign)
	{
	  int predictor[2], delta[2], samp1[2], samp2[2];

	  int x = 0;
	  dataleft -= nBlockAlign;

	  nib = 0;
	  n = 0;

	  for (i = 0; i < channels; i++)
	    {
	      predictor[i] = buf[x];
	      x++;
	    }

	  for (i = 0; i < channels; i++)
	    {
	      delta[i] = (short) le_int (&buf[x], 2);
	      x += 2;
	    }

	  for (i = 0; i < channels; i++)
	    {
	      samp1[i] = (short) le_int (&buf[x], 2);
	      x += 2;
	      OUT_SAMPLE (samp1[i]);
	    }

	  for (i = 0; i < channels; i++)
	    {
	      samp2[i] = (short) le_int (&buf[x], 2);
	      x += 2;
	      OUT_SAMPLE (samp2[i]);
	    }

	  while (n < (wSamplesPerBlock * 2 * channels))
	    for (i = 0; i < channels; i++)
	      {
		int pred, new, error_delta, i_delta;

		pred = ((samp1[i] * coeff[predictor[i]].coeff1)
			+ (samp2[i] * coeff[predictor[i]].coeff2)) / 256;
		i_delta = error_delta = GETNIBBLE;

		if (i_delta & 0x08)
		  i_delta -= 0x10;	/* Convert to signed */

		new = pred + (delta[i] * i_delta);
		OUT_SAMPLE (new);

		delta[i] = delta[i] * AdaptionTable[error_delta] / 256;
		if (delta[i] < 16)
		  delta[i] = 16;

		samp2[i] = samp1[i];
		samp1[i] = new;
	      }
	}

  if (outp > 0)
    write (audiofd, outbuf, outp /*(outp+3) & ~3 */ );
}

static void
play_wave (char *filename, int fd, unsigned char *hdr, int l)
{
  int filelen;

  int n;

  int channels = 1;
  int bits = 8;
  int format = AFMT_U8;
  int speed = 11025;
  int p = 12;
  int fmt = -1;

  filelen = le_int (&hdr[4], 4);

  if (verbose > 2)
    fprintf (stderr, "Filelen = %d\n", filelen);

  while (p < l - 16 && memcmp (&hdr[p], "data", 4) != 0)
    {
      n = le_int (&hdr[p + 4], 4);

      if (memcmp (&hdr[p], "fmt ", 4) == 0)
	{
	  if (verbose > 3)
	    fprintf (stderr, "FMT chunk, len = %d\n", n);

	  fmt = le_int (&hdr[p + 8], 2);
	  channels = le_int (&hdr[p + 10], 2);
	  speed = le_int (&hdr[p + 12], 4);
	  /* bytes_per_sec = le_int (&hdr[p + 16], 4); */
	  /* bytes_per_sample = le_int (&hdr[p + 20], 2); */

	  bits = le_int (&hdr[p + 22], 2);

	  if (fmt == WAVE_FORMAT_ADPCM)
	    {
	      play_ms_adpcm_wave (filename, fd, hdr, l);
	      return;
	    }

	  if (verbose > 3)
	    fprintf (stderr, "fmt %04x, bits %d, chn %d, speed %d\n",
		     fmt, bits, channels, speed);
	}

      p += n + 8;
    }

  if (p < l - 16 && memcmp (&hdr[p], "data", 4) == 0)
    {

      n = le_int (&hdr[p + 4], 4);
      p += 8;

      if (verbose > 3)
	fprintf (stderr, "DATA chunk. Offs = %d, len = %d\n", p, n);

      if (oss_lseek (fd, p, SEEK_SET) == -1)
	{
	  perror (filename);
	  fprintf (stderr, "Can't seek to the data chunk\n");
	  return;
	}


      if (fmt != WAVE_FORMAT_PCM)
	switch (fmt)
	  {
	  case WAVE_FORMAT_ALAW:
	    if (verbose > 1)
	      fprintf (stderr, "ALAW encoded wave file\n");
	    format = AFMT_A_LAW;
	    break;

	  case WAVE_FORMAT_MULAW:
	    if (verbose > 1)
	      fprintf (stderr, "MULAW encoded wave file\n");
	    format = AFMT_MU_LAW;
	    break;

	  case WAVE_FORMAT_IMA_ADPCM:
	    if (verbose > 1)
	      fprintf (stderr, "IMA ADPCM encoded wave file\n");
	    format = AFMT_IMA_ADPCM;
	    break;

	  default:
	    fprintf (stderr, "%s: Unsupported wave format %04x\n", filename,
		     fmt);
	    return;
	  }
      else format = bits;

      if (bits == 32)
	{
	  format = AFMT_S32_LE;
	}
      else if (bits == 24)
	{
	  format = AFMT_S32_LE;
	}

      if (verbose)
        {
           fprintf(stderr, "Playing WAV file %s, ", filename);
           print_verbose (format, channels, speed);
        }

      if (!setup_device (fd, channels, format, speed))
	return;

      if (bits != 24)
	{
	  dump_data (fd, n);
	}
      else
	{
	  dump_data_24 (fd, n);
	}
    }
}

/*ARGSUSED*/
static void
play_au (char *filename, int fd, unsigned char *hdr, int l)
{

  char *fmts = "<Unknown format>";
  int filelen;

  int i;

  int channels = 1;
  int format = AFMT_U8;
  int speed = 11025;
  int p = 24;
  int fmt = -1;

  p = be_int (&hdr[4], 4);
  filelen = be_int (&hdr[8], 4);
  fmt = be_int (&hdr[12], 4);
  speed = be_int (&hdr[16], 4);
  channels = be_int (&hdr[20], 4);

  if (filelen == -1)
    filelen = 0x7fffffff;

  switch (fmt)
    {
    case 1:
      format = AFMT_MU_LAW;
      fmts = "mu-law";
      break;

    case 2:
      format = AFMT_S8;
      fmts = "8 bit";
      break;

    case 3:
      format = AFMT_S16_BE;
      fmts = "16 bit";
      break;

    case 4:
      format = AFMT_S24_BE;
      fmts = "24 bit";
      break;

    case 5:
      format = AFMT_S32_BE;
      fmts = "32 bit";
      break;

    case 6:
    case 7:
      fprintf (stderr, "Floating point encoded .au files are not supported");
      fmts = "floating point";
      break;

    case 23:
    case 24:
    case 25:
    case 26:
      fprintf (stderr, "G.72x ADPCM encoded .au files are not supported");
      fmts = "ADPCM";
      break;

    case 27:
      format = AFMT_A_LAW;
      fmts = "A-Law";
      break;

    default:

      fprintf (stderr, "%s: Unknown encoding %d.\n", filename, fmt);
      return;
    }

  if (oss_lseek (fd, p, SEEK_SET) == -1)
    {
      perror (filename);
      fprintf (stderr, "Can't seek to the data chunk\n");
      return;
    }

  if (verbose)
    {
      fprintf (stderr, "Playing .au file %s, %s/%s/%d Hz\n",
	       filename, fmts, (channels == 1) ? "mono" : "stereo", speed);

      if (verbose > 1)
	{
	  fprintf (stderr, "Annotations: ");
	  for (i = 24; i < p; i++)
	    if (isprint (hdr[i]))
	      fprintf (stderr, "%c", hdr[i]);
	    else
	      fprintf (stderr, ".");
	  fprintf (stderr, "\n");
	}
    }

  if (!setup_device (fd, channels, format, speed))
    return;

  dump_data (fd, filelen);
}

/*ARGSUSED*/
static void
play_iff (char *filename, int fd, unsigned char *buf, int type)
{
  enum
  {
    COMM_BIT,
    SSND_BIT,
    FVER_BIT
  };
#define COMM_FOUND (1 << COMM_BIT)
#define SSND_FOUND (1 << SSND_BIT)
#define FVER_FOUND (1 << FVER_BIT)

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
#define ms02_FMT 0x6D730002
#define ms11_FMT 0x6D730011
#define raw_FMT  0x72617720
#define in24_FMT 0x696e3234
#define ni24_FMT 0x6e693234
#define in32_FMT 0x696E3332
#define ni32_FMT 0x6E693332

#define SEEK(fd, offset, n) \
  do { \
    if (oss_lseek (fd, offset, SEEK_CUR) == -1) \
      { \
        fprintf (stderr, "%s: error: cannot seek to end of " #n "chunk.\n", \
                 filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
  } while (0)

#define READ(fd, buf, len, n) \
  do { \
    if (chunk_size < len) \
      { \
        fprintf (stderr, \
                 "%s: error: chunk " #n "size is too small.\n", filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
    if (read(fd, buf, len) < len) \
      { \
        fprintf (stderr, "%s: error: cannot read " #n "chunk.\n", filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
    SEEK (fd, chunk_size - len, n); \
  } while (0)

  int channels = 1, bits = 8, format, speed = 11025;
  int found = 0, chunk_size = 18, sound_size = 0, total_size;
  long double COMM_rate;
  unsigned int chunk_id, timestamp, sound_loc = 0, offset = 0, csize = 12;

  if (type == _16SV_FILE) format = AFMT_S16_BE;
  else format = AFMT_S8;

  total_size = be_int (buf + 4, 4);
  do
    {
      if (read (fd, buf, 8) < 8)
        {
          fprintf (stderr, "%s: Cannot read AIFF chunk header at pos %d\n",
                           filename, csize);
          if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta;
          return;
        }
      chunk_id = be_int (buf, 4);
      chunk_size = be_int (buf + 4, 4);
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
            if (type == AIFC_FILE) READ (fd, buf, 22, COMM);
            else READ (fd, buf, 18, COMM);
            found |= COMM_FOUND;

            channels = be_int (buf, 2);
#if 0
            num_frames = be_int (buf + 2, 4); /* ossplay doesn't use this */
#endif
            bits = be_int (buf + 6, 2);
            bits += bits % 8;
            switch (bits)
              {
                 case 8: format = AFMT_S8; break;
                 case 16: format = AFMT_S16_BE; break;
                 case 24: format = AFMT_S24_BE; break;
                 case 32: format = AFMT_S32_BE; break;
                 default: format = AFMT_S16_BE; break;
              }
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
                case sowt_FMT:
                /* Apple Docs refer to this as AFMT_S16_LE only, but some
                   programs misinterpret this. */
                  switch (bits)
                    {
                      case 8: format = AFMT_S8; break;
                      case 16: format = AFMT_S16_LE; break;
                      case 24: format = AFMT_S24_LE; break;
                      case 32: format = AFMT_S32_LE; break;
                      default: format = AFMT_S16_LE; break;
                    } break;
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
                case ms11_FMT:
                case ima4_FMT: format = AFMT_IMA_ADPCM; break;
#if 0
                case fl32_FMT:
                case FL32_FMT: format = AFMT_FLOAT; break;
                case ms02_FMT: format = -1; break; /* MS ADPCM */
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
            SEEK (fd, chunk_size - 8, SSND);
            break;
          case FVER_HUNK:
            READ (fd, buf, 4, FVER);
            timestamp = be_int (buf, 4);
            found |= FVER_FOUND;
            break;

          /* 8SVX chunks */
          case VHDR_HUNK: READ (fd, buf, 14, VHDR);
                          speed = be_int (buf + 12, 2);
                          found |= COMM_FOUND;
                          break;
          case MDAT_HUNK: /* MAUD chunk */
          case BODY_HUNK: sound_size = chunk_size;
                          sound_loc = csize + 4;
                          found |= SSND_FOUND;
                          if ((!strcmp (filename, "-")) &&
                              (oss_lseek == oss_lseek_stdin))
                             goto stdinext;
                          SEEK (fd, chunk_size, BODY);
                          break;
          case CHAN_HUNK: READ (fd, buf, 4, CHAN);
                          channels = be_int (buf, 4);
                          channels = (channels & 0x01) +
                                     ((channels & 0x02) >> 1) +
                                     ((channels & 0x04) >> 2) +
                                     ((channels & 0x08) >> 3);
                          break;

          /* MAUD chunks */
          case MHDR_HUNK: READ (fd, buf, 20, MHDR);
                          bits = be_int (buf + 4, 2);
                          switch (bits)
                            {
                              case 8: format = AFMT_S8; break;
                              case 16: format = AFMT_S16_BE; break;
                              case 24: format = AFMT_S24_BE; break;
                              case 32: format = AFMT_S32_BE; break;
                              default: format = AFMT_S16_BE; break;
                            }
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

          /* common chunks */
          case NAME_HUNK:
          case AUTH_HUNK:
          case ANNO_HUNK:
          case COPY_HUNK:
            if (verbose > 3)
              {
                int i, len;

                fprintf (stderr, "%s: ", filename);
                if (chunk_size > 1024) len = 1024;
                else len = chunk_size;
                switch (chunk_id)
                  {
                    case NAME_HUNK: fprintf (stderr, "Name: ");
                                    READ (fd, buf, len, NAME);
                                    break;
                    case AUTH_HUNK: fprintf (stderr, "Author: ");
                                    READ (fd, buf, len, AUTH);
                                    break;
                    case COPY_HUNK: fprintf (stderr, "Copyright: ");
                                    READ (fd, buf, len, COPY);
                                    break;
                    case ANNO_HUNK: fprintf (stderr, "Annonations: ");
                                    READ (fd, buf, len, ANNO);
                                    break;
                  }
                for (i = 0; i < len; i++)
                  fprintf (stderr, "%c", buf[i]?buf[i]:' ');
                fprintf (stderr, "\n");
                break;
              }

          default: SEEK (fd, chunk_size, UNKNOWN);
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
        fprintf(stderr, "Playing AIFF file %s, ", filename);
      else if (type == AIFC_FILE)
        fprintf(stderr, "Playing AIFC file %s, ", filename);
      else if (type == _8SVX_FILE)
        fprintf(stderr, "Playing 8SVX file %s, ", filename);
      else if (type == _16SV_FILE)
        fprintf(stderr, "Playing 16SV file %s, ", filename);
      else
        fprintf(stderr, "Playing MAUD file %s, ", filename);
      print_verbose (format, channels, speed);
    }

  if (!setup_device (fd, channels, format, speed))
    return;

  dump_data (fd, sound_size);
  return;
}

/*ARGSUSED*/
static void
play_voc (char *filename, int fd, unsigned char *hdr, int l)
{
  int data_offs, vers, id, len, blklen;
  unsigned char buf[256], block[256];
  int speed = 11025;
  int channels = 1, bits = 8;
  int fmt, tmp;

  int loopcount = 0, loopoffs = 4;

  data_offs = le_int (&hdr[0x14], 2);
  vers = le_int (&hdr[0x16], 2);
  id = le_int (&hdr[0x18], 2);

  if ((((~vers) + 0x1234) & 0xffff) != id)
    {
      fprintf (stderr, "%s: Not a valid .VOC file\n", filename);
      return;
    }

  if (verbose)
    fprintf (stderr, "Playing .VOC file %s\n", filename);

   /*LINTED*/ while (1)
    {
      oss_lseek (fd, data_offs, SEEK_SET);

      if (read (fd, buf, 1) != 1)
	return;

      if (buf[0] == 0)
	return;			/* End */

      if (read (fd, &buf[1], 3) != 3)
	{
	  fprintf (stderr, "%s: Truncated .VOC file (%d)\n",
		   filename, buf[0]);
	  return;
	}

      blklen = len = le_int (&buf[1], 3);

      if (verbose > 3)
	fprintf (stderr, "%0x: Block type %d, len %d\n",
		 data_offs, buf[0], len);
      switch (buf[0])
	{

	case 1:		/* Sound data block */
	  if (read (fd, block, 2) != 2)
	    return;

	  tmp = 256 - block[0];	/* Time constant */
	  speed = (1000000 + tmp / 2) / tmp;

	  fmt = block[1];
	  len -= 2;

	  if (fmt != 0)
	    {
	      fprintf (stderr,
		       "%s: Only PCM encoded .VOC file supported\n",
		       filename);
	      return;
	    }

	  if (!setup_device (fd, channels, bits, speed))
	    return;

	case 2:		/* Continuation data */
	  dump_data (fd, len);
	  break;

	case 3:		/* Silence */
	  if (read (fd, block, 3) != 3)
	    return;
	  len = le_int (block, 2);
	  tmp = 256 - block[2];	/* Time constant */
	  speed = (1000000 + tmp / 2) / tmp;
	  if (!setup_device (fd, 1, 8, speed))
	    return;
	  {
	    int i;
	    unsigned char empty[1024];
	    for (i = 0; i < 1024; i++)
	      empty[i] = 0x80;

	    while (len > 0)
	      {
		i = 1024;
		if (i > len)
		  i = len;
		write (audiofd, empty, i);

		len -= i;
	      }
	    if (!fixedspeed)
	      ioctl (audiofd, SNDCTL_DSP_POST, 0);
	  }
	  break;

	case 6:		/* Loop start */
	  if (read (fd, block, 2) != 2)
	    return;
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
	  if (read (fd, block, 4) != 4)
	    return;

	  bits = 8;
	  channels = block[3] + 1;
	  fmt = block[2];
	  if (fmt != 0)
	    {
	      fprintf (stderr,
		       "%s: Only PCM encoded .VOC file supported\n",
		       filename);
	      return;
	    }
	  break;

	case 9:		/* New format sound data */
	  if (read (fd, block, 12) != 12)
	    return;

	  len -= 12;

	  speed = le_int (&block[0], 2);
	  bits = block[2];
	  channels = block[3];
	  fmt = le_int (&block[4], 2);

	  if (fmt != 0 && fmt != 4 && fmt != 7)
	    {
	      fprintf (stderr,
		       "%s: Only PCM or mu-Law encoded .VOC file supported\n",
		       filename);
	      return;
	    }

	  if (fmt == 7)
	    bits = AFMT_MU_LAW;

	  if (!setup_device (fd, channels, bits, speed))
	    return;
	  dump_data (fd, len);
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

  strncpy (current_filename, filepart (filename),
	   sizeof (current_filename) - 1);
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
 * Try to detect the file type - First .aiff
 */

  if (l > 11 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "AIFF", 4) == 0)
    {
      play_iff (filename, fd, buf, AIFF_FILE);
      goto done;
    }

  if (l > 11 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "AIFC", 4) == 0)
    {
      play_iff (filename, fd, buf, AIFC_FILE);
      goto done;
    }

  if (l > 11 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "8SVX", 4) == 0)
    {
      play_iff (filename, fd, buf, _8SVX_FILE);
      goto done;
    }

  if (l > 11 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "16SV", 4) == 0)
    {
      play_iff (filename, fd, buf, _16SV_FILE);
      goto done;
    }

  if (l > 11 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "MAUD", 4) == 0)
    {
      play_iff (filename, fd, buf, MAUD_FILE);
      goto done;
    }

  if ((i = read (fd, buf + 12, sizeof (buf) - 12)) == -1)
    {
      perror (filename);
      goto seekerror;
    }
  l += i;

  oss_lseek (fd, 0, SEEK_SET);	/* Start from the beginning */

  if (l > 16 &&
      memcmp (&buf[0], "RIFF", 4) == 0 && memcmp (&buf[8], "WAVE", 4) == 0)
    {
      play_wave (filename, fd, buf, l);
      goto done;
    }

  if (l > 24 &&
      (*(unsigned int *) &buf[0] == SUN_MAGIC ||
       *(unsigned int *) &buf[0] == DEC_MAGIC ||
       *(unsigned int *) &buf[0] == SUN_INV_MAGIC ||
       *(unsigned int *) &buf[0] == DEC_INV_MAGIC))
    {
      play_au (filename, fd, buf, l);
      goto done;
    }

  if (l > 32 && memcmp (&buf[0], "Creative Voice File", 16) == 0)
    {
      play_voc (filename, fd, buf, l);
      goto done;
    }

/*
 *	The file was not identified by it's content. Try using the file name
 *	suffix.
 */

  suffix = filename + strlen (filename) - 1;

  while (suffix != filename && *suffix != '.')
    suffix--;

  if (strcmp (suffix, ".au") == 0 || strcmp (suffix, ".AU") == 0)
    {				/* Raw mu-Law data */

      if (verbose)
	fprintf (stderr, "Playing raw mu-Law file %s\n", filename);

      if (!setup_device (fd, 1, AFMT_MU_LAW, 8000))
	return;

      dump_data (fd, 0x7fffffff);
      goto done;
    }

  if (strcmp (suffix, ".snd") == 0 || strcmp (suffix, ".SND") == 0)
    {
      if (!quiet)
	fprintf (stderr,
		 "%s: Unknown format. Assuming RAW audio (%d/%d/%d.\n",
		 filename, default_speed, default_bits, default_channels);

      if (!setup_device (fd, default_channels, default_bits, default_speed))
	return;

      dump_data (fd, 0x7fffffff);
      goto done;
    }

  if (strcmp (suffix, ".cdr") == 0 || strcmp (suffix, ".CDR") == 0)
    {
      if (verbose)
	fprintf (stderr, "%s: Playing CD-R (cdwrite) file.\n", filename);

      if (!setup_device (fd, 2, AFMT_S16_BE, 44100))
	return;

      dump_data (fd, 0x7fffffff);
      goto done;
    }


  if (strcmp (suffix, ".raw") == 0 || strcmp (suffix, ".RAW") == 0)
    {
      if (verbose)
	fprintf (stderr, "%s: Playing RAW file.\n", filename);

      if (!setup_device (fd, default_channels, default_bits, default_speed))
	return;

      dump_data (fd, 0x7fffffff);
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
	fprintf (stderr, "Playing MPEG audio file %s\n", filename);

      if (!setup_device (fd, 2, AFMT_S16_NE, 44100))
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

  if (!fixedspeed)
    ioctl (audiofd, SNDCTL_DSP_SYNC, 0);
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

int
main (int argc, char *argv[])
{
  char *prog;
  extern int optind;
  int i, c;

  prog = argv[0];

  while ((c = getopt (argc, argv, "Rqvhfd:o:b:s:c:")) != EOF)
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

	case 'f':
	  fixedspeed = 1;
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
	  sscanf (optarg, "%d", &default_bits);
	  break;

	case 's':
	  sscanf (optarg, "%d", &default_speed);
	  break;

	case 'c':
	  sscanf (optarg, "%d", &default_channels);
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

  for (i = 1; i < argc; i++)
    play_file (argv[i]);

  close (audiofd);
  return exitstatus;
}
