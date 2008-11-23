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
#define MAX_CHANNELS		12
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

#define AFMT_MS_ADPCM		-(AFMT_S16_LE | 0x1000000)
#define AFMT_MS_IMA_ADPCM	-(AFMT_S16_LE | 0x2000000)
#define AFMT_MAC_IMA_ADPCM	-(AFMT_S16_LE | 0x4000000)
#define AFMT_S24_PACKED_LE	AFMT_S24_PACKED
#define AFMT_S24_PACKED_BE	-(AFMT_S32_LE | 0x1000000)
#define AFMT_CR_ADPCM_2		-(AFMT_U8 | 0x1000000)
#define AFMT_CR_ADPCM_3		-(AFMT_U8 | 0x2000000)
#define AFMT_CR_ADPCM_4		-(AFMT_U8 | 0x4000000)
#define AFMT_FIBO_DELTA		-(AFMT_U8 | 0x10000000)
#define AFMT_EXP_DELTA		-(AFMT_U8 | 0x20000000)

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
  CRP = CR&CP
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
typedef ssize_t (seekfunc_t) (int, unsigned long long *, unsigned long long, double,
                              unsigned long long, int);

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
