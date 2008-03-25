#include "ossplay.h"
#include "dump.h"

extern int force_speed, force_bits, force_channels;
extern int audiofd, quitflag;
extern char audio_devname[32];

static int dump_data_24 (int, unsigned int, int);
static int dump_creative_adpcm (int, unsigned int, int);
static int dump_fibonacci_delta (int, unsigned int, int);
static int dump_msadpcm (int, unsigned int, int, int, int, adpcm_coeff *);
static int dump_creative_adpcm (int, unsigned int, int);

#define OREAD(fd, buf, len) \
  do { \
    if (quitflag == 1) \
      { \
        quitflag = 0; \
        ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL); \
        return -1; \
      } \
    if ((l = read (fd, buf, len)) <= 0) \
      { \
        if (quitflag == 1) \
          { \
            quitflag = 0; \
            ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL); \
            return -1; \
          } \
        return 0; \
      } \
  } while(0)

#define OWRITE(fd, buf, len) \
  do { \
   if (write (fd, buf, len) == -1) \
     { \
        if ((errno == EINTR) && (quitflag == 1)) \
          { \
            quitflag = 0; \
            ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL); \
            return -1; \
          } \
        perror (audio_devname); \
        exit (-1); \
     } \
  } while (0)

int
dump_sound (int fd, unsigned int filesize, int format, int channels,
            int speed, void * metadata)
{
  if (force_speed != -1) speed = force_speed;
  if (force_channels != -1) channels = force_channels;
  if (force_bits != -1) format = force_bits;
  switch (format)
    {
      case AFMT_MS_ADPCM:
        if (!setup_device (fd, AFMT_S16_LE, channels, speed)) return -2;
        else
          {
            msadpcm_values_t * val = (msadpcm_values_t *)metadata;

            return dump_msadpcm (fd, filesize, channels, val->nBlockAlign,
                                 val->wSamplesPerBlock, val->coeff);
          }
      case AFMT_CR_ADPCM_2:
      case AFMT_CR_ADPCM_3:
      case AFMT_CR_ADPCM_4:
        if (!setup_device (fd, AFMT_U8, channels, speed)) return -2;
        return dump_creative_adpcm (fd, filesize, format);
      case AFMT_FIBO_DELTA:
      case AFMT_EXP_DELTA:
        if (!setup_device (fd, AFMT_U8, channels, speed)) return -2;
        return dump_fibonacci_delta (fd, filesize, format);
      case AFMT_S24_LE:
        if (!setup_device (fd, AFMT_S32_NE, channels, speed)) return -2;
        return dump_data_24 (fd, filesize, 0);
      case AFMT_S24_BE:
        if (!setup_device (fd, AFMT_S32_NE, channels, speed)) return -2;
        return dump_data_24 (fd, filesize, 1);
      default:
        if (!setup_device (fd, format, channels, speed)) return -2;
        return dump_data (fd, filesize);
    }
  return 0;
}

int
dump_data (int fd, unsigned int filesize)
{
  int bsize, l;
  unsigned char buf[1024];

  bsize = sizeof (buf);

  while (filesize)
    {
      l = bsize;

      if (l > filesize)
	l = filesize;

      OREAD (fd, buf, l);
      OWRITE (audiofd, buf, l);

      filesize -= l;
    }

  return 0;
}

static int
dump_data_24 (int fd, unsigned int filesize, int big_endian)
{
/*
 * Dump 24 bit packed audio data to the device (after converting to 32 bit).
 */
  int bsize, i, l;
  unsigned char buf[1024];
  int outbuf[1024], outlen = 0;

  int sample_s32, v1;

  bsize = sizeof (buf);

  filesize -= filesize % 3;

  if (big_endian) v1 = 24;
  else v1 = 8;

  while (filesize >= 3)
    {
      l = bsize - bsize % 3;
      if (l > filesize)
	l = filesize;

      if (l < 3)
	break;

      OREAD (fd, buf, l);

      outlen = 0;

      for (i = 0; i < l; i += 3)
	{
	  unsigned int *u32 = (unsigned int *) &sample_s32;	/* Alias */

	  *u32 = (buf[i] << v1) | (buf[i + 1] << 16) | (buf[i + 2] << (32-v1));
	  outbuf[outlen++] = sample_s32;
	}

      OWRITE (audiofd, outbuf, outlen * sizeof (int));

      filesize -= l;
    }
  return 0;
}

