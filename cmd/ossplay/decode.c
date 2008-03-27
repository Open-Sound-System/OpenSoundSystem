/*
 * Purpose: File format decode routines for ossplay
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include "ossplay.h"
#include "decode.h"

typedef unsigned int (decfunc_t) (unsigned char **, unsigned char *,
                                  unsigned int, void *);

typedef struct fib_values {
  unsigned char pred;
  const char * table;
} fib_values_t;

typedef struct cradpcm_values {
  const unsigned char * const * table;

  signed char limit;
  signed char shift;
  signed char step;
  unsigned char ratio;
  unsigned char pred;
} cradpcm_values_t;

typedef struct decoders_queue {
  struct decoders_queue * next;
  decfunc_t * decoder;
  unsigned char * outbuf;
  char flag;
  void * metadata;
} decoders_queue_t;

enum {
  OBUF,
  META
};

#define FREE_OBUF (1 << OBUF)
#define FREE_META (1 << META)

extern int force_speed, force_bits, force_channels, amplification;
extern int audiofd, quitflag, quiet;
extern char audio_devname[32];

static unsigned int decode_24 (unsigned char **, unsigned char *,
                               unsigned int, void *);
static unsigned int decode_amplify (unsigned char **, unsigned char *,
                                    unsigned int, void *);
static unsigned int decode_cr (unsigned char **, unsigned char *,
                               unsigned int, void *);
static unsigned int decode_fib (unsigned char **, unsigned char *,
                                unsigned int, void *);
static unsigned int decode_msadpcm (unsigned char **, unsigned char *,
                                    unsigned int, void *);
static unsigned int decode_nul (unsigned char **, unsigned char *,
                                unsigned int, void *);
static int decode (int, unsigned int, int, decoders_queue_t *);
static fib_values_t * setup_fib (int, int);
static cradpcm_values_t * setup_cr (int, int);

int
decode_sound (int fd, unsigned int filesize, int format, int channels,
            int speed, void * metadata)
{
  decoders_queue_t * dec, * decoders = NULL;
  int res, bsize;

  if (force_speed != -1) speed = force_speed;
  if (force_channels != -1) channels = force_channels;
  if (force_bits != -1) format = force_bits;

  if (filesize < 2) return 0;
  dec = calloc (1, sizeof(decoders_queue_t));

  switch (format)
    {
      case AFMT_MS_ADPCM:
        dec->metadata = metadata;
        dec->decoder = decode_msadpcm;
        bsize = ((msadpcm_values_t *)dec->metadata)->nBlockAlign;
        dec->outbuf = malloc (bsize * 4 * sizeof (char));
        dec->flag = FREE_OBUF;

        format = AFMT_S16_LE;
        break;
      case AFMT_CR_ADPCM_2:
      case AFMT_CR_ADPCM_3:
      case AFMT_CR_ADPCM_4:
        dec->metadata = (void *)setup_cr (fd, format);;
        dec->flag = 1;
        dec->decoder = decode_cr;
        dec->outbuf = malloc (((cradpcm_values_t *)dec->metadata)->ratio *
                               1024 * sizeof (char));
        dec->flag = FREE_OBUF | FREE_META;

        filesize--;
        format = AFMT_U8;
        bsize = 1024;
        break;
      case AFMT_FIBO_DELTA:
      case AFMT_EXP_DELTA:
        dec->metadata = (void *)setup_fib (fd, format);;
        dec->flag = 1;
        dec->decoder = decode_fib;
        dec->outbuf = malloc (2048 * sizeof (char));
        dec->flag = FREE_OBUF | FREE_META;

        filesize--;
        format = AFMT_U8;
        bsize = 1024;
        break;
      case AFMT_S24_LE:
        dec->metadata = (void *)8;
        dec->decoder = decode_24;
        dec->outbuf = malloc (1024 * sizeof(int));
        dec->flag = FREE_OBUF;

        format = AFMT_S32_NE;
        bsize = 1024 - 1024 % 3;
        break;
      case AFMT_S24_BE:
        dec->metadata = (void *)24;
        dec->decoder = decode_24;
        dec->outbuf = malloc (1024 * sizeof(int));
        dec->flag = FREE_OBUF;

        format = AFMT_S32_NE;
        bsize = 1024 - 1024 % 3;
        break;
      default:
        dec->decoder = decode_nul;
        dec->outbuf = NULL;

        bsize = 1024;
        break;
    }
  if (amplification > 0)
    {
      decoders = malloc (sizeof (decoders_queue_t));
      decoders->metadata = (void *)(long)format;
      decoders->decoder = decode_amplify;
      decoders->next = NULL;
      decoders->outbuf = NULL;
      decoders->flag = 0;
    }
  dec->next = decoders;

  if (!setup_device (fd, format, channels, speed)) return -2;
  res = decode (fd, filesize, bsize, dec);

  decoders = dec;
  while (decoders != NULL)
    {
      if (decoders->flag & FREE_META) free (decoders->metadata);
      if (decoders->flag & FREE_OBUF) free (decoders->outbuf);
      decoders = decoders->next;
      free (dec);
      dec = decoders;
    }

  return res;
}

static int
decode (int fd, unsigned int filesize, int bsize, decoders_queue_t * dec)
{
  unsigned int dataleft = filesize, outl;
  unsigned char *buf, *obuf;
  decoders_queue_t * d;

  buf = malloc (bsize * sizeof(char));

  while (dataleft)
    {
      d = dec;

      if (bsize > dataleft) bsize = dataleft;

      if (quitflag == 1)
        {
          quitflag = 0;
          ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL);
          return -1;
        }
      if ((outl = read (fd, buf, bsize)) <= 0)
        {
          if (quitflag == 1)
            {
              quitflag = 0;
              ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL);
              return -1;
            }
          if ((dataleft) && (filesize != UINT_MAX) && (!quiet))
            fprintf (stderr, "Sound data ended prematurily!\n");
          return 0;
        }

      obuf = buf;
      do
        {
          outl = d->decoder (&(d->outbuf), obuf, outl, d->metadata);
          obuf = d->outbuf;
          d = d->next;
        }
      while (d != NULL);

     if (write (audiofd, obuf, outl) == -1)
       {
         if ((errno == EINTR) && (quitflag == 1))
           {
             quitflag = 0;
             ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL);
             return -1;
           }
          perror (audio_devname);
          exit (-1);
       }

      dataleft -= bsize;
    }

  return 0;
}

static unsigned int
decode_24 (unsigned char ** obuf, unsigned char * buf, unsigned int l,
           void * metadata)
{
  unsigned int outlen = 0, i, * u32;
  int sample_s32, v1 = (int)(long)metadata, * outbuf = (int *) * obuf;

  for (i = 0; i < l; i += 3)
    {
      u32 = (unsigned int *) &sample_s32;	/* Alias */

      *u32 = (buf[i] << v1) | (buf[i + 1] << 16) | (buf[i + 2] << (32-v1));
      outbuf[outlen++] = sample_s32;
    }

  return outlen * sizeof(int);
}

