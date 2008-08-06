/*
 * Purpose: File format decode routines for ossplay
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include "ossplay.h"
#include "ossplay_decode.h"

typedef ssize_t (decfunc_t) (unsigned char **, unsigned char *,
                             ssize_t, void *);
typedef int (seekfunc_t) (int, unsigned int *, int, double, int, int); 

typedef struct fib_values {
  unsigned char pred;
  const signed char * table;
} fib_values_t;

typedef struct cradpcm_values {
  const unsigned char * const * table;

  signed char limit;
  signed char shift;
  signed char step;
  unsigned char ratio;
  unsigned char pred;
} cradpcm_values_t;

typedef struct verbose_values {
  char tstring[20];
  double next_sec;
  double tsecs;
  double constant;
  unsigned int * datamark;
  int format;
} verbose_values_t;

typedef struct decoders_queue {
  struct decoders_queue * next;
  decfunc_t * decoder;
  unsigned char * outbuf;
  void * metadata;
  unsigned int flag;
} decoders_queue_t;

enum {
  OBUF,
  META
};

#define FREE_OBUF (1 << OBUF)
#define FREE_META (1 << META)

extern int force_speed, force_fmt, force_channels, amplification;
extern int audiofd, quitflag, verbose, int_conv;
extern char audio_devname[32];
extern off_t (*ossplay_lseek) (int, off_t, int);
extern double seek_time;

static decfunc_t decode_24;
static decfunc_t decode_8_to_s16;
static decfunc_t decode_amplify;
static decfunc_t decode_cr;
static decfunc_t decode_endian;
static decfunc_t decode_fib;
static decfunc_t decode_msadpcm; 
static decfunc_t decode_nul;

static int decode (int, int, int, int, unsigned int *, int, double,
                   decoders_queue_t *, seekfunc_t *);
static float format2ibits (int);
static int format2obits (int);
static cradpcm_values_t * setup_cr (int, int);
static fib_values_t * setup_fib (int, int);
static verbose_values_t * setup_verbose (int, double, unsigned int *);
static decoders_queue_t * setup_normalize (int *, int *, decoders_queue_t *);
static char * totime (double);
static void print_verbose_info (const unsigned char *, ssize_t, void *);

static seekfunc_t seek_normal;
static seekfunc_t seek_compressed;

int
decode_sound (int fd, unsigned int filesize, int format, int channels,
              int speed, void * metadata)
{
  decoders_queue_t * dec, * decoders;
  seekfunc_t * seekf = NULL;
  int bsize, obsize, res = -2;
  double constant;

  if (force_speed != -1) speed = force_speed;
  if (force_channels != -1) channels = force_channels;
  if (force_fmt != 0) format = force_fmt;
  if (channels > MAX_CHANNELS) channels = MAX_CHANNELS;

  constant = format2ibits (format) * speed * channels / 8;
#if 0
  /*
   * There is no reason to use SNDCTL_DSP_GETBLKSIZE in applications like this.
   * Using some fixed local buffer size will work equally well.
   */
  ioctl (audiofd, SNDCTL_DSP_GETBLKSIZE, &bsize);
#else
  bsize = PLAYBUF_SIZE;
