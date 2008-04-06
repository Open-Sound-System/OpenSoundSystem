/*
 * Purpose: Console output interface functions and related.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2008. All rights reserved.

extern int quiet, quitflag, exitstatus, loop;
extern char current_songname[64];

#include "ossplay.h"
#include "parser.h"

void perror_msg (const char * s)
{
  perror (s);
}

void print_msg (char type, const char * fmt, ...)
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
      case UPDATEM:
        fprintf (stdout, "\r");
        vfprintf (stdout, fmt, ap);
        fflush (stdout);
        break;
      case CLEARUPDATEM:
        fprintf (stdout, "\r\n");
        break;
      case HELPM:
        vfprintf (stdout, fmt, ap);
        break;
      default: /* case NORMALM, STARTM, CONTM, ENDM: */
        if (!quiet) vfprintf (stdout, fmt, ap);
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
  free (ptr);
}

char *
ossplay_strdup (const char * s)
{
  return strdup (s);
}

int
main (int argc, char **argv)
{
  int i;

  i = parse_opts (argc, argv);

  argc -= i - 1;
  argv += i - 1;

  do for (i = 1; i < argc; i++)
    {
      strncpy (current_songname, filepart (argv[i]), sizeof (current_songname));
      current_songname[sizeof (current_songname) - 1] = 0;
      play_file (argv[i]);
      quitflag = 0;
    }
  while (loop);

  return exitstatus;
}
