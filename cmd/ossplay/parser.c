/*
 * Purpose: File format parse routines for ossplay
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include "ossplay.h"
#include "parser.h"
#include "decode.h"

#include <ctype.h>

/* Magic numbers used in Sun and NeXT audio files (.au/.snd) */
#define SUN_MAGIC 	0x2e736e64	/* Really '.snd' */
#define SUN_INV_MAGIC	0x646e732e	/* '.snd' upside-down */
#define DEC_MAGIC	0x2e736400	/* Really '\0ds.' (for DEC) */
#define DEC_INV_MAGIC	0x0064732e	/* '\0ds.' upside-down */

/* Magic numbers for file formats based on IFF */
#define FORM_MAGIC	0x464f524d	/* 'FORM' */
#define AIFF_MAGIC	0x41494646	/* 'AIFF' */
#define AIFC_MAGIC	0x41494643	/* 'AIFC' */
#define _8SVX_MAGIC	0x38535658	/* '8SVX' */
#define _16SV_MAGIC	0x31365356	/* '16SV' */
#define MAUD_MAGIC	0x4D415544	/* 'MAUD' */

/* Magic numbers for .wav files */
#define RIFF_MAGIC	0x52494646	/* 'RIFF' */
#define RIFX_MAGIC	0x52494658	/* 'RIFX' */
#define WAVE_MAGIC	0x57415645	/* 'WAVE' */

/* Beginning of magic for Creative .voc files */
#define Crea_MAGIC	0x43726561	/* 'Crea' */

enum {
  AIFF_FILE,
  AIFC_FILE,
  WAVE_FILE,
  WAVE_FILE_BE,
  _8SVX_FILE,
  _16SV_FILE,
  MAUD_FILE
};

extern int audiofd, quitflag, quiet, verbose;
extern int raw_file, raw_mode, exitstatus, force_fmt;
#ifdef MPEG_SUPPORT
static int mpeg_enabled = 0;
#endif

static off_t (*ossplay_lseek) (int, off_t, int) = lseek;
static off_t ossplay_lseek_stdin (int, off_t, int);
static void play_au (const char *, int, unsigned char *, int);
static void play_iff (const char *, int, unsigned char *, int);
#ifdef MPEG_SUPPORT
extern void play_mpeg (const char *, int, unsigned char *, int);
#endif
static void play_voc (const char *, int, unsigned char *, int);

static off_t
ossplay_lseek_stdin (int fd, off_t off, int w)
{
  off_t i;
  ssize_t bytes_read;
  char buf[BUFSIZ];

  if (w == SEEK_END) return -1;
  if (off < 0) return -1;
  if (off == 0) return 0;
  i = off;
  while (i > 0)
    {
      bytes_read = read(fd, buf, (i > BUFSIZ)?BUFSIZ:i);
      if (bytes_read == -1) return -1;
      else if (bytes_read == 0) return off;
      i -= bytes_read;
    }
  return off;
}

