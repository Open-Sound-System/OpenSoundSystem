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
  unsigned long long * datamark;
}
verbose_values_t;

errors_t decode_sound (dspdev_t *, int, unsigned long long, int, int, int,
                       void *);
errors_t encode_sound (dspdev_t *, fctypes_t, const char *, int, int, int,
                       unsigned long long);
int get_db_level (const unsigned char *, ssize_t, int);
verbose_values_t * setup_verbose (int, double, unsigned long long *);

#endif
