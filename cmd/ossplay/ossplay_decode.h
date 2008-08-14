#ifndef OSSPLAY_DECODE_H
#define OSSPLAY_DECODE_H

#include "ossplay.h"

#define MAC_IMA_BLKLEN		34
/*
 * ima4 block length in AIFC files. Qt has "stsd" chunk which can change this,
 * but I know of no AIFC equivalent.
 */

typedef struct verbose_values {
  char tstring[20];
  double next_sec;
  double next_sec2;
  double tsecs;
  double constant;
  int format;
  unsigned int * datamark;
}
verbose_values_t;

int decode_sound (dspdev_t *, int, unsigned int, int, int, int, void *);
int encode_sound (dspdev_t *, fctypes_t, const char *, int, int, int,
                  unsigned int);
int get_db_level (const unsigned char *, ssize_t, int);
verbose_values_t * setup_verbose (int, double, unsigned int *);

#endif