#endif

  if (filesize < 2) return 0;
  decoders = dec = ossplay_malloc (sizeof (decoders_queue_t));
  dec->next = NULL;
  dec->flag = 0;
  seekf = seek_normal;

  switch (format)
    {
      case AFMT_MS_ADPCM:
        if (metadata == NULL)
          {
            msadpcm_values_t * val = ossplay_malloc (sizeof (msadpcm_values_t));

            val->nBlockAlign = 256; val->wSamplesPerBlock = 496;
            val->wNumCoeff = 7; val->channels = DEFAULT_CHANNELS;
            val->coeff[0].coeff1 = 256; val->coeff[0].coeff2 = 0;
            val->coeff[1].coeff1 = 512; val->coeff[1].coeff2 = -256;
            val->coeff[2].coeff1 = 0; val->coeff[2].coeff2 = 0;
            val->coeff[3].coeff1 = 192; val->coeff[3].coeff2 = 64;
            val->coeff[4].coeff1 = 240; val->coeff[4].coeff2 = 0;
            val->coeff[5].coeff1 = 460; val->coeff[5].coeff2 = -208;
            val->coeff[6].coeff1 = 392; val->coeff[6].coeff2 = -232;

            dec->metadata = (void *)val;
            dec->flag |= FREE_META;
          }
        else dec->metadata = metadata;
        dec->decoder = decode_msadpcm;
        bsize = ((msadpcm_values_t *)dec->metadata)->nBlockAlign;
        obsize = 4 * bsize * sizeof (char);
        dec->outbuf = ossplay_malloc (obsize);
        dec->flag |= FREE_OBUF;
        seekf = seek_compressed;

        format = AFMT_S16_LE;
        break;
      case AFMT_CR_ADPCM_2:
      case AFMT_CR_ADPCM_3:
      case AFMT_CR_ADPCM_4:
        dec->metadata = (void *)setup_cr (fd, format);;
        if (dec->metadata == NULL) goto exit;
        dec->decoder = decode_cr;
        obsize = ((cradpcm_values_t *)dec->metadata)->ratio * bsize;
        dec->outbuf = ossplay_malloc (obsize);
        dec->flag = FREE_OBUF | FREE_META;
        seekf = seek_compressed;

        if (filesize != UINT_MAX) filesize--;
        format = AFMT_U8;
        break;
      case AFMT_FIBO_DELTA:
      case AFMT_EXP_DELTA:
        dec->metadata = (void *)setup_fib (fd, format);;
        if (dec->metadata == NULL) goto exit;
        dec->decoder = decode_fib;
        obsize = 2 * bsize;
        dec->outbuf = ossplay_malloc (obsize);
        dec->flag = FREE_OBUF | FREE_META;
        seekf = seek_compressed;

        if (filesize != UINT_MAX) filesize--;
        format = AFMT_U8;
        break;
      case AFMT_S24_LE:
      case AFMT_S24_BE:
        dec->metadata = (void *)(long)format;
        dec->decoder = decode_24;
        bsize = PLAYBUF_SIZE - PLAYBUF_SIZE % 3;
        obsize = bsize * sizeof(int);
        dec->outbuf = ossplay_malloc (obsize);
        dec->flag = FREE_OBUF;

        format = AFMT_S32_NE;
        break;
      default:
        dec->decoder = decode_nul;
        dec->flag = 0;

        obsize = bsize;
        break;
    }

  if (int_conv)
    decoders = setup_normalize (&format, &obsize, decoders);

  if ((amplification > 0) && (amplification != 100))
    {
      decoders->next = ossplay_malloc (sizeof (decoders_queue_t));
      decoders = decoders->next;
      decoders->metadata = (void *)(long)format;
      decoders->decoder = decode_amplify;
      decoders->next = NULL;
      decoders->outbuf = NULL;
      decoders->flag = 0;
    }

  if (!(res = setup_device (format, channels, speed)))
    {
      res = -2;
      goto exit;
    }
  if (res != format)
    decoders = setup_normalize (&format, &obsize, decoders);

  res = decode (fd, format, channels, speed, &filesize, bsize,
                constant, dec, seekf);

exit:
  decoders = dec;
  while (decoders != NULL)
    {
      if (decoders->flag & FREE_META) ossplay_free (decoders->metadata);
      if (decoders->flag & FREE_OBUF) ossplay_free (decoders->outbuf);
      decoders = decoders->next;
      ossplay_free (dec);
      dec = decoders;
    }

  return res;
}

