#ifndef OSSPLAY_H
#define OSSPLAY_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <soundcard.h>

#undef  MPEG_SUPPORT
#define PLAYBUF_SIZE		1024
#define RECBUF_SIZE		512
#define DEFAULT_CHANNELS	1
#define DEFAULT_FORMAT		AFMT_U8
#define DEFAULT_SPEED		11025
#define MAX_CHANNELS		128
/*
 * Every update of output in verbose mode while playing is separated by at
 * least PLAY_UPDATE_INTERVAL milliseconds.
 */
#define PLAY_UPDATE_INTERVAL		200.0
/* As above, but for recording */
#define REC_UPDATE_INTERVAL		1000.0
/* As above, but used for level meters while recording */
#define LMETER_UPDATE_INTERVAL		20.0
/* Should be smaller than the others. Used to ensure an update at end of file */
#define UPDATE_EPSILON			1.0

/*
 * We overload the format definitions to include some "fake" formats.
 * Therefor, the values should be negative to avoid collusions.
 */
enum {
 AFMT_MS_ADPCM = -256,
 AFMT_MS_IMA_ADPCM,
 AFMT_MS_IMA_ADPCM_3BITS,
 AFMT_MAC_IMA_ADPCM,
 AFMT_S24_PACKED_BE,
 AFMT_CR_ADPCM_2,
 AFMT_CR_ADPCM_3,
 AFMT_CR_ADPCM_4,
 AFMT_FIBO_DELTA,
 AFMT_EXP_DELTA	
};
#define AFMT_S24_PACKED_LE	AFMT_S24_PACKED

typedef struct {
  int fd;
  int format;
  int channels;
  int speed;
  int flags;
  int reclevel;
#ifndef OSS_DEVNODE_SIZE
#define OSS_DEVNODE_SIZE	32
#endif
  char dname[OSS_DEVNODE_SIZE];
#ifndef OSS_LONGNAME_SIZE
#define OSS_LONGNAME_SIZE	64
#endif
  char current_songname[OSS_LONGNAME_SIZE];
  char * recsrc;
  char * playtgt;
}
dspdev_t;

typedef enum {
  WAVE_FILE,
  AU_FILE,
  RAW_FILE,
  AIFF_FILE,
  AIFC_FILE,
  WAVE_FILE_BE,
  _8SVX_FILE,
  _16SV_FILE,
  MAUD_FILE
}
fctypes_t;

typedef enum {
  CP = 0x1,
  CR = 0x2,
  CRP = 0x3
}
direction_t;

typedef struct fmt_struct {
  const char * name;
  const int fmt;
  const direction_t dir;
  const int may_conv;
}
format_t;

typedef struct cnt_struct {
  const char * name;
  const fctypes_t type;
  const int dformat;
  const int dchannels;
  const int dspeed;
}
container_t;

typedef struct {
  int coeff1, coeff2;
}
adpcm_coeff;

typedef struct msadpcm_values {
  int nBlockAlign;
  int wSamplesPerBlock;
  int wNumCoeff;
  int bits;
  adpcm_coeff coeff[32];
  int channels;
}
msadpcm_values_t;

typedef ssize_t (decfunc_t) (unsigned char **, unsigned char *,
                             ssize_t, void *);
typedef ssize_t (seekfunc_t) (int, unsigned long long *, unsigned long long,
                              double, unsigned long long, int);

typedef enum {
  FREE_OBUF = 1,
  FREE_META = 2
}
decoder_flag_t;

typedef struct decoders_queue {
  struct decoders_queue * next;
  decfunc_t * decoder;
  unsigned char * outbuf;
  void * metadata;
  decoder_flag_t flag;
}
decoders_queue_t;

typedef enum {
  E_OK,
  E_SETUP_ERROR,
  E_FORMAT_UNSUPPORTED,
  E_CHANNELS_UNSUPPORTED,
  E_DECODE,
  E_ENCODE,
  E_USAGE,
  /*
   * Not an error, but since seek function can also return an error this needs
   * to be different from the others
   */
  SEEK_CONT_AFTER_DECODE
}
errors_t;

