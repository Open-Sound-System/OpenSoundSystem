/*
 * Purpose: A program that demonstrates use of syncronization groups.
 * Copyright (C) 4Front Technologies, 2002-2004. Released under GPLv2/CDDL.
 *
 * Description:
 * This program opens three audio devices (hard coded in the program)
 * and creates a syncronization group using {!nlink SNDCTL_DSP_SYNCGROUP}.
 * 
 * Next it starts all the devices joined in the group simultaneously
 * by calling {!nlink SNDCTL_DSP_SYNCSTART}. Finally it will keep copying
 * audio input from the 3rd device to the other two ones.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#define MAX_DEV		10

int
main (int argc, char *argv[])
{
  int i, id, fd[MAX_DEV], ndevs = 0;
  char buf[32768] = { 0 };

  oss_syncgroup group;

  group.id = 0;
  group.mode = PCM_ENABLE_OUTPUT;

/*
 * Open the devices listed on command line
 */
  if (argc < 2)
    exit (-1);

  for (i = 1; i < argc; i++)
    {
      if ((fd[ndevs] = open (argv[i], O_WRONLY, 0)) == -1)
	{
	  perror (argv[i]);
	  exit (-1);
	}

      if (ioctl (fd[ndevs], SNDCTL_DSP_SYNCGROUP, &group) == -1)
	{
	  perror ("SNDCTL_DSP_SYNCGROUP");
	  exit (-1);
	}

      if (write (fd[ndevs], buf, sizeof (buf)) != sizeof (buf))
	{
	  perror ("write");
	  exit (-1);
	}

      ndevs++;
    }

  printf ("Sync group %x created with %d devices\n", group.id, ndevs);


  id = group.id;

  if (ioctl (fd[0], SNDCTL_DSP_SYNCSTART, &id) == -1)
    {
      perror ("SNDCTL_DSP_SYNCSTART");
      exit (-1);
    }

  while (1)
    {
      for (i = 0; i < ndevs; i++)
	if (write (fd[i], buf, sizeof (buf)) != sizeof (buf))
	  {
	    perror ("write2");
	    exit (-1);
	  }
    }

  exit (0);
}