static int
decode (int fd, int format, int channels, int speed, unsigned int * datamark,
        int bsize, double constant, decoders_queue_t * dec, seekfunc_t * seekf)
{
#define EXITCODE(code) \
  do { \
    ossplay_free (verbose_meta); \
    ossplay_free (buf); \
    clear_update (); \
    ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL); \
    return code; \
  } while (0)

  int rsize = bsize;
  unsigned int filesize = *datamark;
  ssize_t outl;
  unsigned char * buf, * obuf, contflag = 0;
  decoders_queue_t * d;
  verbose_values_t * verbose_meta = NULL;

  buf = ossplay_malloc (bsize * sizeof(char));
  if (verbose)
    verbose_meta = (void *)setup_verbose (format, constant, datamark);

  *datamark = 0;

  while (*datamark < filesize)
    {
      if (quitflag == 1) EXITCODE (-1);

      rsize = bsize;
      if (rsize > filesize - *datamark) rsize = filesize - *datamark;

      if ((seek_time != 0) && (seekf != NULL))
        {
          int ret;

          ret = seekf (fd, datamark, filesize, constant, rsize, channels);
          if (ret < 0) EXITCODE (ret);
          else if (ret == 0) continue;
          else contflag = 1;
        }

      if ((outl = read (fd, buf, rsize)) <= 0)
        {
          ossplay_free (buf);
          clear_update ();
          if (quitflag == 1)
            {
              ioctl (audiofd, SNDCTL_DSP_HALT_OUTPUT, NULL);
              return -1;
            }
          if ((outl == 0) & (filesize != UINT_MAX))
            print_msg (WARNM, "Sound data ended prematurily!\n");
          return 0;
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

      if (verbose) print_verbose_info (obuf, outl, verbose_meta);
      if (write (audiofd, obuf, outl) == -1)
        {
          if ((errno == EINTR) && (quitflag == 1)) EXITCODE (-1);
          ossplay_free (buf);
          perror_msg (audio_devname);
          exit (-1);
        }
    }

  ossplay_free (buf);
  ossplay_free (verbose_meta);
  clear_update ();
  return 0;
}

int silence (unsigned int len, int speed)
{
  int i;
  unsigned char empty[1024];

  if (!(i = setup_device (AFMT_U8, 1, speed))) return -1;

  if (i == AFMT_S16_NE) len /= 2;

  memset (empty, 0, 1024 * sizeof (unsigned char));

  while (len > 0)
    {
      i = 1024;
      if (i > len) i = len;
      if ((write (audiofd, empty, i) < 0) &&
          (errno == EINTR) && (quitflag == 1)) return -1;

      len -= i;
    }

  return 0;
}