static const format_t format_a[] = {
  {"S8",		AFMT_S8,		CRP,		AFMT_S16_NE},
  {"U8",		AFMT_U8,		CRP,		AFMT_S16_NE},
  {"S16_LE",		AFMT_S16_LE,		CRP,		AFMT_S16_NE},
  {"S16_BE",		AFMT_S16_BE,		CRP,		AFMT_S16_NE},
  {"U16_LE",		AFMT_U16_LE,		CRP,		AFMT_S16_NE},
  {"U16_BE",		AFMT_U16_BE,		CRP,		AFMT_S16_NE},
  {"S24_LE",		AFMT_S24_LE,		CRP,		0},
  {"S24_BE",		AFMT_S24_BE,		CRP,		0},
  {"S32_LE",		AFMT_S32_LE,		CRP,		AFMT_S32_NE},
  {"S32_BE",		AFMT_S32_BE,		CRP,		AFMT_S32_NE},
  {"A_LAW",		AFMT_A_LAW,		CRP,		AFMT_S16_NE},
  {"MU_LAW",		AFMT_MU_LAW,		CRP,		AFMT_S16_NE},
  {"IMA_ADPCM",		AFMT_IMA_ADPCM,		CP,		0},
  {"MS_IMA_ADPCM",	AFMT_MS_IMA_ADPCM,	CP,		0},
  {"MS_IMA_ADPCM_3BITS",AFMT_MS_IMA_ADPCM_3BITS,CP,		0},
  {"MAC_IMA_ADPCM",	AFMT_MAC_IMA_ADPCM,	CP,		0},
  {"MS_ADPCM",		AFMT_MS_ADPCM,		CP,		0},
  {"CR_ADPCM_2",	AFMT_CR_ADPCM_2,	CP,		0},
  {"CR_ADPCM_3",	AFMT_CR_ADPCM_3,	CP,		0},
  {"CR_ADPCM_4",	AFMT_CR_ADPCM_4,	CP,		0},
  {"FLOAT",		AFMT_FLOAT,		CRP,		0},
  {"S24_PACKED",	AFMT_S24_PACKED,	CRP,		0},
  {"S24_PACKED_BE",	AFMT_S24_PACKED_BE,	CP,		0},
  {"SPDIF_RAW",		AFMT_SPDIF_RAW,		CR,		0},
  {"FIBO_DELTA",	AFMT_FIBO_DELTA,	CP,		0},
  {"EXP_DELTA",		AFMT_EXP_DELTA,		CP,		0},
  {NULL,		0,			0,		0}
};

static const container_t container_a[] = {
  {"WAV",		WAVE_FILE,	AFMT_S16_LE,	2,	48000},
  {"AU",		AU_FILE,	AFMT_MU_LAW,	1,	8000},
  {"RAW",		RAW_FILE,	AFMT_S16_LE,	2,	44100},
  {"AIFF",		AIFF_FILE,	AFMT_S16_BE,	2,	48000},
  {NULL,		0,		0,		0,	0}
}; /* Order should match fctypes_t enum so that container_a[type] works */

int be_int (const unsigned char *, int);
const char * filepart (const char *);
int le_int (const unsigned char *, int);
off_t ossplay_lseek_stdin (int, off_t, int);
int ossplay_parse_opts (int, char **, dspdev_t *);
int ossrecord_parse_opts (int, char **, dspdev_t *);
int play (dspdev_t *, int, unsigned long long *, unsigned long long, double,
          decoders_queue_t *, seekfunc_t *);
int record (dspdev_t *, FILE *, const char *, double, unsigned long long,
            unsigned long long *, decoders_queue_t * dec);
const char * sample_format_name (int);
int setup_device (dspdev_t *, int, int, int);
int silence (dspdev_t *, unsigned long long, int);
float format2bits (int);

void select_playtgt (dspdev_t *);
void select_recsrc (dspdev_t *);
void open_device (dspdev_t *);

#if !defined(OSS_BIG_ENDIAN) && !defined(OSS_LITTLE_ENDIAN)
#if AFMT_S16_NE == AFMT_S16_BE
#define OSS_BIG_ENDIAN
#else
#define OSS_LITTLE_ENDIAN
#endif /* AFMT_S16_NE == AFMT_S16_BE */
#endif /* !OSS_BIG_ENDIAN && !OSS_LITTLE_ENDIAN */

#include "ossplay_console.h"

#endif
