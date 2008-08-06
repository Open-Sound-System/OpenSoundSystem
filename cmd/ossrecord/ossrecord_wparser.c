/* Purpose: Simple wave file (.wav) recorder for OSS
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <soundcard.h>
#include <errno.h>
#include "ossrecord.h"
#include "ossrecord_wparser.h"

#define AU		".snd"
#define RIFF		"RIFF"
#define WAVE		"WAVE"
#define FMT		"fmt "
#define DATA		"data"
#define WAV_PCM_CODE	1
#define WAV_MU_CODE	7
#define WAVE_MONO	1
#define WAVE_STEREO	2

#pragma pack(1)

typedef struct
{
  char main_chunk[4];
  unsigned int length;
  char chunk_type[4];

  char sub_chunk[4];
  unsigned int sc_len;
  unsigned short format;
  unsigned short modus;
  unsigned int sample_fq;
  unsigned int byte_p_sec;
  unsigned short block_align;
  unsigned short bit_p_spl;

  char data_chunk[4];
  unsigned int data_length;

}
WaveHeader;

typedef struct
{
  char magic[4];
  unsigned int offset;
  unsigned int filelen;
  unsigned int fmt;
  unsigned int speed;
  unsigned int channels;
}
AuHeader;

#pragma pack()

extern FILE *wave_fp;

static int
bswap (int x)
{
  int y = 0;
  unsigned char *a = ((unsigned char *) &x) + 3;
  unsigned char *b = (unsigned char *) &y;

  *b++ = *a--;
  *b++ = *a--;
  *b++ = *a--;
  *b++ = *a--;

  return y;
}

#ifdef OSS_BIG_ENDIAN
static short
bswaps (short x)
{
  short y = 0;
  unsigned char *a = ((unsigned char *) &x) + 1;
  unsigned char *b = (unsigned char *) &y;

  *b++ = *a--;
  *b++ = *a--;

  return y;
}
#endif

int
format2obits (int format)
{
  switch (format)
    {
      case AFMT_MU_LAW:
      case AFMT_A_LAW:
      case AFMT_U8:
      case AFMT_S8: return 8;
      case AFMT_VORBIS:
      case AFMT_MPEG:
      case AFMT_IMA_ADPCM:
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

int
write_head (void)
{
  int dl = datalimit;
  WaveHeader wh;
  AuHeader ah;
  int bits = format2obits (format);

  switch (type)
    {
      case WAVE_FILE:
        if (datalimit <= 0) dl = 0x7fff0000;
        memcpy ((char *) &wh.main_chunk, RIFF, 4);
        wh.length = LE_INT (dl + sizeof (WaveHeader) - 8);
        memcpy ((char *) &wh.chunk_type, WAVE, 4);
        memcpy ((char *) &wh.sub_chunk, FMT, 4);
        wh.sc_len = LE_INT (16);
        switch (format)
          {
            case AFMT_MU_LAW:
              wh.format = LE_SH (WAV_MU_CODE);
              break;
            case AFMT_U8:
            case AFMT_S16_LE:
            case AFMT_S24_LE:
            case AFMT_S24_PACKED:
            case AFMT_S32_LE:
              wh.format = LE_SH (WAV_PCM_CODE);
              break;
            default:
              fprintf (stderr, "Format not supported by WAV writer!\n");
              return -1;
          }
        wh.modus = LE_SH (channels);
        wh.sample_fq = LE_INT (speed);
        wh.block_align = LE_SH ((bits / 8) * channels);
        wh.byte_p_sec = LE_INT (speed * channels * (bits / 8));
        wh.bit_p_spl = LE_SH (bits);
        memcpy ((char *) &wh.data_chunk, DATA, 4);
        wh.data_length = LE_INT (dl);
        if (fwrite (&wh, sizeof (WaveHeader), 1, wave_fp) == 0) return -1;
        break;
      case AU_FILE:
        if (datalimit <= 0) dl = 0x7fff0000;
        memcpy ((char *) &ah.magic, AU, 4);
	ah.offset = BE_INT (24);
        ah.filelen = BE_INT (dl);
        switch (format)
          {
            case AFMT_MU_LAW: ah.fmt = BE_INT(1); break;
            case AFMT_S8: ah.fmt = BE_INT(2); break;
            case AFMT_S16_BE: ah.fmt = BE_INT(3);  break;
            case AFMT_S24_BE: ah.fmt = BE_INT(4);  break;
            case AFMT_S32_BE: ah.fmt = BE_INT(5);  break;
            default:
              fprintf (stderr, "Format not supported by WAV writer!\n");
              return -1;
          }
        ah.speed = BE_INT (speed);
        ah.channels = BE_INT (channels);
        if (fwrite (&ah, sizeof (AuHeader), 1, wave_fp) == 0) return -1;
        break;
      case RAW_FILE: return 0;
    }
  return 0;
}