static ssize_t
decode_24 (unsigned char ** obuf, unsigned char * buf,
           ssize_t l, void * metadata)
{
  unsigned int outlen = 0, i, * u32, v1;
  int sample_s32, format = (int)(long)metadata, * outbuf = (int *) * obuf;

  if (format == AFMT_S24_LE) v1 = 8;
  else v1 = 24;

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
  static const signed char CodeToDelta[16] = {
    -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21
  };
  static const signed char CodeToExpDelta[16] = {
    -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64
  };
  unsigned char buf;
  fib_values_t * val;

  val = ossplay_malloc (sizeof (fib_values_t));
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

  val = ossplay_malloc (sizeof (cradpcm_values_t));
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

static ssize_t
decode_8_to_s16 (unsigned char ** obuf, unsigned char * buf,
                 ssize_t l, void * metadata)
{
  int format = (int)(long)metadata;
  unsigned int i;
  short * outbuf = (short *) * obuf;
  static const short mu_law_table[256] = { 
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956, 
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764, 
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412, 
    -11900,-11388,-10876,-10364,-9852, -9340, -8828, -8316, 
    -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140, 
    -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092, 
    -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004, 
    -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980, 
    -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436, 
    -1372, -1308, -1244, -1180, -1116, -1052, -988,  -924, 
    -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652, 
    -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396, 
    -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260, 
    -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132, 
    -120,  -112,  -104,  -96,   -88,   -80,   -72,   -64, 
    -56,   -48,   -40,   -32,   -24,   -16,   -8,     0, 
    32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956, 
    23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764, 
    15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412, 
    11900, 11388, 10876, 10364, 9852,  9340,  8828,  8316, 
    7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140, 
    5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092, 
    3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004, 
    2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980, 
    1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436, 
    1372,  1308,  1244,  1180,  1116,  1052,  988,   924, 
    876,   844,   812,   780,   748,   716,   684,   652, 
    620,   588,   556,   524,   492,   460,   428,   396, 
    372,   356,   340,   324,   308,   292,   276,   260, 
    244,   228,   212,   196,   180,   164,   148,   132, 
    120,   112,   104,   96,    88,    80,    72,    64, 
    56,    48,    40,    32,    24,    16,    8,     0 
  };

  static const short a_law_table[256] = { 
    -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736, 
    -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784, 
    -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368, 
    -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392, 
    -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944, 
    -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136, 
    -11008,-10496,-12032,-11520,-8960, -8448, -9984, -9472, 
    -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568, 
    -344,  -328,  -376,  -360,  -280,  -264,  -312,  -296, 
    -472,  -456,  -504,  -488,  -408,  -392,  -440,  -424, 
    -88,   -72,   -120,  -104,  -24,   -8,    -56,   -40, 
    -216,  -200,  -248,  -232,  -152,  -136,  -184,  -168, 
    -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184, 
    -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696, 
    -688,  -656,  -752,  -720,  -560,  -528,  -624,  -592, 
    -944,  -912,  -1008, -976,  -816,  -784,  -880,  -848, 
    5504,  5248,  6016,  5760,  4480,  4224,  4992,  4736, 
    7552,  7296,  8064,  7808,  6528,  6272,  7040,  6784, 
    2752,  2624,  3008,  2880,  2240,  2112,  2496,  2368, 
    3776,  3648,  4032,  3904,  3264,  3136,  3520,  3392, 
    22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944, 
    30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136, 
    11008, 10496, 12032, 11520, 8960,  8448,  9984,  9472, 
    15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568, 
    344,   328,   376,   360,   280,   264,   312,   296, 
    472,   456,   504,   488,   408,   392,   440,   424, 
    88,    72,    120,   104,   24,    8,     56,    40, 
    216,   200,   248,   232,   152,   136,   184,   168, 
    1376,  1312,  1504,  1440,  1120,  1056,  1248,  1184, 
    1888,  1824,  2016,  1952,  1632,  1568,  1760,  1696, 
    688,   656,   752,   720,   560,   528,   624,   592, 
    944,   912,   1008,  976,   816,   784,   880,   848 
  };

  switch (format)
    {
      case AFMT_U8:
        for (i = 0; i < l; i++) outbuf[i] = (buf[i] - 128) << 8;
        break;
      case AFMT_S8:
        for (i = 0; i < l; i++) outbuf[i] = buf[i] << 8;
        break;
      case AFMT_MU_LAW:
        for (i = 0; i < l; i++) outbuf[i] = mu_law_table[buf[i]];
        break;
      case AFMT_A_LAW:
        for (i = 0; i < l; i++) outbuf[i] = a_law_table[buf[i]];
        break;
    }

  return 2*l;
}

static ssize_t
decode_cr (unsigned char ** obuf, unsigned char * buf,
           ssize_t l, void * metadata)
{
  cradpcm_values_t * val = (cradpcm_values_t *) metadata;
  int i, j, pred = val->pred, step = val->step;
  unsigned char value;
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

static ssize_t
decode_fib (unsigned char ** obuf, unsigned char * buf,
            ssize_t l, void * metadata)
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

static ssize_t
decode_msadpcm (unsigned char ** obuf, unsigned char * buf,
                ssize_t l, void * metadata)
{
  msadpcm_values_t * val = (msadpcm_values_t *)metadata;

  int i, n = 0, nib = 0, outp = 0, x = 0, channels = val->channels;
  int AdaptionTable[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
  };
  int predictor[MAX_CHANNELS], delta[MAX_CHANNELS], samp1[MAX_CHANNELS],
      samp2[MAX_CHANNELS];
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

        if (x > l) return outp;
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

static ssize_t
decode_nul (unsigned char ** obuf, unsigned char * buf,
            ssize_t l, void * metadata)
{
  *obuf = buf;
  return l;
}

static ssize_t
decode_endian (unsigned char ** obuf, unsigned char * buf,
               ssize_t l, void * metadata)
{
  int format = (int)(long)metadata, i, len;

  switch (format)
    {
      case AFMT_S16_OE:
        {
          short * s = (short *)buf;

          len = l/2;
          for (i = 0; i < len; i++)
            s[i] = ((s[i] >> 8) & 0x00FF) |
                   ((s[i] << 8) & 0xFF00);
        }
        break;
      /* case AFMT_S24_OE: unneeded because of decode_24 */
      case AFMT_S32_OE:
        {
          int * s = (int *)buf;

          len = l/4;
          for (i = 0; i < len; i++)
            s[i] = ((s[i] >> 24) & 0x000000FF) |
                   ((s[i] << 8) & 0x00FF0000) | ((s[i] >> 8) & 0x0000FF00) |
                   ((s[i] << 24) & 0xFF000000);
        }
        break;
#if AFMT_S16_LE == AFMT_S16_NE
      case AFMT_U16_BE: /* U16_BE -> S16_LE */
#else
      case AFMT_U16_LE: /* U16_LE -> S16_BE */
#endif
        {
          short * s = (short *)buf;

          len = l/2;
          for (i = 0; i < len ; i++)
            s[i] = (((s[i] >> 8) & 0x00FF) | ((s[i] << 8) & 0xFF00)) -
                   USHRT_MAX/2;
        }
      break;
 /* Not an endian conversion, but included for completeness sake */
#if AFMT_S16_LE == AFMT_S16_NE
      case AFMT_U16_LE:
#else
      case AFMT_U16_BE:
#endif
       {
          short * s = (short *)buf;

          len = l/2;
          for (i = 0; i < len; i++)
             s[i] -= USHRT_MAX/2;
       }
      break;
    }
  *obuf = buf;
  return l;
}

static ssize_t
decode_amplify (unsigned char ** obuf, unsigned char * buf,
                ssize_t l, void * metadata)
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

          for (i = 0; i < len; i++) s[i] = s[i] * (long)amplification / 100;
        }
       break;
   }

  *obuf = buf;
  return l;
}

