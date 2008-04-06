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
#define DEFAULT_CHANNELS	1
#define DEFAULT_FORMAT		AFMT_U8
#define DEFAULT_SPEED		11025

enum {
  ERRORM,
  HELPM,
  NORMALM,
  NOTIFYM,
  UPDATEM,
  CLEARUPDATEM,
  WARNM,
  STARTM,
  CONTM,
  ENDM
};

#define AFMT_MS_ADPCM	-(AFMT_S16_LE | 0x1000000)
#define AFMT_CR_ADPCM_2	-(AFMT_U8 | 0x1000000)
#define AFMT_CR_ADPCM_3	-(AFMT_U8 | 0x2000000)
#define AFMT_CR_ADPCM_4	-(AFMT_U8 | 0x4000000)
#define AFMT_FIBO_DELTA	-(AFMT_U8 | 0x10000000)
#define AFMT_EXP_DELTA	-(AFMT_U8 | 0x20000000)

typedef struct fmt_struct {
  const char const * name;
  const int fmt;
  const int may_conv;
}
format_t;

typedef struct {
  int coeff1, coeff2;
}
adpcm_coeff;

typedef struct msadpcm_values {
  int nBlockAlign;
  int wSamplesPerBlock;
  int wNumCoeff;
  adpcm_coeff coeff[32];

  int channels;
}
msadpcm_values_t;

int be_int (const unsigned char *, int);
const char * filepart (const char *);
int le_int (const unsigned char *, int);
void * ossplay_malloc (size_t);
void ossplay_free (void *);
char * ossplay_strdup (const char *);
int parse_opts (int, char **);
void perror_msg (const char * s);
void print_msg (char, const char *, ...);
void print_verbose (int, int, int);
int setup_device (int, int, int, int);

#endif
