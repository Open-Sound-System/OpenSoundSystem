#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

int section = 0;
char *volume = "Unknown volume";
char *title = "unknown";

int
main (int argc, char *argv[])
{
  char line[1024], *s;
  int upper;
  FILE *f;
  char date[32] = "August 31, 2006";

  extern char *optarg;
  extern int optind;
  int c;

  while ((c = getopt (argc, argv, "v:s:t:")) != EOF)
    switch (c)
      {
      case 'v':
	volume = optarg;
	break;
      case 't':
	title = optarg;
	break;
      case 's':
	section = atoi (optarg);
	break;
      }

  if (optind >= argc)
    {
      fprintf (stderr, "%s: No input file specified\n", argv[0]);
      exit (-1);
    }

  if ((f = fopen (argv[optind], "r")) == NULL)
    {
      perror (argv[optind]);
      exit (-1);
    }

  printf (".\" Automatically generated text\n");
  printf (".TH %d \"%s\" \"OSS\" \"%s\"\n", section, date, volume);

  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      s = line;
      upper = 1;

      while (*s && *s != '\n')
	{
	  if (!isupper (*s) && *s != ' ')
	    upper = 0;
	  s++;
	}
      *s = 0;
      if (line[0] == 0)
	upper = 0;

      if (upper)
	printf (".SH %s\n", line);
      else
	{
	  s = line;

	  if (*s == 'o' && s[1] == ' ')
	    {
	      printf (".IP \\(bu 3\n");
	      s += 2;
	      printf ("%s\n", s);
	      continue;
	    }
	  if (*s == ' ')
	    s++;
	  printf ("%s\n", s);
	}
    }

  fclose (f);

  exit (0);
}
