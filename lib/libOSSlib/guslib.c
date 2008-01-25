#ifndef PATCH_PATH
#define PATCH_PATH "/usr/local/lib/midia/instruments"
#endif

#define OLD_PATCH_PATH "/dos/ultrasnd/midi"

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "../../include/soundcard.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "gmidi.h"

static int instrmap[256] = { -1, 0 };
static int initialized = 0;

struct pat_header
{
  char magic[12];
  char version[10];
  char description[60];
  unsigned char instruments;
  char voices;
  char channels;
  unsigned short nr_waveforms;
  unsigned short master_volume;
  unsigned long data_size;
};

struct sample_header
{
  char name[7];
  unsigned char fractions;
  long len;
  long loop_start;
  long loop_end;
  unsigned short base_freq;
  long low_note;
  long high_note;
  long base_note;
  short detune;
  unsigned char panning;

  unsigned char envelope_rate[6];
  unsigned char envelope_offset[6];

  unsigned char tremolo_sweep;
  unsigned char tremolo_rate;
  unsigned char tremolo_depth;

  unsigned char vibrato_sweep;
  unsigned char vibrato_rate;
  unsigned char vibrato_depth;

  char modes;

  short scale_frequency;
  unsigned short scale_factor;
};

SEQ_USE_EXTBUF ();

int
get_dint (unsigned char *p)
{
  int i;
  unsigned int v = 0;

  for (i = 0; i < 4; i++)
    {
      v |= (p[i] << (i * 8));
    }
  return (int) v;
}

unsigned short
get_word (unsigned char *p)
{
  int i;
  unsigned short v = 0;

  for (i = 0; i < 2; i++)
    v |= (*p++ << (i * 8));
  return (short) v;
}

short
get_int (unsigned char *p)
{
  return (short) get_word (p);
}

int
gusinit (int seqfd, int dev)
{

  initialized = 1;

  if (ioctl (seqfd, SNDCTL_SEQ_RESETSAMPLES, &dev) == -1)
    {
      fprintf (stderr, "OSSlib: Can't reset GM programs. Device %d\n", dev);
      perror ("GUS init");
      exit (-1);
    }

  return 0;
}

static char *gusdir = NULL;

