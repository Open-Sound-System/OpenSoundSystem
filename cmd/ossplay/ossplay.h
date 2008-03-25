#ifndef OSSPLAY_H
#define OSSPLAY_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <soundcard.h>
#include <sys/ioctl.h>

#define AFMT_MS_ADPCM	-(AFMT_S16_LE | 0x1000000)
#define AFMT_CR_ADPCM_2	-(AFMT_U8 | 0x1000000)
#define AFMT_CR_ADPCM_3	-(AFMT_U8 | 0x2000000)
#define AFMT_CR_ADPCM_4	-(AFMT_U8 | 0x4000000)
#define AFMT_FIBO_DELTA	-(AFMT_U8 | 0x10000000)
#define AFMT_EXP_DELTA	-(AFMT_U8 | 0x20000000)

typedef struct {
  int coeff1, coeff2;
}
adpcm_coeff;

typedef struct msadpcm_values {
  int nBlockAlign;
  int wSamplesPerBlock;
  int wNumCoeff;
  adpcm_coeff coeff[32];
}
msadpcm_values_t;

int setup_device (int, int, int, int);
int be_int (unsigned char *, int);
int le_int (unsigned char *, int);

#endif