static verbose_values_t *
setup_verbose (int format, double constant, unsigned int * filesize)
{
  verbose_values_t * val;

  val = ossplay_malloc (sizeof (verbose_values_t));

  if (*filesize == UINT_MAX)
    {
      val->tsecs = UINT_MAX;
      strcpy (val->tstring, "unknown");
    }
  else
    {
      char * p;

      val->tsecs = *filesize / constant;
      p = totime (val->tsecs);
      val->tsecs -= 0.01;
      strcpy (val->tstring, p);
      ossplay_free (p);
    }

  val->next_sec = 0;
  val->format = format;
  val->constant = constant;
  val->datamark = filesize;

  return val;
}

static char *
totime (double secs)
{
  char time[20];
  unsigned long min = secs / 60;

  snprintf (time, 20, "%.2lu:%05.2f", min, secs - min * 60);

  return ossplay_strdup (time);
}

static void
print_verbose_info (const unsigned char * buf, ssize_t l, void * metadata)
{
/*
 * Display a rough recording level meter, and the elapsed time.
 */
  static const unsigned char db_table[256] = {
  /* Lookup table for log10(ix)*2, ix=0..255 */
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

  verbose_values_t * val = (verbose_values_t *)metadata;
  int i, lim, v = 0, level;
  char * rtime;
  double secs;

  secs = *val->datamark / val->constant;
  if (secs < val->next_sec) return;
  val->next_sec = secs + MIN_UPDATE_INTERVAL/1000;
  if (val->next_sec > val->tsecs) val->next_sec = val->tsecs;

  level = 0;

  switch (val->format)
    {
      case AFMT_S16_NE:
        {
          short *p;

          p = (short *)buf;
          lim = l/2;

          for (i = 0; i < lim; i++)
            {
              v = (*p++) << 16;
              if (v < 0) v = -v;
              if (v > level) level = v;
            }
        }
        break;

      case AFMT_S32_NE:
        {
         int *p;

         p = (int *)buf;
         lim = l / 4;

         for (i = 0; i < lim; i++)
           {
             v = *p++;
             if (v < 0) v = -v;
             if (v > level) level = v;
           }
        }
        break;
    }

  level >>= 24;

  if (level > 255) level = 255;
  v = db_table[level];

  rtime = totime (secs);
  print_update (v, rtime, val->tstring);
  ossplay_free (rtime);
}

static float
format2ibits (int format)
{
  switch (format)
    {
      case AFMT_CR_ADPCM_2: return 2;
      case AFMT_CR_ADPCM_3: return 2.66;
      case AFMT_CR_ADPCM_4:
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
      case AFMT_S24_LE:
      case AFMT_S24_BE:
      case AFMT_S24_PACKED: return 24;
      case AFMT_SPDIF_RAW:
      case AFMT_S32_LE:
      case AFMT_S32_BE: return 32;
      case AFMT_FLOAT: return sizeof (float);
      case AFMT_QUERY:
      default: return 0;
   }
}

static int
format2obits (int format)
{
  switch (format)
    {
      case AFMT_FIBO_DELTA:
      case AFMT_EXP_DELTA:
      case AFMT_CR_ADPCM_2:
      case AFMT_CR_ADPCM_3:
      case AFMT_CR_ADPCM_4:
      case AFMT_MU_LAW:
      case AFMT_A_LAW:
      case AFMT_U8:
      case AFMT_S8: return 8;
      case AFMT_VORBIS:
      case AFMT_MPEG:
      case AFMT_IMA_ADPCM:
      case AFMT_MS_ADPCM:
      case AFMT_S16_LE:
      case AFMT_S16_BE:
      case AFMT_U16_LE:
      case AFMT_U16_BE: return 16;
      case AFMT_S24_PACKED: return 24;
      case AFMT_SPDIF_RAW:
      case AFMT_S24_LE:
      case AFMT_S24_BE:
      case AFMT_S32_LE:
      case AFMT_S32_BE: return 32;
      case AFMT_FLOAT: return sizeof (float);
      case AFMT_QUERY:
      default: return 0;
    }
}

static decoders_queue_t *
setup_normalize (int * format, int * obsize, decoders_queue_t * decoders)
{
  if ((*format == AFMT_S16_OE) || (*format == AFMT_S32_OE) ||
      (*format == AFMT_U16_LE) || (*format == AFMT_U16_BE))
    {
      decoders->next = ossplay_malloc (sizeof (decoders_queue_t));
      decoders = decoders->next;
      decoders->decoder = decode_endian;
      decoders->metadata = (void *)(long)*format;
      *format = (*format == AFMT_S32_OE)?AFMT_S32_NE:AFMT_S16_NE;
      decoders->next = NULL;
      decoders->outbuf = NULL;
      decoders->flag = 0;
    }
  else if (format2obits (*format) == 8)
    {
      decoders->next = ossplay_malloc (sizeof (decoders_queue_t));
      decoders = decoders->next;
      decoders->decoder = decode_8_to_s16;
      decoders->metadata = (void *)(long)*format;
      decoders->next = NULL;
      *obsize *= 2;
      decoders->outbuf = ossplay_malloc (*obsize);
      decoders->flag = FREE_OBUF;
      *format = AFMT_S16_NE;
    }
  return decoders;
}

static int
seek_normal (int fd, unsigned int * datamark, int filesize,
             double constant, int channels, int rsize) 
{
  off_t pos = seek_time * constant;
  int ret;

  seek_time = 0;
  if ((pos > filesize) || (pos < *datamark)) return -1;
  pos -= pos % channels;

  ret = ossplay_lseek (fd, pos - *datamark, SEEK_CUR);
  if (ret == -1) return -1;
  *datamark = ret;

  return 0;
}

static int
seek_compressed (int fd, unsigned int * datamark, int filesize,
                 double constant, int rsize, int channels)
/*
 * We have to use this method because some compressed formats depend on
 * the previous state of the decoder.
 */
{
  unsigned int pos = seek_time * constant;

  if (pos > filesize)
    {
      seek_time = 0;
      return -1;
    }

  if (*datamark + rsize < pos)
     {
       return 1;
     }
  else
    {
      seek_time = 0;
      return 0;
    }
}
