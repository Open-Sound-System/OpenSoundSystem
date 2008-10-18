/*
 * Purpose: File header write routines for OSSRecord.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <soundcard.h>
#include <errno.h>
#include "ossplay_wparser.h"

#pragma pack(1)

typedef struct {
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

typedef struct {
  char magic[4];
  unsigned int offset;
  unsigned int filelen;
  unsigned int fmt;
  unsigned int speed;
  unsigned int channels;
}
AuHeader;

typedef struct {
  char main_chunk[4];
  unsigned int length;
  char chunk_type[4];

  char sub_chunk[4];
  unsigned int comm_len;
  unsigned short channels;
  unsigned int num_frames;
  unsigned short bits;
  unsigned char speed[10];
  char data_chunk[4];
  unsigned int data_length;
  unsigned int offset;
  unsigned int blocksize;
}
AiffHeader;

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

int
write_head (FILE * wave_fp, fctypes_t type, unsigned int datalimit,
            int format, int channels, int speed)
{
  unsigned int dl = datalimit;

  if ((speed <= 0) || (channels <= 0) || (format <= 0)) return -1;

  switch (type)
    {
      case WAVE_FILE: {
        WaveHeader wh;
        int bits = format2bits (format);

        if (datalimit <= 0) dl = 0x7fff0000;
        memcpy ((char *) &wh.main_chunk, "RIFF", 4);
        wh.length = LE_INT (dl + sizeof (WaveHeader) - 8);
        memcpy ((char *) &wh.chunk_type, "WAVE", 4);
        memcpy ((char *) &wh.sub_chunk, "fmt ", 4);
        wh.sc_len = LE_INT (16);
        switch (format)
          {
            case AFMT_A_LAW:
              wh.format = LE_SH (6);
              break;
            case AFMT_MU_LAW:
              wh.format = LE_SH (7);
              break;
            case AFMT_U8:
            case AFMT_S16_LE:
            case AFMT_S24_LE:
            case AFMT_S24_PACKED:
            case AFMT_S32_LE:
              wh.format = LE_SH (1);
              break;
            default:
              print_msg (ERRORM,
                         "%s sample format not supported by WAV writer!\n",
                         sample_format_name (format));
              return -1;
          }
        wh.modus = LE_SH (channels);
        wh.sample_fq = LE_INT (speed);
        wh.block_align = LE_SH ((bits / 8) * channels);
        wh.byte_p_sec = LE_INT (speed * channels * (bits / 8));
        wh.bit_p_spl = LE_SH (bits);
        memcpy ((char *) &wh.data_chunk, "data", 4);
        wh.data_length = LE_INT (dl);
        if (fwrite (&wh, sizeof (WaveHeader), 1, wave_fp) == 0) return -1;
        } break;
      case AU_FILE: {
        AuHeader ah;

        if (datalimit <= 0) dl = (unsigned int)-1;
        memcpy ((char *) &ah.magic, ".snd", 4);
        ah.offset = BE_INT (24);
        ah.filelen = BE_INT (dl);
        switch (format)
          {
            case AFMT_MU_LAW: ah.fmt = BE_INT (1); break;
            case AFMT_S8: ah.fmt = BE_INT (2); break;
            case AFMT_S16_BE: ah.fmt = BE_INT (3);  break;
            case AFMT_S24_PACKED_BE: ah.fmt = BE_INT (4);  break;
            case AFMT_S32_BE: ah.fmt = BE_INT (5);  break;
            case AFMT_A_LAW: ah.fmt = BE_INT (27); break;
            default:
              print_msg (ERRORM,
                         "%s sample format not supported by AU writer!\n",
                         sample_format_name (format));
              return -1;
          }
        ah.speed = BE_INT (speed);
        ah.channels = BE_INT (channels);
        if (fwrite (&ah, sizeof (AuHeader), 1, wave_fp) == 0) return -1;
        } break;
      case AIFF_FILE: {
        AiffHeader afh;
        int bits = format2bits (format);
        int i;

        if (datalimit <= 0) dl = 0x7fff0000;
        if (dl % 2) dl++;
        memcpy ((char *) &afh.main_chunk, "FORM", 4);
        afh.length = BE_INT (dl + sizeof (AiffHeader) - 8);
        memcpy ((char *) &afh.chunk_type, "AIFF", 4);
        memcpy ((char *) &afh.sub_chunk, "COMM", 4);
        afh.comm_len = BE_INT (18);
        afh.channels = BE_SH (channels);
        afh.num_frames = BE_INT (dl / bits);
        afh.bits = BE_SH (bits);
        switch (format)
          {
            case AFMT_U8: 
            case AFMT_S16_BE:
            case AFMT_S24_PACKED_BE:
            case AFMT_S32_BE:
              break;
            default:
              print_msg (ERRORM,
                         "%s sample format not supported by AIFF writer!\n",
                         sample_format_name (format));
              return -1;
          }

        /* The method used here is good enough for sane values */
        memset (afh.speed, 0, sizeof (afh.speed));
        i = 0;
        while ((1L << (i + 1)) <= speed) i++;
        afh.speed[0] = 64; afh.speed[1] = i-1;
        i = (speed - (1 << i)) << (31-i);
        afh.speed[5] = i & 255;
        afh.speed[4] = (i >> 8) & 255;
        afh.speed[3] = (i >> 16) & 255;
        afh.speed[2] = ((i >> 24) & 127) + 128;

        memcpy ((char *) &afh.data_chunk, "SSND", 4);
        afh.data_length = BE_INT (dl);
        afh.offset = BE_SH (0);
        afh.blocksize = BE_SH (0);
        if (fwrite (&afh, sizeof (AiffHeader), 1, wave_fp) == 0) return -1;
        } break;
      case RAW_FILE:
      default: return 0;
    }
  return 0;
}

int
finalize_head (FILE * wave_fp, fctypes_t type, unsigned int datalimit,
               int format, int channels, int speed)
{
  char flag = 1;

  fseek (wave_fp, 0, SEEK_SET);
  if ((type == AIFF_FILE) && (datalimit % 2))
    {
      /*
       * All chunks must have an even length in an AIFF file,
       * so we have to add a pad byte in this case.
       */
      flag = 0;
      datalimit++;
    }
  write_head (wave_fp, type, datalimit, format, channels, speed);
  if (!flag)
    {
      fseek (wave_fp, 0, SEEK_END);
      if (fwrite (&flag , 1, 1, wave_fp) == -1)
        print_msg (ERRORM, "Couldn't add padding byte to SSND chunk!\n");
    }
  return 0;
}
