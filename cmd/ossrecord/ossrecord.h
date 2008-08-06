#ifndef _OSSRECORD_H
#define _OSSRECORD_H

void decorate (void *, int, int);
void end (void);
void open_audio (void);

typedef struct cnt_struct {
  const char * name;
  const int type;
  const int dformat;
  const int dchannels;
  const int dspeed;
} container_t;

typedef struct fmt_struct {
  const char * name;
  const int fmt;
} format_t;

extern int audio_fd, channels, datalimit, format, speed, type;

enum {
  WAVE_FILE,
  AU_FILE,
  RAW_FILE
};

#endif
