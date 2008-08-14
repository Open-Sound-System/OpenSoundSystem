#ifndef _OSSPLAY_CONSOLE_H
#define _OSSPLAY_CONSOLE_H

#include "ossplay.h"

typedef enum {
  ERRORM,
  HELPM,
  NORMALM,
  NOTIFYM,
  WARNM,
  STARTM,
  CONTM,
  ENDM,
  VERBOSEM
}
prtype_t;

void clear_update (void);
void ossplay_free (void *);
void * ossplay_malloc (size_t);
char * ossplay_strdup (const char *);
void perror_msg (const char * s);
void print_msg (prtype_t, const char *, ...);
void print_record_update (int, double, const char *, int);
void print_update (int, double, const char *);
char * totime (double);

#endif