static fib_values_t *
setup_fib (int fd, int format)
{
  static const char CodeToDelta[16] = {
    -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21
  };
  static const char CodeToExpDelta[16] = {
    -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64
  };
  unsigned char buf;
  fib_values_t * val;

  val = malloc (sizeof (fib_values_t));
  if (format == AFMT_EXP_DELTA) val->table = CodeToExpDelta;
  else val->table = CodeToDelta;

  if (read (fd, &buf, 1) <= 0) return NULL;
  val->pred = buf;

  return val;
}

static cradpcm_values_t *
setup_cr (int fd, int format)
{
  static const unsigned char T2[4][3] = {
    { 128, 6, 1 },
    { 32,  4, 1 },
    { 8,   2, 1 },
    { 2,   0, 1 }
  };

  static const unsigned char T3[3][3] = {
    { 128, 5, 3 },
    { 16,  2, 3 },
    { 2,   0, 1 }
  };

  static const unsigned char T4[2][3] = {
    { 128, 4, 7 },
    { 8,   0, 7 }
  };

  static const unsigned char * t_row[4];

  unsigned char buf;
  cradpcm_values_t * val;
  int i;

  val = malloc (sizeof (cradpcm_values_t));
  val->table = t_row;

  if (format == AFMT_CR_ADPCM_2)
    {
      val->limit = 1;
      val->step = val->shift = 2;
      val->ratio = 4;
      for (i=0; i < 4; i++) t_row[i] = T2[i];
    }
  else if (format == AFMT_CR_ADPCM_3)
    {
      val->limit = 3;
      val->ratio = 3;
      val->step = val->shift = 0;
      for (i=0; i < 3; i++) t_row[i] = T3[i];
    }
  else /* if (format == AFMT_CR_ADPCM_4) */
    {
      val->limit = 5;
      val->ratio = 2;
      val->step = val->shift = 0;
      for (i=0; i < 2; i++) t_row[i] = T4[i];
    }

  if (read (fd, &buf, 1) <= 0) return NULL;
  val->pred = buf;

  return val;
}

