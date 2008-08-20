/*
 * Purpose: Console output interface functions and related.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

#include <sys/wait.h>
#include "ossplay_console.h"
#include "ossplay_parser.h"
#include "ossplay_decode.h"

extern int exitstatus, eflag, from_stdin, loop, quiet, verbose;

static FILE * normalout;
static int dots = -11, direction = 0;

void
perror_msg (const char * s)
{
  perror (s);
}

void
clear_update (void)
{
  if (verbose) fprintf (normalout, "\r\n");
  dots = -11;
  direction = 0;
}

void
print_update (int v, double secs, const char * total)
{
  char template[12] = "-------++!!", * rtime;

  if (v > 0) template[v] = '\0';
  else
    {
      template[0] = '0';
      template[1] = '\0';
    }

  rtime = totime (secs);
  fprintf (stdout, "\rTime: %s of %s VU %-11s", rtime, total, template);
  fflush (stdout);
  ossplay_free (rtime);
}

void
print_record_update (int v, double secs, const char * fname, int update)
{
  char template[12] = "-------++!!";

  int x1, x2, i;
  extern int level_meters;

  fprintf (stderr, "\r%s [", fname);
  x1 = dots;
  x2 = dots + 10;

  if (update)
    {
      if (direction == 0)
        {
          dots++;
          if (dots >= 10) direction = 1;
        }
      else
        {
          dots--;
          if (dots <= -10) direction = 0;
        }
    }
 
  if (dots < 0)
    {
      x1 = 0;
      x2 = dots + 10;
      if (x2 < 0) x2 = 0;
    }
  if (dots >= 0)
    {
      x2 = 10;
      x1 = dots;
    }

  for (i = 0; i < x1; i++)
    fprintf (stderr, " ");
  for (i = x1; i < x2; i++)
    fprintf (stderr, ".");
  for (i = 0; i < 10 - x2; i++)
    fprintf (stderr, " ");

  if (secs < 60.0)
    fprintf (stderr, "] %1.2f secs", secs);
  else
    {
      int hours, mins;

      mins = (int) (secs / 60.0);
      secs -= (mins * 60);

      hours = mins / 60;
      mins = mins % 60;
      fprintf (stderr, "] %02d:%02d:%02.2f", hours, mins, secs);
    }

  if (!level_meters)
    {
      return;
    }
  else if (v > 0)
    {
      template[v] = '\0';
      fprintf (stderr, " VU %-11s", template);
    }
  else
    {
      fprintf (stderr, " VU %-11s", "0");
    }

  fflush (stderr);
}

void print_msg (prtype_t type, const char * fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  switch (type)
    {
      case NOTIFYM:
        if (quiet) break;
      case WARNM:
        if (quiet == 2) break;
      case ERRORM:
        vfprintf (stderr, fmt, ap);
        break;
      case HELPM:
        vfprintf (stdout, fmt, ap);
        break;
      case VERBOSEM:
        if (verbose) vfprintf (normalout, fmt, ap);
        break;
      default: /* case NORMALM, STARTM, CONTM, ENDM: */
        if (!quiet) vfprintf (normalout, fmt, ap);
        break;
    }
  va_end (ap);
}

void *
ossplay_malloc (size_t sz)
{
  void *ptr;

  if (sz == 0) return NULL;
  ptr = malloc (sz);
  if (ptr == NULL)
    {
      /* Not all libcs support using %z for size_t */
      fprintf (stderr, "Can't allocate %lu bytes\n", (unsigned long)sz);
      exit (-1);
    }
  return ptr;
}

void
ossplay_free (void * ptr)
{
  if (ptr == NULL) return;
  free (ptr);
}

char *
ossplay_strdup (const char * s)
{
  char * p;

  if (s == NULL) return NULL;
  p = strdup (s);
  if (p == NULL)
    {
      fprintf (stderr, "Can't allocate memory for strdup\n");
      exit (-1);
    }
  return p;
}

static int
ossplay_main (int argc, char ** argv)
{
  int i;
  dspdev_t dsp = { 0 };

  normalout = stdout;

  i = ossplay_parse_opts (argc, argv, &dsp);

  argc -= i - 1;
  argv += i - 1;

  dsp.flags = O_WRONLY;
  open_device (&dsp);
  if (dsp.playtgt != NULL) select_playtgt (&dsp);

  do for (i = 1; i < argc; i++)
    {
      strncpy (dsp.current_songname, filepart (argv[i]),
               sizeof (dsp.current_songname));
      dsp.current_songname[sizeof (dsp.current_songname) - 1] = 0;
      from_stdin = !strcmp (argv[i], "-");
      play_file (&dsp, argv[i]);
      eflag = 0;
    }
  while (loop);

  return exitstatus;
}

static int
ossrecord_main (int argc, char ** argv)
{
  int err, i, oind;
  dspdev_t dsp = { 0 };
  char current_filename[512];

  extern int force_fmt, force_channels, force_speed, nfiles;
  extern unsigned int datalimit;
  extern fctypes_t type;
  extern char script[512];

  normalout = stderr;
  /* Since recording can be redirected to stdout, we always output to stderr */

  oind = ossrecord_parse_opts (argc, argv, &dsp);

  dsp.flags = O_RDONLY;
  open_device (&dsp);
  if (dsp.recsrc != NULL) select_recsrc (&dsp);

  strncpy (dsp.current_songname, filepart (argv[oind]),
           sizeof (dsp.current_songname));
  dsp.current_songname[sizeof (dsp.current_songname) - 1] = 0;

  for (i = 0; i < nfiles; i++)
    {
      if (nfiles > 1)
        sprintf (current_filename, argv[oind], i + 1);
      else
        snprintf (current_filename, sizeof (current_filename),
                  "%s", argv[oind]);
      err = encode_sound (&dsp, type, current_filename, force_fmt,
                          force_channels, force_speed, datalimit);
      clear_update ();
      if (*script)
        {
          if (fork () == 0)
            {
              if (execlp (script, script, current_filename, (char *)NULL) == -1)
                {
                  perror (script);
                  exit (-1);
                }
            }

          print_msg (NORMALM,
                     "Waiting for the '%s' script(s) to finish - please stand"
                     " by\n", script);
          while (wait (NULL) != -1);
        }

      if (err) return err;
    }

  return 0;
}

char *
totime (double secs)
{
  char time[20];
  unsigned long min = secs / 60;

  snprintf (time, 20, "%.2lu:%05.2f", min, secs - min * 60);

  return ossplay_strdup (time);
}

int
main (int argc, char **argv)
{
  if (strstr (filepart (argv[0]), "ossplay")) return ossplay_main (argc, argv);
  return ossrecord_main (argc, argv);;
}