void
play_file (const char * filename)
{
  int fd, l, i;
  unsigned char buf[PLAYBUF_SIZE];
  const char * suffix;

  if (!strcmp(filename, "-"))
    {
      FILE *fp;

      fp = fdopen(0, "rb");
      fd = fileno(fp);
      /*
       * Use emulation if stdin is not seekable (e.g. on Linux).
       */
      if (lseek (fd, 0, SEEK_CUR) == -1) ossplay_lseek = ossplay_lseek_stdin;
    }
  else fd = open (filename, O_RDONLY, 0);
  if (fd == -1)
    {
      perror_msg (filename);
      exitstatus++;
      return;
    }

  if (raw_file)
    {
      print_msg (NORMALM, "%s: Playing RAW file.\n", filename);

      decode_sound (fd, UINT_MAX, DEFAULT_FORMAT, DEFAULT_CHANNELS,
                    DEFAULT_SPEED, NULL);
      goto done;
    }

  if ((l = read (fd, buf, 12)) == -1)
    {
      perror_msg (filename);
      goto seekerror;
    }

  if (l == 0)
    {
      print_msg (ERRORM, "%s is empty file.\n", filename);
      goto seekerror;
    }

/*
 * Try to detect the file type - First .wav
 */
  switch (be_int (buf, 4))
    {
      case SUN_MAGIC:
      case DEC_MAGIC:
      case SUN_INV_MAGIC:
      case DEC_INV_MAGIC:
        if ((i = read (fd, buf + 12, 12)) == -1)
          {
            perror_msg (filename);
            goto seekerror;
          }
        l += i;
        if (l < 24) break;
        play_au (filename, fd, buf, l);
        goto done;
      case Crea_MAGIC:
        if ((i = read (fd, buf + 12, 7)) == -1)
          {
            perror_msg (filename);
            goto seekerror;
          }
        l += i;
        if ((l < 19) || (memcmp (buf, "Creative Voice File", 19))) break;
        play_voc (filename, fd, buf, l);
        goto done;
      case RIFF_MAGIC:
        if ((l < 12) || be_int (buf + 8, 4) != WAVE_MAGIC) break;
        play_iff (filename, fd, buf, WAVE_FILE);
        goto done;
      case RIFX_MAGIC:
        if ((l < 12) || be_int (buf + 8, 4) != WAVE_MAGIC) break;
        play_iff (filename, fd, buf, WAVE_FILE_BE);
        goto done;
      case FORM_MAGIC:
        if (l < 12) break;
        switch (be_int (buf + 8, 4))
          {
            case AIFF_MAGIC:
              play_iff (filename, fd, buf, AIFF_FILE);
              goto done;
            case AIFC_MAGIC:
              play_iff (filename, fd, buf, AIFC_FILE);
              goto done;
            case _8SVX_MAGIC:
              play_iff (filename, fd, buf, _8SVX_FILE);
              goto done;
            case _16SV_MAGIC:
              play_iff (filename, fd, buf, _16SV_FILE);
              goto done;
            case MAUD_MAGIC:
              play_iff (filename, fd, buf, MAUD_FILE);
              goto done;
            default: break;
          }
      default: break;
    }

  ossplay_lseek (fd, 0, SEEK_SET);	/* Start from the beginning */

/*
 *	The file was not identified by it's content. Try using the file name
 *	suffix.
 */

  suffix = strrchr (filename, '.');
  if (suffix == NULL) suffix = filename;

  if (strcmp (suffix, ".au") == 0 || strcmp (suffix, ".AU") == 0)
    {				/* Raw mu-Law data */
      print_msg (NORMALM, "Playing raw mu-Law file %s\n", filename);

      decode_sound (fd, UINT_MAX, AFMT_MU_LAW, 1, 8000, NULL);
      goto done;
    }

  if (strcmp (suffix, ".snd") == 0 || strcmp (suffix, ".SND") == 0)
    {
      print_msg (NORMALM,
                 "%s: Unknown format. Assuming RAW audio (%d/%d/%d.\n",
                 filename, DEFAULT_SPEED, DEFAULT_FORMAT, DEFAULT_CHANNELS);

      decode_sound (fd, UINT_MAX, DEFAULT_FORMAT, DEFAULT_CHANNELS,
                  DEFAULT_SPEED, NULL);
      goto done;
    }

  if (strcmp (suffix, ".cdr") == 0 || strcmp (suffix, ".CDR") == 0)
    {
      print_msg (NORMALM, "%s: Playing CD-R (cdwrite) file.\n", filename);

      decode_sound (fd, UINT_MAX, AFMT_S16_BE, 2, 44100, NULL);
      goto done;
    }


  if (strcmp (suffix, ".raw") == 0 || strcmp (suffix, ".RAW") == 0)
    {
      print_msg (NORMALM, "%s: Playing RAW file.\n", filename);

      decode_sound (fd, UINT_MAX, DEFAULT_FORMAT, DEFAULT_CHANNELS,
                  DEFAULT_SPEED, NULL);
      goto done;
    }

#ifdef MPEG_SUPPORT
  if (strcmp (suffix, ".mpg") == 0 || strcmp (suffix, ".MPG") == 0 ||
      strcmp (suffix, ".mp2") == 0 || strcmp (suffix, ".MP2") == 0)
    {				/* Mpeg file */
      int tmp;

      if (!mpeg_enabled)
	{
	  print_msg (ERRORM, "%s: Playing MPEG audio files is not available\n",
		     filename);
	  goto done;
	}

      print_msg (NORMALM, "Playing MPEG audio file %s\n", filename);

      if (!setup_device (fd, AFMT_S16_NE, 2, 44100))
	return;

      tmp = APF_NORMAL;
      ioctl (audiofd, SNDCTL_DSP_PROFILE, &tmp);
      play_mpeg (filename, fd, buf, l);
      goto done;
    }
#endif

  print_msg (ERRORM, "%s: Unrecognized audio file type.\n", filename);
  exitstatus++;
done:
  close (fd);

#if 0
  ioctl (audiofd, SNDCTL_DSP_SYNC, NULL);
#endif
  return;
seekerror:
  exitstatus++;
  close (fd);
}