static unsigned int
decode_cr (unsigned char ** obuf, unsigned char * buf, unsigned int l,
           void * metadata)
{
  cradpcm_values_t * val = (cradpcm_values_t *) metadata;
  int i, j, value, pred = val->pred, step = val->step;
  signed char sign;

  for (i=0; i < l; i++)
    for (j=0; j < val->ratio; j++)
      {
        sign = (buf[i] & val->table[j][0])?-1:1;
        value = (buf[i] >> val->table[j][1]) & val->table[j][2];
        pred += sign*(value << step);
        if (pred > 255) pred = 255;
        else if (pred < 0) pred = 0;
        (*obuf)[val->ratio*i+j] = pred;
        if ((value >= val->limit) && (step < 3+val->shift)) step++;
        if ((value == 0) && (step > val->shift)) step--;
      }

  val->pred = pred;
  val->step = step;
  return val->ratio*l;
}

static unsigned int
decode_fib (unsigned char ** obuf, unsigned char * buf, unsigned int l,
            void * metadata)
{
  fib_values_t * val = (fib_values_t *)metadata;
  int i, x = val->pred;
  unsigned char d;

  for (i = 0; i < 2*l; i++)
    {
      d = buf[i/2];
      if (i & 1) d &= 0xF;
      else d >>= 4;
      x += val->table[d];
      if (x > 255) x = 255;
      if (x < 0) x = 0;
      (*obuf)[i] = x;
    }

  val->pred = x;
  return 2*l;
}

static unsigned int
decode_msadpcm (unsigned char ** obuf, unsigned char * buf, unsigned int l,
                void * metadata)
{
  msadpcm_values_t * val = (msadpcm_values_t *)metadata;

  int i, n = 0, nib = 0, outp = 0, x = 0, channels = val->channels;
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
   (*obuf)[outp++] = (unsigned char)(s & 0xff); \
   (*obuf)[outp++] = (unsigned char)((s >> 8) & 0xff); \
   n += 2; \
 } while(0)

#define GETNIBBLE \
        ((nib == 0) ? \
                (buf[x + nib++] >> 4) & 0x0f : \
                buf[x++ + --nib] & 0x0f \
	)

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
      OUT_SAMPLE (samp2[i]);
    }

  for (i = 0; i < channels; i++)
    {
      samp2[i] = (short) le_int (&buf[x], 2);
      x += 2;
      OUT_SAMPLE (samp2[i]);
    }

  while (n < (val->wSamplesPerBlock * 2 * channels))
    for (i = 0; i < channels; i++)
      {
        pred = ((samp1[i] * val->coeff[predictor[i]].coeff1)
                + (samp2[i] * val->coeff[predictor[i]].coeff2)) / 256;
        i_delta = error_delta = GETNIBBLE;

        if (i_delta & 0x08)
        i_delta -= 0x10;	/* Convert to signed */

        new = pred + (delta[i] * i_delta);
        OUT_SAMPLE (new);

        delta[i] = delta[i] * AdaptionTable[error_delta] / 256;
        if (delta[i] < 16) delta[i] = 16;

        samp2[i] = samp1[i];
        samp1[i] = new;
      }

  return outp;
}

static unsigned int
decode_nul (unsigned char ** obuf, unsigned char * buf, unsigned int l,
            void * metadata)
{
  *obuf = buf;
  return l;
}

static unsigned int
decode_amplify (unsigned char ** obuf, unsigned char * buf, unsigned int l,
                void * metadata)
{
  int format = (int)(long)metadata, i, len;

  switch (format)
    {
      case AFMT_S16_NE:
        {
          short *s = (short *)buf;
          len = l / 2;

          for (i = 0; i < len ; i++) s[i] = s[i] * amplification / 100;
        }
        break;
      case AFMT_S32_NE:
        {
          int *s = (int *)buf;
          len = l / 4;

          for (i = 0; i < len; i++) s[i] = s[i] * amplification / 100;
        }
       break;
   }

  *obuf = buf;
  return l;
}
