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
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

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
char *prog;

int audiofd = 0;

char audio_devname[32] = "/dev/dsp";
extern void play_mpeg (char *in_name, int fd, unsigned char *hdr, int l);
static void select_playtgt (char *playtgt);

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

void
usage (void)
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
  exit (-1);
}

void
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
play_ms_adpcm_wave (char *filename, int fd, unsigned char *hdr, int l)
{
  int i, n, dataleft, x;

  adpcm_coeff coeff[32];
  static int AdaptionTable[] = { 230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
  };

  unsigned char buf[4096];

  int channels = 1;
  int bits = 8;
  int speed = 11025;
  int p = 12, max, outp;
  int nBlockAlign;
  int wSamplesPerBlock, wNumCoeff;
  int fmt = -1;
  int nib;

  unsigned char outbuf[64 * 1024];

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

	  bits = AFMT_S16_LE;

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
	    fprintf (stderr, "fmt %04x, bits %d, chn %d, speed %d\n",
		     fmt, bits, channels, speed);
	}

      p += n + 8;
    }

  if (p < l - 16 && memcmp (&hdr[p], "data", 4) == 0)
    {

      dataleft = n = le_int (&hdr[p + 4], 4);
      p += 8;

      if (verbose > 3)
	fprintf (stderr, "DATA chunk. Offs = %d, len = %d\n", p, n);

      if (lseek (fd, p, SEEK_SET) == -1)
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
	fprintf (stderr, "Playing MS ADPCM .WAV file %s, %s/%d Hz\n",
		 filename, (channels == 1) ? "mono" : "stereo", speed);

      if (!setup_device (fd, channels, bits, speed))
	return;

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
  int file_bits = 8;
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

      if (lseek (fd, p, SEEK_SET) == -1)
	{
	  perror (filename);
	  fprintf (stderr, "Can't seek to the data chunk\n");
	  return;
	}


      if (verbose)
	{
	  char chn[32];
	  if (channels == 1)
	    strcpy (chn, "mono");
	  else if (channels == 2)
	    strcpy (chn, "stereo");
	  else
	    sprintf (chn, "%d channels", channels);

	  fprintf (stderr, "Playing .WAV file %s, %d bits/%s/%d Hz\n",
		   filename, bits, chn, speed);
	}

      if (fmt != WAVE_FORMAT_PCM)
	switch (fmt)
	  {
	  case WAVE_FORMAT_ALAW:
	    if (verbose > 1)
	      fprintf (stderr, "ALAW encoded wave file\n");
	    bits = AFMT_A_LAW;
	    break;

	  case WAVE_FORMAT_MULAW:
	    if (verbose > 1)
	      fprintf (stderr, "MULAW encoded wave file\n");
	    bits = AFMT_MU_LAW;
	    break;

	  case WAVE_FORMAT_IMA_ADPCM:
	    if (verbose > 1)
	      fprintf (stderr, "IMA ADPCM encoded wave file\n");
	    bits = AFMT_IMA_ADPCM;
	    break;

	  default:
	    fprintf (stderr, "%s: Unsupported wave format %04x\n", filename,
		     fmt);
	    return;
	  }

      file_bits = bits;

      if (bits == 32)
	{
	  bits = AFMT_S32_LE;
	}
      else if (bits == 24)
	{
	  bits = AFMT_S32_LE;
	}
      if (!setup_device (fd, channels, bits, speed))
	return;

      if (file_bits != 24)
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
  int bits = 8;
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
      bits = AFMT_MU_LAW;
      fmts = "mu-law";
      break;

    case 2:
      bits = AFMT_S8;
      fmts = "8 bit";
      break;

    case 3:
      bits = AFMT_S16_BE;
      fmts = "16 bit";
      break;

    case 4:
      bits = AFMT_S24_BE;
      fmts = "24 bit";
      break;

    case 5:
      bits = AFMT_S32_BE;
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
      bits = AFMT_A_LAW;
      fmts = "A-Law";
      break;

    default:

      fprintf (stderr, "%s: Unknown encoding %d.\n", filename, fmt);
      return;
    }

  if (lseek (fd, p, SEEK_SET) == -1)
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

  if (!setup_device (fd, channels, bits, speed))
    return;

  dump_data (fd, filelen);
}

/*ARGSUSED*/
static void
play_aiff (char *filename, int fd, unsigned char *hdr, int l)
{

#if 0
  int channels = 1;
  int bits = 8;
  int speed = 11025;
  int p = 24;
#endif

  fprintf (stderr, "AIFF files are not supported yet (%s)\n", filename);
  return;

#if 0
  if (lseek (fd, p, SEEK_SET) == -1)
    {
      perror (filename);
      fprintf (stderr, "Can't seek to the data chunk\n");
      return;
    }

  if (verbose)
    {
      fprintf (stderr, "Playing AIFF file %s, %dbits/%s/%d Hz\n",
	       filename, bits, (channels == 1) ? "mono" : "stereo", speed);

    }

  if (!setup_device (fd, channels, bits, speed))
    return;

  dump_data (fd, filelen);
#endif
}