/*ARGSUSED*/
static void
play_iff (const char * filename, int fd, unsigned char * buf, int type)
{
/*
 * Generalized IFF parser - handles WAV, AIFF, AIFC, 8SVX, 16SV and MAUD.
 */
  enum
  {
    COMM_BIT,
    SSND_BIT,
    FVER_BIT
  };
#define COMM_FOUND (1 << COMM_BIT)
#define SSND_FOUND (1 << SSND_BIT)
#define FVER_FOUND (1 << FVER_BIT)

#define LIST_HUNK 0x4c495354

#define FVER_HUNK 0x46564552
#define COMM_HUNK 0x434F4D4D
#define SSND_HUNK 0x53534E44
#define ANNO_HUNK 0x414E4E4F
#define NAME_HUNK 0x4E414D45
#define AUTH_HUNK 0x41555448
#define COPY_HUNK 0x28632920

#define VHDR_HUNK 0x56484452
#define BODY_HUNK 0x424f4459
#define CHAN_HUNK 0x4348414e

#define MDAT_HUNK 0x4D444154
#define MHDR_HUNK 0x4D484452

#define alaw_FMT 0x616C6177
#define ALAW_FMT 0x414C4157
#define ulaw_FMT 0x756C6177
#define ULAW_FMT 0x554C4157
#define sowt_FMT 0x736F7774
#define twos_FMT 0x74776F73
#define fl32_FMT 0x666C3332
#define FL32_FMT 0x464C3332
#define ima4_FMT 0x696D6134
#define NONE_FMT 0x4E4F4E45
#define raw_FMT  0x72617720
#define in24_FMT 0x696e3234
#define ni24_FMT 0x6e693234
#define in32_FMT 0x696E3332
#define ni32_FMT 0x6E693332

#define fmt_HUNK 0x666d7420
#define data_HUNK 0x64617461
#define INFO_HUNK 0x494e464f

#define ASEEK(fd, offset, n) \
  do { \
    if (ossplay_lseek (fd, offset, SEEK_CUR) == -1) \
      { \
        print_msg (ERRORM, "%s: error: cannot seek to end of " #n " chunk.\n", \
                 filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
  } while (0)

#define AREAD(fd, buf, len, n) \
  do { \
    if (chunk_size < len) \
      { \
        print_msg (ERRORM, \
                   "%s: error: chunk " #n " size is too small.\n", filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
    if (read(fd, buf, len) < len) \
      { \
        print_msg (ERRORM, "%s: error: cannot read " #n " chunk.\n",filename); \
        if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta; \
        else return; \
      } \
    ASEEK (fd, chunk_size - len, n); \
  } while (0)

#define BITS2SFORMAT(endian) \
  do { \
    switch (bits) \
      { \
         case 8: format = AFMT_S8; break; \
         case 16: format = AFMT_S16_##endian; break; \
         case 24: format = AFMT_S24_##endian; break; \
         case 32: format = AFMT_S32_##endian; break; \
         default: format = AFMT_S16_##endian; break; \
     } break; \
  } while (0)

  int channels = 1, bits = 8, format, speed = 11025;
  long double COMM_rate;
  unsigned int chunk_id, chunk_size = 18, csize = 12, found = 0, offset = 0,
               sound_loc = 0, sound_size = 0, timestamp, total_size;
  int (*ne_int) (const unsigned char *p, int l) = be_int;

  msadpcm_values_t msadpcm_val = {
    256, 496, 7, {
      {256, 0},
      {512, -256},
      {0, 0},
      {192, 64},
      {240, 0},
      {460, -208},
      {392, -232} },
    DEFAULT_CHANNELS
  };

  if (type == _8SVX_FILE) format = AFMT_S8;
  else format = AFMT_S16_BE;
  if (type == WAVE_FILE) ne_int = le_int;

  total_size = ne_int (buf + 4, 4);
  if (verbose > 1)
    print_msg (NORMALM, "Filelen = %u\n", total_size);
  do
    {
      if (read (fd, buf, 8) < 8)
        {
          print_msg (ERRORM, "%s: Cannot read chunk header at pos %u\n",
                     filename, csize);
          if ((found & SSND_FOUND) && (found & COMM_FOUND)) goto nexta;
          return;
        }
      chunk_id = be_int (buf, 4);
      chunk_size = ne_int (buf + 4, 4);
      if (chunk_size % 2) chunk_size += 1;

      switch (chunk_id)
        {
          /* AIFF / AIFC chunks */
          case COMM_HUNK:
            if (found & COMM_FOUND)
              {
                print_msg (ERRORM,
                         "%s: error: COMM hunk not singular!\n", filename);
                return;
              }
            if (type == AIFC_FILE) AREAD (fd, buf, 22, COMM);
            else AREAD (fd, buf, 18, COMM);
            found |= COMM_FOUND;

            channels = be_int (buf, 2);
#if 0
            num_frames = be_int (buf + 2, 4); /* ossplay doesn't use this */
#endif
            bits = be_int (buf + 6, 2);
            bits += bits % 8;
            BITS2SFORMAT (BE);
#if AFMT_S16_NE == AFMT_S16_LE
            {
              unsigned char tmp, *s = buf + 8; int i;

              for (i=0; i < 5; i++)
                {
                  tmp = s[i]; s[i] = s[9-i]; s[9-i] = tmp;
                }
            }
#endif
            memcpy (&COMM_rate, buf + 8, sizeof (COMM_rate));
            speed = (int)COMM_rate;

            if (type != AIFC_FILE)
              {
                csize += chunk_size + 8;
                continue;
              }

            switch (be_int (buf + 18, 4))
              {
                case NONE_FMT: break;
                case twos_FMT: format = AFMT_S16_BE; break;
                case in24_FMT: format = AFMT_S24_BE; break;
                case in32_FMT: format = AFMT_S32_BE; break;
                case ni24_FMT: format = AFMT_S24_LE; break;
                case ni32_FMT: format = AFMT_S32_LE; break;
                case sowt_FMT: BITS2SFORMAT (LE); break;
                /* Apple Docs refer to this as AFMT_S16_LE only, but some
                   programs misinterpret this. */
                case raw_FMT: format = AFMT_U8; break;
                case alaw_FMT:
                case ALAW_FMT: format = AFMT_A_LAW; break;
                case ulaw_FMT:
                case ULAW_FMT: format = AFMT_MU_LAW; break;
                case ima4_FMT: format = AFMT_IMA_ADPCM; break;
#if 0
                case fl32_FMT:
                case FL32_FMT: format = AFMT_FLOAT; break;
#endif
                default:
                  print_msg (ERRORM, 
                           "%s: error: %c%c%c%c compression is not supported\n",
                           filename, *(buf + 18), *(buf + 19),
                           *(buf + 20), *(buf + 21));
                  if (force_fmt == 0) return;
              }
              break;
          case SSND_HUNK:
            if (found & SSND_FOUND)
              {
                print_msg (ERRORM,
                           "%s: error: SSND hunk not singular!\n", filename);
                return;
              }
            if (chunk_size < 8)
              {
                print_msg (ERRORM,
                           "%s: error: impossibly small SSND hunk\n", filename);
                return;
              }
            if (read (fd, buf, 8) < 8)
              {
                print_msg (ERRORM, "%s: error: cannot read SSND chunk.\n",
                           filename);
                return;
              }
            found |= SSND_FOUND;

            offset = be_int (buf, 4);
#if 0
            block_size = be_int (buf + 4, 4); /* ossplay doesn't use this */
#endif
            sound_loc = csize + 16 + offset;
            sound_size = chunk_size - 8;

            if ((!strcmp (filename, "-")) &&
                (ossplay_lseek == ossplay_lseek_stdin))
              goto stdinext;
            ASEEK (fd, chunk_size - 8, SSND);
            break;
          case FVER_HUNK:
            AREAD (fd, buf, 4, FVER);
            timestamp = be_int (buf, 4);
            found |= FVER_FOUND;
            break;

          /* 8SVX / 16SV chunks */
          case VHDR_HUNK:
            AREAD (fd, buf, 16, VHDR);
            speed = be_int (buf + 12, 2);
            if (type == _8SVX_FILE) switch (buf[15])
              {
                case 0: format = AFMT_S8; break;
                case 1: format = AFMT_FIBO_DELTA; break;
                case 2: format = AFMT_EXP_DELTA; break;
                default:
                 print_msg (ERRORM, "%s: Unsupported compression %d\n",
                            filename, buf[15]);
                if (force_fmt == 0) return;
              }
            found |= COMM_FOUND;
            break;
          case data_HUNK: /* WAVE chunk */
            if (verbose > 2)
              print_msg (NORMALM,  "DATA chunk. Offs = %u, "
                         "len = %u\n", csize+8, chunk_size);
            sound_loc = csize + 8;
          case MDAT_HUNK: /* MAUD chunk */
          case BODY_HUNK:
            sound_size = chunk_size;
            if (chunk_id != data_HUNK) sound_loc = csize + 4;
            found |= SSND_FOUND;
            if ((!strcmp (filename, "-")) &&
                (ossplay_lseek == ossplay_lseek_stdin))
              goto stdinext;
            ASEEK (fd, chunk_size, BODY);
            break;
          case CHAN_HUNK:
            AREAD (fd, buf, 4, CHAN);
            channels = be_int (buf, 4);
            channels = (channels & 0x01) +
                       ((channels & 0x02) >> 1) +
                       ((channels & 0x04) >> 2) +
                       ((channels & 0x08) >> 3);
            break;

          /* MAUD chunks */
          case MHDR_HUNK:
            AREAD (fd, buf, 20, MHDR);
            bits = be_int (buf + 4, 2);
            BITS2SFORMAT (BE);
            speed = be_int (buf + 8, 4) / be_int (buf + 12, 2);
            channels = be_int (buf + 16, 2);
            switch (be_int (buf + 18, 2))
              {
                case 0: /* NONE */ break;
                case 2: format = AFMT_A_LAW; break;
                case 3: format = AFMT_MU_LAW; break;
                case 6: format = AFMT_IMA_ADPCM; break;
                default:
                  print_msg (ERRORM, "%s: format not supported", filename);
                  if (force_fmt == 0) return;
              }
            found |= COMM_FOUND;
            break;

          /* WAVE chunks */
          case fmt_HUNK: { int len, i, x;
            if (found & COMM_FOUND)
              {
                print_msg (ERRORM, "%s: error: fmt hunk not singular!\n",
                           filename);
                return;
              }
            if (chunk_size > 1024) len = 1024;
            else len = chunk_size;
            AREAD (fd, buf, len, fmt);

            format = ne_int (buf, 2);
            if (verbose > 2)
              print_msg (NORMALM,  "FMT chunk: len = %u, fmt = %#x\n",
                         chunk_size, format);

            msadpcm_val.channels = channels = ne_int (buf + 2, 2);
            speed = ne_int (buf + 4, 4);
            msadpcm_val.nBlockAlign = ne_int (buf + 12, 2);
            bits = ne_int (buf + 14, 2);
            bits += bits % 8;

            if (format == 0xFFFE)
              {
                if (chunk_size < 40)
                   {
                     print_msg (ERRORM, "%s: invallid fmt chunk\n", filename);
                     return;
                   }
                format = ne_int (buf + 24, 2);
              }
            switch (format)
              {
                case 0x1:
                  if (type == WAVE_FILE) BITS2SFORMAT (LE);
                  else BITS2SFORMAT (BE);
                  if (bits == 8) format = AFMT_U8;
                  break;
                case 0x2: format = AFMT_MS_ADPCM; break;
                case 0x6: format = AFMT_A_LAW; break;
                case 0x7: format = AFMT_MU_LAW; break;
                case 0x11: format = AFMT_IMA_ADPCM; break;
#if 0
                case 0x3: format = AFMT_FLOAT; break;
                case 0x50: /* MPEG */
                case 0x55: /* MPEG 3 */
#endif
                default:
                  print_msg (ERRORM, "%s: Unsupported wave format %#x\n",
                             filename, format);
                  if (force_fmt == 0) return;
              }
            found |= COMM_FOUND;

            if ((format != AFMT_MS_ADPCM) || (chunk_size < 20)) break;
            msadpcm_val.wSamplesPerBlock = ne_int (buf + 18, 2);
            if (chunk_size < 22) break;
            msadpcm_val.wNumCoeff = ne_int (buf + 20, 2);
            if (msadpcm_val.wNumCoeff > 32) msadpcm_val.wNumCoeff = 32;

            x = 22;

            for (i = 0; (i < msadpcm_val.wNumCoeff) && (x < chunk_size-3); i++)
              {
                msadpcm_val.coeff[i].coeff1 = (short) ne_int (buf + x, 2);
                x += 2;
                msadpcm_val.coeff[i].coeff2 = (short) ne_int (buf + x, 2);
                x += 2;
              }
            } break;

          /* common chunks */
          case NAME_HUNK:
          case AUTH_HUNK:
          case ANNO_HUNK:
          case COPY_HUNK:
            if (verbose)
              {
                int i, len;

                print_msg (STARTM,  "%s: ", filename);
                if (chunk_size > 1024) len = 1024;
                else len = chunk_size;
                switch (chunk_id)
                  {
                    case NAME_HUNK:
                      print_msg (CONTM, "Name: ");
                      AREAD (fd, buf, len, NAME);
                      break;
                    case AUTH_HUNK:
                      print_msg (CONTM, "Author: ");
                      AREAD (fd, buf, len, AUTH);
                      break;
                    case COPY_HUNK:
                      print_msg (CONTM, "Copyright: ");
                      AREAD (fd, buf, len, COPY);
                      break;
                    case ANNO_HUNK:
                      print_msg (CONTM, "Annonations: ");
                      AREAD (fd, buf, len, ANNO);
                      break;
                  }
                for (i = 0; i < len; i++)
                  print_msg (CONTM, "%c", isprint (buf[i])?buf[i]:' ');
                print_msg (ENDM, "\n");
                break;
              }

          default:
            ASEEK (fd, chunk_size, UNKNOWN);
            break;
       }

      csize += chunk_size + 8;

    } while (csize < total_size);

nexta:
    if ((found & COMM_FOUND) == 0)
      {
        print_msg (ERRORM, "%s: Couldn't find format chunk!\n", filename);
        return;
      }

    if ((found & SSND_FOUND) == 0)
      {
        print_msg (ERRORM, "%s: Couldn't find sound chunk!\n", filename);
        return;
      }

    if ((type == AIFC_FILE) && ((found & FVER_FOUND) == 0))
      print_msg (ERRORM, "%s: Couldn't find AIFFC FVER chunk.\n", filename);

    if (ossplay_lseek (fd, sound_loc, SEEK_SET) == -1)
      {
        perror_msg (filename);
        print_msg (ERRORM, "Can't seek in file\n");
        return;
      }

stdinext:
  if (!quiet)
    {
      if (type == AIFF_FILE)
        print_msg (NORMALM, "Playing AIFF file %s, ", filename);
      else if (type == AIFC_FILE)
        print_msg (NORMALM, "Playing AIFC file %s, ", filename);
      else if ((type == WAVE_FILE) || (type == WAVE_FILE_BE))
        print_msg (NORMALM, "Playing WAVE file %s, ", filename);
      else if (type == _8SVX_FILE)
        print_msg (NORMALM, "Playing 8SVX file %s, ", filename);
      else if (type == _16SV_FILE)
        print_msg (NORMALM, "Playing 16SV file %s, ", filename);
      else
        print_msg (NORMALM, "Playing MAUD file %s, ", filename);
      print_verbose (format, channels, speed);
    }

  decode_sound (fd, sound_size, format, channels, speed, (void *)&msadpcm_val);

  return;
}

/*ARGSUSED*/
static void
play_au (const char * filename, int fd, unsigned char * hdr, int l)
{
  int channels = 1, format = AFMT_S8, speed = 11025;
  unsigned int filelen, fmt = 0, i, p = 24, an_len = 0;

  p = be_int (hdr + 4, 4);
  filelen = be_int (hdr + 8, 4);
  fmt = be_int (hdr + 12, 4);
  speed = be_int (hdr + 16, 4);
  channels = be_int (hdr + 20, 4);

  if (verbose > 1)
    {
      if (filelen == (unsigned int)-1)
        print_msg (NORMALM, "%s: Filelen: unspecified\n", filename);
      else
        print_msg (NORMALM, "%s: Filelen: %u\n", filename, filelen);
    }
  if (verbose > 2) print_msg (NORMALM, "%s: Offset: %u\n", filename, p);
  if (filelen == (unsigned int)-1) filelen = UINT_MAX;

  switch (fmt)
    {
    case 1:
      format = AFMT_MU_LAW;
      break;

    case 2:
      format = AFMT_S8;
      break;

    case 3:
      format = AFMT_S16_BE;
      break;

    case 4:
      format = AFMT_S24_BE;
      break;

    case 5:
      format = AFMT_S32_BE;
      break;

    case 6:
    case 7:
      print_msg (ERRORM, "%s: Floating point encoded .au files are not supported",
                 filename);
      if (force_fmt == 0) return;

    case 23:
    case 24:
    case 25:
    case 26:
      print_msg (ERRORM, "%s: G.72x ADPCM encoded .au files are not supported",
                 filename);
      if (force_fmt == 0) return;

    case 27:
      format = AFMT_A_LAW;
      break;

    default:
      print_msg (ERRORM, "%s: Unknown encoding %d.\n", filename, fmt);
      if (force_fmt == 0) return;
    }

  if (!quiet)
    {
      print_msg (NORMALM, "Playing .au file %s, ", filename);
      print_verbose (format, channels, speed);

      if ((verbose) && (p > 24))
	{
          if (p > 1048) an_len = 1024;
          else an_len = p - 24;
          if (read (fd, hdr, an_len) < an_len)
            {
              print_msg (ERRORM, "%s: Can't read to pos %u\n", filename,
                         an_len + 24);
              return;
            }
	  print_msg (NORMALM, "%s: Annotations: ", filename);
	  for (i = 0; i < an_len; i++)
	    print_msg (NORMALM, "%c", isprint (hdr[i])?hdr[i]:'.');
	  print_msg (NORMALM, "\n");
	}
    }

  if (ossplay_lseek (fd, p - l - an_len, SEEK_CUR) == -1)
    {
      perror_msg (filename);
      print_msg (ERRORM, "Can't seek to the data chunk\n");
      return;
    }

  decode_sound (fd, filelen, format, channels, speed, NULL);
}

/*ARGSUSED*/
static void
play_voc (const char * filename, int fd, unsigned char * hdr, int l)
{
#define VREAD(fd, buf, len) \
  do { \
    if (read (fd, buf, len) < len) \
      { \
        print_msg (ERRORM, "%s: Can't read %d bytes at pos %d\n", \
                   filename, len, l); \
        return; \
      } \
    pos += len; \
  } while (0)

  unsigned int data_offs, vers, id, len, blklen, fmt, tmp,
               loopcount = 0, loopoffs = 4, pos = l + 7;
  unsigned char buf[256], plock = 0, block_type;
  int speed = 11025, channels = 1, bits = 8, format = AFMT_U8;

  if (read (fd, hdr + 19, 7) < 7)
    {
      print_msg (ERRORM, "%s: Not a valid .VOC file\n", filename);
      return;
    }

  data_offs = le_int (hdr + 0x14, 2);
  vers = le_int (hdr + 0x16, 2);
  id = le_int (hdr + 0x18, 2);

  if ((((~vers) + 0x1234) & 0xffff) != id)
    {
      print_msg (ERRORM, "%s: Not a valid .VOC file\n", filename);
      return;
    }

  print_msg (NORMALM, "Playing .VOC file %s\n", filename);

   /*LINTED*/ while (1)
    {
      if (ossplay_lseek (fd, data_offs - pos, SEEK_CUR) == -1)
        {
          print_msg (ERRORM, "%s: Can't seek to pos %d\n", filename, data_offs);
          return;
        }
      pos = data_offs + 4;

      if ((tmp = read (fd, buf, 1)) < 1)
        {
          /* Don't warn when read returns 0 - it may be end of file. */
          if (tmp != 0)
            print_msg (ERRORM,
                       "%s: Can't read 1 byte at pos %d\n", filename, l);
          return;
        }

      block_type = buf[0];

      if (block_type == 0)
	return;			/* End */

      if (read (fd, buf, 3) != 3)
	{
	  print_msg (ERRORM, "%s: Truncated .VOC file (%d)\n",
		     filename, buf[0]);
	  return;
	}

      blklen = len = le_int (buf, 3);

      if (verbose > 2)
	print_msg (NORMALM, "%s: %0x: Block type %d, len %d\n",
		   filename, data_offs, block_type, len);
      switch (block_type)
	{

	case 1:		/* Sound data buf */
          if (!plock)
            {
	      VREAD (fd, buf, 2);

	      tmp = 256 - buf[0];	/* Time constant */
	      speed = (1000000 + tmp / 2) / tmp / channels;

               fmt = buf[1];
               len -= 2;

               switch (fmt)
                 {
                   case 0: format = AFMT_U8; break;
                   case 1: format = AFMT_CR_ADPCM_4; break;
                   case 2: format = AFMT_CR_ADPCM_3; break;
                   case 3: format = AFMT_CR_ADPCM_2; break;
                   case 4: format = AFMT_S16_LE; break;
                   case 6: format = AFMT_A_LAW; break;
                   case 7: format = AFMT_MU_LAW; break;
                   default:
                     print_msg (ERRORM,
                                "%s: encoding %d is not supported\n",
                                filename, fmt);
                     if (force_fmt == 0) return;
                 }
            }

	case 2:		/* Continuation data */
          if (decode_sound (fd, len, format, channels, speed, NULL) < 0)
            return;
          pos += len;
	  break;

	case 3:		/* Silence */
	  VREAD (fd, buf, 3);
	  len = le_int (buf, 2);
	  tmp = 256 - buf[2];	/* Time constant */
	  speed = (1000000 + tmp / 2) / tmp;
          silence (len, speed);
	  break;

        case 5: 	/* Text */
          if (!quiet)
            {
              int i;

              if (len > 256) len = 256;
              VREAD (fd, buf, len);
              print_msg (STARTM, "Text: ");
              for (i = 0; i < len; i++)
                print_msg (CONTM, "%c", isprint(buf[i])?buf[i]:'.');
              print_msg (ENDM, "\n");
            }
          break;

        case 6:		/* Loop start */
          VREAD (fd, buf, 2);
          loopoffs = data_offs + blklen + 4;
          loopcount = le_int (buf, 2);
          break;

        case 7:		/* End of repeat loop */
          if (loopcount != 0xffff) loopcount--;

          /* Set "return" point. Compensate for increment of data_offs. */
          if (loopcount > 0) data_offs = loopoffs - blklen - 4;

          break;

        case 8:		/* Sampling parameters */
          VREAD (fd, buf, 4);

          speed = 256000000/(channels * (65536 - le_int (buf, 2)));
          channels = buf[3] + 1;
          fmt = buf[2];
          switch (fmt)
            {
              case 0: format = AFMT_U8; break;
              case 1: format = AFMT_CR_ADPCM_4; break;
              case 2: format = AFMT_CR_ADPCM_3; break;
              case 3: format = AFMT_CR_ADPCM_2; break;
              case 4: format = AFMT_S16_LE; break;
              case 6: format = AFMT_A_LAW; break;
              case 7: format = AFMT_MU_LAW; break;
              default:
                print_msg (ERRORM,
                           "%s: encoding %d is not supported\n", filename, fmt);
                if (force_fmt == 0) return;
            }
          plock = 1;
          break;

        case 9:		/* New format sound data */
          VREAD (fd, buf, 12);

          len -= 12;

          speed = le_int (buf, 3);
          bits = buf[4];
          channels = buf[5];
          fmt = le_int (buf + 6, 2);

          switch (fmt)
            {
              case 0: format = AFMT_U8; break;
              case 1: format = AFMT_CR_ADPCM_4; break;
              case 2: format = AFMT_CR_ADPCM_3; break;
              case 3: format = AFMT_CR_ADPCM_2; break;
              case 4: format = AFMT_S16_LE; break;
              case 6: format = AFMT_A_LAW; break;
              case 7: format = AFMT_MU_LAW; break;
              default:
                print_msg (ERRORM,
                           "%s: encoding %d is not supported\n", filename, fmt);
                if (force_fmt == 0) return;
            }

          if (decode_sound (fd, len, format, channels, speed, NULL) < 0)
            return;
          pos += len;
	  break;
	}

      if (block_type != 8) plock = 0;
      data_offs += blklen + 4;
    }
}
