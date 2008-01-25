#define INSTR_FILE "std.o3"
#define DRUMS_FILE "drums.o3"

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "../../include/soundcard.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int instrmap[256] = { -1, 0 };
static int initialized = 0;

static char dir_path[256] = "/etc";

SEQ_USE_EXTBUF ();

static int drumfile = -1, stdfile = -1;

static void
find_ossdir (void)
{
  FILE *f;
  int i, l;
  char line[1024], *p;

  if ((f = fopen ("/etc/oss.conf", "r")) == NULL)
    {
      return;
    }

  while (fgets (line, 1000, f) != NULL)
    {
      for (i = 0; i < strlen (line); i++)
	if (line[i] == '\n')
	  line[i] = 0;

      p = line;
      while (*p && *p != '=')
	p++;
      if (*p == 0)
	continue;

      *p++ = 0;

      if (*p != '/')
	continue;
      if (strcmp (line, "OSSLIBDIR") == 0)
	{
	  strcpy (dir_path, p);
	  fclose (f);
	  return;
	}
    }

  fclose (f);
}

int
opl3init (int seqfd, int dev)
{

  if (!initialized)
    {
      char fname[256];

      find_ossdir ();

      sprintf (fname, "%s/etc/%s", dir_path, INSTR_FILE);
      if ((stdfile = open (fname, O_RDONLY, 0)) == -1)
	{
	  perror (fname);
	}
      sprintf (fname, "%s/etc/%s", dir_path, DRUMS_FILE);
      if ((drumfile = open (fname, O_RDONLY, 0)) == -1)
	{
	  perror (fname);
	}
    }
  initialized = 1;

#if 0
  if (ioctl (seqfd, SNDCTL_SEQ_RESETSAMPLES, &dev) == -1)
    {
      fprintf (stderr, "OSSlib: Can't reset GM programs. Device %d\n", dev);
      perror ("OPL3 init");
      exit (-1);
    }
#endif

  return 0;
}

int
opl3load (int seqfd, int type, int dev, int pgm)
{
  int i, n, patfd, print_only = 0;
  unsigned char buf[256];
  struct sbi_instrument patch;
  char *name;

  if (!initialized)
    opl3init (seqfd, dev);

  if (stdfile == -1 || drumfile == -1)
    return 0;

  if (pgm < 0 || pgm > 255)
    return 0;

  if (instrmap[pgm] == pgm)
    return 0;

  instrmap[pgm] = pgm;

  patch.key = OPL3_PATCH;
  patch.device = dev;
  patch.channel = pgm;

  if (pgm > 127)
    {
      name = DRUMS_FILE;
      if (lseek (drumfile, (pgm - 128) * 60, SEEK_SET) == -1)
	{
	  perror (DRUMS_FILE);
	  exit (-1);
	}
      if (read (drumfile, buf, 60) != 60)
	{
	  perror (DRUMS_FILE);
	  exit (-1);
	}
    }
  else
    {
      name = INSTR_FILE;
      if (lseek (stdfile, pgm * 60, SEEK_SET) == -1)
	{
	  perror (INSTR_FILE);
	  exit (-1);
	}
      if (read (stdfile, buf, 60) != 60)
	{
	  perror (INSTR_FILE);
	  exit (-1);
	}
    }

  if (strncmp ((char *) buf, "4OP", 3) != 0)
    if (strncmp ((char *) buf, "2OP", 3) == 0)
      {
	patch.key = FM_PATCH;
      }
    else
      {
	fprintf (stderr, "OSSlib: Invalid OPL3 patch file %s, instr=%d\n",
		 name, pgm % 128);
	return 0;
      }

  for (i = 0; i < 22; i++)
    patch.operators[i] = buf[i + 36];
  SEQ_WRPATCH (&patch, sizeof (patch));
  return 0;
}