static int
dump_fibonacci_delta (int fd, unsigned int filesize, int format)
{
  const char CodeToDelta[16] = { -34, -21, -13, -8, -5, -3, -2, -1,
                                  0, 1, 2, 3, 5, 8, 13, 21 };
  const char CodeToExpDelta[16] = { -128, -64, -32, -16, -8, -4, -2, -1,
                                     0, 1, 2, 4, 8, 16, 32, 64 };

  int bsize, l, i, x;
  unsigned char buf[1024], obuf[2048], d;
  const char * table;

  if (format == AFMT_EXP_DELTA) table = CodeToExpDelta;
  else table = CodeToDelta;

  bsize = sizeof (buf);

  OREAD (fd, buf, 1);
  x = *buf;
  OWRITE (audiofd, buf, 1);

  while (filesize)
    {
      l = bsize;

      if (l > filesize)
        l = filesize;

      OREAD (fd, buf, l);
      for (i = 1; i < 2*l; ++i)
        {
          d = buf[i/2];
          if (i & 1) d &= 0xF;
          else d >>= 4;
          x += CodeToDelta[d];
          if (x > 255) x = 255;
          if (x < 0) x = 0;
          obuf[i] = x;
        }
      OWRITE (audiofd, obuf, 2*l);

      filesize -= l;
    }

  return 0;
}

static int
dump_msadpcm (int fd, unsigned int dataleft, int channels, int nBlockAlign,
              int wSamplesPerBlock, adpcm_coeff *coeff)
{
  int i, n, nib, max, outp = 0, x;
  unsigned char buf[4096];
  unsigned char outbuf[64 * 1024];
  int AdaptionTable[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
  };
  int predictor[2], delta[2], samp1[2], samp2[2];
  int pred, new, error_delta, i_delta;

/*
 * Playback procedure
 */
#define OUT_SAMPLE(s) \
 do { \
   if (s > 32767) s = 32767; else if (s < -32768) s = -32768; \
   outbuf[outp++] = (unsigned char)(s & 0xff); \
   outbuf[outp++] = (unsigned char)((s >> 8) & 0xff); \
   n += 2; \
   if (outp >= max) \
     { \
       OWRITE (audiofd, outbuf, outp); \
       outp = 0; \
     } \
 } while(0)

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
	  x = 0;

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
    OWRITE (audiofd, outbuf, outp /*(outp+3) & ~3 */ );
  return 0;
}

static int
dump_creative_adpcm (int fd, unsigned int filesize, int format)
{
  const unsigned char T2[4][3] = {
    { 128, 6, 1 },
    { 32,  4, 1 },
    { 8,   2, 1 },
    { 2,   0, 1 }
  };

  const unsigned char T3[3][3] = {
    { 128, 5, 3 },
    { 16,  2, 3 },
    { 2,   0, 1 }
  };

  const unsigned char T4[2][3] = {
    { 128, 4, 7 },
    { 8,   0, 7 }
  };

  const unsigned char * t_row[4] = { T4[0], T4[1] };
  const unsigned char * const * table = t_row;

  int bsize = 1024, l;
  unsigned char buf[1024], obuf[4096];

  signed char limit = 5, sign, shift = 0, ratio = 2;
  int step = 0, pred, i, j, val;

  if (filesize == 0) return 0;

  if (format == AFMT_CR_ADPCM_2)
    {
      limit = 1;
      step = shift = 2;
      ratio = 4;
      for (i=0; i < 4; i++) t_row[i] = T2[i];
    }
  else if (format == AFMT_CR_ADPCM_3)
    {
      limit = 3;
      ratio = 3;
      for (i=0; i < 3; i++) t_row[i] = T3[i];
    }

  OREAD (fd, buf, 1);
  filesize--;
  pred = *buf;
  OWRITE (audiofd, buf, 1);

  while (filesize)
    {
      l = bsize;

      if (l > filesize)
	l = filesize;

      OREAD (fd, buf, l);
      for (i=0; i < l; i++)
        for (j=0; j < ratio; j++)
          {
            sign = (buf[i] & table[j][0])?-1:1;
            val = (buf[i] >> table[j][1]) & table[j][2];
            pred += sign*(val << step);
            if (pred > 255) pred = 255;
            else if (pred < 0) pred = 0;
            obuf[ratio*i+j] = pred;
            if ((val >= limit) && (step < 3+shift)) step++;
            if ((val == 0) && (step > shift)) step--;
          }
      OWRITE (audiofd, obuf, ratio*l);

      filesize -= l;
    }

  return 0;
}