static void
play_8svx (char *filename, int fd, unsigned char *hdr, int l)
{

  int n;

  int channels = 1;
  int bits = AFMT_S8;
  int speed = 11025;
  int p = 24;

  /* filelen = be_int (&hdr[4], 4); */

  p = 12;

  while (p < l - 16 && memcmp (&hdr[p], "BODY", 4) != 0)
    {
      n = be_int (&hdr[p + 4], 4);

      if (memcmp (&hdr[p], "NAME", 4) == 0)
	{
	  if (verbose > 3)
	    fprintf (stderr, "%s: Name: %s\n", filename, hdr + p + 8);
	}
      else if (memcmp (&hdr[p], "ANNO", 4) == 0)
	{
	  if (verbose > 3)
	    fprintf (stderr, "%s: Annotations: %s\n", filename, hdr + p + 8);
	}
      else if (memcmp (&hdr[p], "AUTH", 4) == 0)
	{
	  if (verbose > 3)
	    fprintf (stderr, "%s: Author: %s\n", filename, hdr + p + 8);
	}
      else if (memcmp (&hdr[p], "VHDR", 4) == 0)
	{
	  speed = be_int (&hdr[p + 20], 2);
	}
      else if (memcmp (&hdr[p], "CHAN", 4) == 0)
	{
	  channels = be_int (&hdr[p + 8], 4);
	  channels = (channels & 0x01) +
	    ((channels & 0x02) >> 1) +
	    ((channels & 0x04) >> 2) + ((channels & 0x08) >> 3);
	}

      p += n + 8;

      p = (p + 1) & ~1;
    }

  if (p >= l - 16 || memcmp (&hdr[p], "BODY", 4) != 0)
    {
      fprintf (stderr, "%s: File data not found.\n", filename);
      return;
    }

  n = be_int (&hdr[p + 4], 4);
  p += 8;

  if (verbose)
    fprintf (stderr, "Playing 8SVX file %s, %d bits/%s/%d Hz\n",
	     filename, 8 /* bits */ , (channels == 1) ? "mono" : "stereo",
	     speed);

  if (lseek (fd, p, SEEK_SET) == -1)
    {
      perror (filename);
      fprintf (stderr, "Can't seek to the data chunk\n");
      return;
    }

  if (!setup_device (fd, channels, bits, speed))
    return;

  dump_data (fd, n);
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

  int loopcount = 0, loopoffs;

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
      lseek (fd, data_offs, SEEK_SET);

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
  int fd, l;
  unsigned char buf[1024];
  char *suffix;

  if ((fd = open (filename, O_RDONLY, 0)) == -1)
    {
      perror (filename);
      exitstatus++;
      return;
    }

  strncpy (current_filename, filepart (filename),
	   sizeof (current_filename) - 1);
  current_filename[sizeof (current_filename) - 1] = 0;

  if ((l = read (fd, buf, sizeof (buf))) == -1)
    {
      perror (filename);
      exitstatus++;
      close (fd);
      return;
    }

  if (l == 0)
    {
      fprintf (stderr, "%s is empty file.\n", filename);
      exitstatus++;
      close (fd);
      return;
    }

  lseek (fd, 0, SEEK_SET);	/* Start from the beginning */

/*
 * Try to detect the file type - First .wav
 */


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

  if (l > 16 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "AIFF", 4) == 0)
    {
      play_aiff (filename, fd, buf, l);
      goto done;
    }

  if (l > 16 &&
      memcmp (&buf[0], "FORM", 4) == 0 && memcmp (&buf[8], "8SVX", 4) == 0)
    {
      play_8svx (filename, fd, buf, l);
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
      printf ("\nPossible recording sources for the selected device:\n\n");

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
	   "Unknown playback target name '%s' - use -i? to get the list\n",
	   playtgt);
  exit (-1);
}

int
main (int argc, char *argv[])
{
  int i, n, c;

  prog = argv[0];

  n = argc;
  i = 1;

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
	  usage ();
	}

      i++;
    }

  if (i > 1)
    {
      argc -= i - 1;
      argv += i - 1;
    }

  open_device ();

  if (playtgt != NULL)
    select_playtgt (playtgt);

  if (argc < 2)
    usage ();

  for (i = 1; i < argc; i++)
    play_file (argv[i]);

  close (audiofd);
  return exitstatus;
}