int
gusload (int seqfd, int type, int dev, int pgm)
{
  int i, n, patfd, print_only = 0;
  struct synth_info info;
  struct pat_header header;
  struct sample_header sample;
  unsigned char buf[256];
  char name[256];
  long offset;
  struct patch_info *patch;

  if (!initialized)
    gusinit (seqfd, dev);

  if (pgm < 0 || pgm > 255)
    return 0;

  if (instrmap[pgm] == pgm)
    return 0;

  instrmap[pgm] = pgm;

  if (patch_names[pgm][0] == 0)	/* Not defined */
    return 0;

  if (gusdir == NULL)
    {
      if ((gusdir = getenv ("GUSPATCHDIR")) == NULL)
	{
#if 0
	  struct stat st;

	  gusdir = PATCH_PATH;

	  if (stat (gusdir, &st) == -1)
	    if (stat (OLD_PATCH_PATH, &st) == 0)
	      gusdir = OLD_PATCH_PATH;
#else
	  gusdir = PATCH_PATH;
#endif
	}
    }

  sprintf (name, "%s/%s.pat", gusdir, patch_names[pgm]);
/* fprintf(stderr, "Loading instrument %d from %s\n", pgm, name); */

  if ((patfd = open (name, O_RDONLY, 0)) == -1)
    {
      static int warned = 0;
      if (!warned)
	{
	  perror (name);
	  fprintf (stderr,
		   "\tThe GUS patch set doesn't appear to be installed.\n");
	  fprintf (stderr,
		   "\tOSSlib expects to find the \"Gravis\" or \"Midia\" patchset.\n");
	  fprintf (stderr, "\tin %s.\n", gusdir);
	  fprintf (stderr,
		   "\tYou can set the GUSPATCHDIR environment variable in case\n");
	  fprintf (stderr,
		   "\tthe .pat files are installed in some other directory.\n");
	}
      warned = 1;
      return 0;
    }

  if (read (patfd, buf, 0xef) != 0xef)
    {
      fprintf (stderr, "%s: Short file\n", name);
      exit (-1);
    }

  memcpy ((char *) &header, buf, sizeof (header));

  if (strncmp (header.magic, "GF1PATCH110", 12))
    {
      fprintf (stderr, "%s: Not a patch file\n", name);
      exit (-1);
    }

  if (strncmp (header.version, "ID#000002", 10))
    {
      fprintf (stderr, "%s: Incompatible patch file version\n", name);
      exit (-1);
    }

  header.nr_waveforms = get_word (&buf[85]);
  header.master_volume = get_word (&buf[87]);

  offset = 0xef;

  for (i = 0; i < header.nr_waveforms; i++)
    {
      if (lseek (patfd, offset, 0) == -1)
	{
	  perror (name);
	  exit (-1);
	}

      if (read (patfd, &buf, sizeof (sample)) != sizeof (sample))
	{
	  fprintf (stderr, "%s: Short file\n", name);
	  exit (-1);
	}

      memcpy ((char *) &sample, buf, sizeof (sample));

      sample.fractions = (char) buf[7];
      sample.len = get_dint (&buf[8]);
      sample.loop_start = get_dint (&buf[12]);
      sample.loop_end = get_dint (&buf[16]);
      sample.base_freq = get_word (&buf[20]);
      sample.low_note = get_dint (&buf[22]);
      sample.high_note = get_dint (&buf[26]);
      sample.base_note = get_dint (&buf[30]);
      sample.detune = get_int (&buf[34]);
      sample.panning = (unsigned char) buf[36];

      memcpy (sample.envelope_rate, &buf[37], 6);
      memcpy (sample.envelope_offset, &buf[43], 6);

      sample.tremolo_sweep = (unsigned char) buf[49];
      sample.tremolo_rate = (unsigned char) buf[50];
      sample.tremolo_depth = (unsigned char) buf[51];

      sample.vibrato_sweep = (unsigned char) buf[52];
      sample.vibrato_rate = (unsigned char) buf[53];
      sample.vibrato_depth = (unsigned char) buf[54];
      sample.modes = (unsigned char) buf[55];
      sample.scale_frequency = get_int (&buf[56]);
      sample.scale_factor = get_word (&buf[58]);

      offset = offset + 96;
      patch = (struct patch_info *) malloc (sizeof (*patch) + sample.len);
      if (patch == NULL)
	{
	  fprintf (stderr, "Failed to allocate %d bytes of memory\n",
		   sizeof (*patch) + sample.len);
	  exit (0);
	}

      patch->key = GUS_PATCH;
      patch->device_no = dev;
      patch->instr_no = pgm;
      patch->mode = sample.modes | WAVE_TREMOLO | WAVE_VIBRATO | WAVE_SCALE;
      patch->len = sample.len;
      patch->loop_start = sample.loop_start;
      patch->loop_end = sample.loop_end;
      patch->base_note = sample.base_note;
      patch->high_note = sample.high_note;
      patch->low_note = sample.low_note;
      patch->base_freq = sample.base_freq;
      patch->detuning = sample.detune;
      patch->panning = (sample.panning - 7) * 16;

      memcpy (patch->env_rate, sample.envelope_rate, 6);
      memcpy (patch->env_offset, sample.envelope_offset, 6);

      patch->tremolo_sweep = sample.tremolo_sweep;
      patch->tremolo_rate = sample.tremolo_rate;
      patch->tremolo_depth = sample.tremolo_depth;

      patch->vibrato_sweep = sample.vibrato_sweep;
      patch->vibrato_rate = sample.vibrato_rate;
      patch->vibrato_depth = sample.vibrato_depth;

      patch->scale_frequency = sample.scale_frequency;
      patch->scale_factor = sample.scale_factor;

      patch->volume = header.master_volume;

      if (lseek (patfd, offset, 0) == -1)
	{
	  perror (name);
	  exit (-1);
	}

      if (read (patfd, patch->data, sample.len) != sample.len)
	{
	  fprintf (stderr, "%s: Short file\n", name);
	  exit (-1);
	}

      SEQ_WRPATCH (patch, sizeof (*patch) + sample.len);

      offset = offset + sample.len;
    }

  close (patfd);
  free (patch);
  return 0;
}
