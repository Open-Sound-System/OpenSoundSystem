/*
 * Purpose: Legacy sound device management utility
 *
 * Description:
 * Device file naming scheme was changed in OSS 4.0. This utility is used
 * to create old style "legacy" device files such as /dev/dsp0 to the
 * corresponding new stype name (such as /dev/sblive0_pcm0).
 *
 * By default the currently existing device links will be preserved. Legacy
 * devices for newly installed devices will be allocated after the
 * previously available devices.
 *
 * Commad line options:
 * -v      Produce verbose output.
 * -r      Remove all pre-existing legacy devices and reset the device
 *         numbering (not recommended).
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "oss_config.h"

#define MAXDEV	HARD_MAX_AUDIO_DEVFILES

oss_sysinfo si;
int mixerfd = -1;
int verbose = 0;

static int recreate_all = 0;

/*
 *****************************
 * /dev/dsp handling
 */

static int
find_dsplink (oss_audioinfo * ai)
{
  int dev;
/*
 * Look for a legacy (/dev/dsp#) devife file that is a symlink to
 * ai->devnode. Return the device number if a
 * matching link is found. Return -1 if nothing is found.
 */

  struct stat st;
  char devname[64], linkdev[256];

  for (dev = 0; dev < MAXDEV; dev++)
    {
      sprintf (devname, "/dev/dsp%d", dev);
      if (lstat (devname, &st) != -1)
	if (S_ISLNK (st.st_mode))
	  {
	    int l;

	    if ((l = readlink (devname, linkdev, sizeof (linkdev) - 1)) == -1)
	      {
		continue;
	      }
	    else
	      {
		linkdev[l] = 0;
		if (strcmp (linkdev, ai->devnode) == 0)	/* Match */
		  {
		    ai->legacy_device = dev;
		    return dev;
		  }
	      }
	  }
    }

  return -1;			/* Nothing found */
}

static void
create_dsplinks (void)
{
  int dev, newdev, numfiles = 0, legacy_number = 0;
  struct stat st;
  char devname[64];

  oss_audioinfo *ai, *audiodevs[MAXDEV];

  oss_renumber_t renum = { 0 };

  if (recreate_all)
    system ("rm -f /dev/dsp[0-9]* /dev/dsp_*");

  printf ("%d audio devices\n", si.numaudios);

  if (si.numaudios < 1)
    return;

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = malloc (sizeof (*ai));

      ai->dev = dev;

      if (ioctl (mixerfd, SNDCTL_AUDIOINFO, ai) == -1)
	{
	  perror ("SNDCTL_AUDIOINFO");
	  exit (-1);
	}

      audiodevs[dev] = ai;

      /* printf ("Adev %d = %s\n", dev, ai->devnode); */
    }

  for (dev = 0; dev < MAXDEV; dev++)
    {

      sprintf (devname, "/dev/dsp%d", dev);
      newdev = dev;

      if (lstat (devname, &st) != -1)
	{
	  if (S_ISLNK (st.st_mode))
	    numfiles = dev + 1;
	}
    }

  printf ("/dev/dsp%d is the next free legacy device\n", numfiles);

  for (dev = 0; dev < si.numaudios; dev++)
    {
      int recreate = 0;
#ifdef VDEV_SUPPORT
      if (audiodevs[dev]->caps & PCM_CAP_HIDDEN)	/* Ignore hidden devices */
	{
	  audiodevs[dev]->legacy_device = -1;
	  continue;
	}
#endif

      newdev = legacy_number++;
      sprintf (devname, "/dev/dsp%d", newdev);

      if (lstat (devname, &st) == -1)
	{
	  printf ("%s: %s\n", devname, strerror (errno));
	  recreate = 1;
	}
      else
	{
	  if (S_ISCHR (st.st_mode))
	    {
	      printf ("%s: character device\n", devname);
	      recreate = 1;
	    }
	  else if (S_ISLNK (st.st_mode))
	    {
	      int l;

	      char linkdev[256];
	      if ((l =
		   readlink (devname, linkdev, sizeof (linkdev) - 1)) == -1)
		{
		  perror ("readlink");
		  strcpy (linkdev, "Invalid");
		}
	      else
		{
		  linkdev[l] = 0;
		}

	      printf ("%s: symlink -> %s ", devname, linkdev);

	      if (strcmp (linkdev, audiodevs[dev]->devnode) != 0)
		{
		  printf ("(should be %s)\n", audiodevs[dev]->devnode);
		  if ((newdev = find_dsplink (audiodevs[dev])) == -1)
		    {
		      recreate = 1;
		      newdev = numfiles++;
		    }
		  else
		    printf ("\tAlready linked to /dev/dsp%d\n", newdev);
		}
	      else
		printf ("OK\n");
	    }
	  else
	    {
	      printf ("%s: unknown file type\n", devname);
	      recreate = 1;
	    }

	}

      if (recreate)
	{
	  audiodevs[dev]->legacy_device = newdev;
	  sprintf (devname, "/dev/dsp%d", newdev);

	  if (strcmp (audiodevs[dev]->devnode, devname) != 0)	/* Not the same */
	    {
	      unlink (devname);
	      if (symlink (audiodevs[dev]->devnode, devname) == -1)
		{
		  perror ("symlink");
		  fprintf (stderr, "Cannot create link %s->%s\n", devname,
			   audiodevs[dev]->devnode);
		  exit (-1);
		}

	      printf ("Created new legacy device %s -> %s\n", devname,
		      audiodevs[dev]->devnode);
	      audiodevs[dev]->legacy_device = newdev;
	    }
	}
    }

  printf ("%d legacy dsp device files\n", numfiles);

  renum.n = si.numaudios;

  for (dev = 0; dev < si.numaudios; dev++)
    {
      if (audiodevs[dev]->legacy_device != dev)
	if (audiodevs[dev]->legacy_device >= 0)
	  printf ("Adev %d (%s) is legacy device file /dev/dsp%d\n", dev,
		  audiodevs[dev]->devnode, audiodevs[dev]->legacy_device);

      renum.map[dev] = audiodevs[dev]->legacy_device;
    }

  if (ioctl (mixerfd, OSSCTL_RENUM_AUDIODEVS, &renum) == -1)
    {
      perror ("audio_renum");
    }


/*
 * Find out a suitable /dev/dsp device (input and output capable).
 * Remove old /dev/dsp if it appears to be a character device node.
 */
  if (lstat ("/dev/dsp", &st) != -1)
    if (S_ISCHR (st.st_mode))
      unlink ("/dev/dsp");

/*
 * Remove /dev/dsp link that points to some bogus device. This may
 * happen if some hot-pluggable (USB) device has been
 * removed from the system.
 */
  if (lstat ("/dev/dsp", &st) != -1)	/* /dev/dsp exists (and is symlink) */
    if (stat ("/dev/dsp", &st) == -1)	/* But points to nowhere */
      unlink ("/dev/dsp");

/*
 * Next find a duplex capable audio device.
 */

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = audiodevs[dev];

      if (!(ai->caps & PCM_CAP_OUTPUT))
	continue;

      if (!(ai->caps & PCM_CAP_INPUT))
	continue;

      printf ("%s is the default /dev/dsp device\n", ai->devnode);
      symlink (ai->devnode, "/dev/dsp");	/* Ignore errors */
      break;
    }

/*
 * Find out a suitable /dev/dsp_ac3 output device.
 */

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = audiodevs[dev];

      if (!(ai->caps & PCM_CAP_OUTPUT))
	continue;

      if (!(ai->oformats & AFMT_AC3))
	continue;

      printf ("%s is the default AC3 output device\n", ai->devnode);
      symlink (ai->devnode, "/dev/dsp_ac3");	/* Ignore errors */
      break;
    }

/*
 * Find out a suitable /dev/dsp_mmap output device.
 */

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = audiodevs[dev];

      if (!(ai->caps & PCM_CAP_OUTPUT))
	continue;

      if (!(ai->caps & PCM_CAP_MMAP))
	continue;

      if (!(ai->caps & PCM_CAP_TRIGGER))
	continue;

      if (ai->max_channels < 2)
	continue;

      if (ai->min_channels > 2)
	continue;

      printf ("%s is the default mmap output device\n", ai->devnode);
      symlink (ai->devnode, "/dev/dsp_mmap");	/* Ignore errors */
      break;
    }

/*
 * Find out a suitable /dev/dsp_multich output device.
 */

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = audiodevs[dev];

      if (!(ai->caps & PCM_CAP_OUTPUT))
	continue;

      if (ai->max_channels < 4)
	continue;

      printf ("%s is the default multichan output device\n", ai->devnode);
      symlink (ai->devnode, "/dev/dsp_multich");	/* Ignore errors */
      break;
    }

/*
 * Find out a suitable /dev/dsp_spdifout output device.
 */

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = audiodevs[dev];

      if (!(ai->caps & PCM_CAP_OUTPUT))
	continue;

      if (!(ai->caps & PCM_CAP_DIGITALOUT))
	continue;

      printf ("%s is the default S/PDIF digital output device\n",
	      ai->devnode);
      symlink (ai->devnode, "/dev/dsp_spdifout");	/* Ignore errors */
      break;
    }

/*
 * Find out a suitable /dev/dsp_spdifin input device.
 */

  for (dev = 0; dev < si.numaudios; dev++)
    {
      ai = audiodevs[dev];

      if (!(ai->caps & PCM_CAP_INPUT))
	continue;

      if (!(ai->caps & PCM_CAP_DIGITALIN))
	continue;

      printf ("%s is the default S/PDIF digital input device\n", ai->devnode);
      symlink (ai->devnode, "/dev/dsp_spdifin");	/* Ignore errors */
      break;
    }
}

/*
 *****************************
 * /dev/mixer handling
 */

static int
find_mixerlink (oss_mixerinfo * xi)
{
  int dev;
/*
 * Look for a legacy (/dev/mixer#) devife file that is a symlink to
 * xi->devnode. Return the device number if a
 * matching link is found. Return -1 if nothing is found.
 */

  struct stat st;
  char devname[64], linkdev[256];

  for (dev = 0; dev < MAXDEV; dev++)
    {
      sprintf (devname, "/dev/mixer%d", dev);
      if (lstat (devname, &st) != -1)
	if (S_ISLNK (st.st_mode))
	  {
	    int l;

	    if ((l = readlink (devname, linkdev, sizeof (linkdev) - 1)) == -1)
	      {
		continue;
	      }
	    else
	      {
		linkdev[l] = 0;
		if (strcmp (linkdev, xi->devnode) == 0)	/* Match */
		  {
		    xi->legacy_device = dev;
		    return dev;
		  }
	      }
	  }
    }

  return -1;			/* Nothing found */
}

static void
create_mixerlinks (void)
{
  int dev, newdev, numfiles = 0;
  struct stat st;
  char devname[64];

  oss_mixerinfo *xi, *mixerdevs[MAXDEV];

  oss_renumber_t renum = { 0 };

  if (recreate_all)
    system ("rm -f /dev/mixer[0-9]*");

  printf ("%d mixer devices\n", si.nummixers);

  if (si.nummixers < 1)
    return;

  for (dev = 0; dev < si.nummixers; dev++)
    {
      xi = malloc (sizeof (*xi));

      xi->dev = dev;

      if (ioctl (mixerfd, SNDCTL_MIXERINFO, xi) == -1)
	{
	  perror ("SNDCTL_MIXERINFO");
	  exit (-1);
	}

      mixerdevs[dev] = xi;

      /* printf ("Mixdev %d = %s\n", dev, xi->devnode); */
    }

  for (dev = 0; dev < MAXDEV; dev++)
    {

      sprintf (devname, "/dev/mixer%d", dev);
      newdev = dev;

      if (lstat (devname, &st) != -1)
	{
	  if (S_ISLNK (st.st_mode))
	    numfiles = dev + 1;
	}
    }

  if (numfiles < si.nummixers)
    numfiles = si.nummixers;

  printf ("/dev/mixer%d is the next free legacy device\n", numfiles);

  for (dev = 0; dev < si.nummixers; dev++)
    {
      int recreate = 0;

      sprintf (devname, "/dev/mixer%d", dev);
      newdev = dev;

      if (lstat (devname, &st) == -1)
	{
	  printf ("%s: %s\n", devname, strerror (errno));
	  recreate = 1;
	}
      else
	{
	  if (S_ISCHR (st.st_mode))
	    {
	      printf ("%s: character device\n", devname);
	      recreate = 1;
	    }
	  else if (S_ISLNK (st.st_mode))
	    {
	      int l;

	      char linkdev[256];
	      if ((l =
		   readlink (devname, linkdev, sizeof (linkdev) - 1)) == -1)
		{
		  perror ("readlink");
		  strcpy (linkdev, "Invalid");
		}
	      else
		{
		  linkdev[l] = 0;
		}

	      printf ("%s: symlink -> %s ", devname, linkdev);

	      if (strcmp (linkdev, mixerdevs[dev]->devnode) != 0)
		{
		  printf ("(should be %s)\n", mixerdevs[dev]->devnode);
		  if ((newdev = find_mixerlink (mixerdevs[dev])) == -1)
		    {
		      recreate = 1;
		      newdev = numfiles++;
		    }
		  else
		    printf ("\tAlready linked to /dev/mixer%d\n", newdev);
		}
	      else
		printf ("OK\n");
	    }
	  else
	    {
	      printf ("%s: unknown file type\n", devname);
	      recreate = 1;
	    }

	}

      if (recreate)
	{
	  mixerdevs[dev]->legacy_device = newdev;
	  sprintf (devname, "/dev/mixer%d", newdev);

	  if (strcmp (mixerdevs[dev]->devnode, devname) != 0)	/* Not the same */
	    {
	      unlink (devname);
	      if (symlink (mixerdevs[dev]->devnode, devname) == -1)
		{
		  perror ("symlink");
		  fprintf (stderr, "Cannot create link %s->%s\n", devname,
			   mixerdevs[dev]->devnode);
		  exit (-1);
		}

	      printf ("Created new legacy device %s -> %s\n", devname,
		      mixerdevs[dev]->devnode);
	    }
	}
    }

  printf ("%d legacy mixer device files\n", numfiles);

  renum.n = si.nummixers;

  for (dev = 0; dev < si.nummixers; dev++)
    {
      if (mixerdevs[dev]->legacy_device != dev)
	printf ("Mixdev %d is legacy device file /dev/mixer%d\n", dev,
		mixerdevs[dev]->legacy_device);
      renum.map[dev] = mixerdevs[dev]->legacy_device;
    }

  if (ioctl (mixerfd, OSSCTL_RENUM_MIXERDEVS, &renum) == -1)
    {
      perror ("mixer_renum");
    }
}

/*
 * MIDI devices (/dev/midiNN)
 */

static int
find_midilink (oss_midi_info * xi)
{
  int dev;
/*
 * Look for a legacy (/dev/midi#) devife file that is a symlink to
 * xi->devnode. Return the device number if a
 * matching link is found. Return -1 if nothing is found.
 */

  struct stat st;
  char devname[64], linkdev[256];

  for (dev = 0; dev < MAXDEV; dev++)
    {
      sprintf (devname, "/dev/midi%d", dev);
      if (lstat (devname, &st) != -1)
	if (S_ISLNK (st.st_mode))
	  {
	    int l;

	    if ((l = readlink (devname, linkdev, sizeof (linkdev) - 1)) == -1)
	      {
		continue;
	      }
	    else
	      {
		linkdev[l] = 0;
		if (strcmp (linkdev, xi->devnode) == 0)	/* Match */
		  {
		    xi->legacy_device = dev;
		    return dev;
		  }
	      }
	  }
    }

  return -1;			/* Nothing found */
}

static void
create_midilinks (void)
{
  int dev, newdev, numfiles = 0;
  struct stat st;
  char devname[64];

  oss_midi_info *xi, *mididevs[MAXDEV];

  oss_renumber_t renum = { 0 };

  if (recreate_all)
    system ("rm -f /dev/midi[0-9]*");

  if (si.nummidis < 1)		/* No MIDI devices in the system */
    return;

  printf ("%d midi devices\n", si.nummidis);
  for (dev = 0; dev < si.nummidis; dev++)
    {
      xi = malloc (sizeof (*xi));

      xi->dev = dev;

      if (ioctl (mixerfd, SNDCTL_MIDIINFO, xi) == -1)
	{
	  perror ("SNDCTL_MIDIINFO");
	  exit (-1);
	}

      mididevs[dev] = xi;

      /* printf ("Mididev %d = %s\n", dev, xi->devnode); */
    }

  for (dev = 0; dev < MAXDEV; dev++)
    {

      sprintf (devname, "/dev/midi%d", dev);
      newdev = dev;

      if (lstat (devname, &st) != -1)
	{
	  if (S_ISLNK (st.st_mode))
	    numfiles = dev + 1;
	}
    }

  if (numfiles < si.nummidis)
    numfiles = si.nummidis;

  printf ("/dev/midi%d is the next free legacy device\n", numfiles);

  for (dev = 0; dev < si.nummidis; dev++)
    {
      int recreate = 0;

      sprintf (devname, "/dev/midi%d", dev);
      newdev = dev;

      if (lstat (devname, &st) == -1)
	{
	  printf ("%s: %s\n", devname, strerror (errno));
	  recreate = 1;
	}
      else
	{
	  if (S_ISCHR (st.st_mode))
	    {
	      printf ("%s: character device\n", devname);
	      recreate = 1;
	    }
	  else if (S_ISLNK (st.st_mode))
	    {
	      int l;

	      char linkdev[256];
	      if ((l =
		   readlink (devname, linkdev, sizeof (linkdev) - 1)) == -1)
		{
		  perror ("readlink");
		  strcpy (linkdev, "Invalid");
		}
	      else
		{
		  linkdev[l] = 0;
		}

	      printf ("%s: symlink -> %s ", devname, linkdev);

	      if (strcmp (linkdev, mididevs[dev]->devnode) != 0)
		{
		  printf ("(should be %s)\n", mididevs[dev]->devnode);
		  if ((newdev = find_midilink (mididevs[dev])) == -1)
		    {
		      recreate = 1;
		      newdev = numfiles++;
		    }
		  else
		    printf ("\tAlready linked to /dev/midi%d\n", newdev);
		}
	      else
		printf ("OK\n");
	    }
	  else
	    {
	      printf ("%s: unknown file type\n", devname);
	      recreate = 1;
	    }

	}

      if (recreate)
	{
	  mididevs[dev]->legacy_device = newdev;
	  sprintf (devname, "/dev/midi%d", newdev);

	  if (strcmp (mididevs[dev]->devnode, devname) != 0)	/* Not the same */
	    {
	      unlink (devname);
	      if (symlink (mididevs[dev]->devnode, devname) == -1)
		{
		  perror ("symlink");
		  fprintf (stderr, "Cannot create link %s->%s\n", devname,
			   mididevs[dev]->devnode);
		  exit (-1);
		}

	      printf ("Created new legacy device %s -> %s\n", devname,
		      mididevs[dev]->devnode);
	    }
	}
    }

  printf ("%d legacy MIDI device files\n", numfiles);

  renum.n = si.nummidis;

  for (dev = 0; dev < si.nummidis; dev++)
    {
      if (mididevs[dev]->legacy_device != dev)
	printf ("Mididev %d is legacy device file /dev/midi%d\n", dev,
		mididevs[dev]->legacy_device);
      renum.map[dev] = mididevs[dev]->legacy_device;
    }

  if (ioctl (mixerfd, OSSCTL_RENUM_MIDIDEVS, &renum) == -1)
    {
      perror ("midi_renum");
    }
}

static void
save_link (FILE * f, char *devname)
{
  char linkname[256];
  int l;

  if ((l = readlink (devname, linkname, sizeof (linkname) - 1)) == -1)
    return;
  linkname[l] = 0;

  fprintf (f, "rm -f %s;ln -sf %s %s\n", devname, linkname, devname);
}

static void
save_links (void)
{
  FILE *f;
  int i;
  char devfile[32];

#if defined(sun)
#define LEGACYDEV_FILE "/etc/oss/legacy_devices"
#else
#define LEGACYDEV_FILE "/usr/lib/oss/etc/legacy_devices"
#endif

  if ((f = fopen (LEGACYDEV_FILE, "w")) == NULL)
    {
      perror (LEGACYDEV_FILE);
      return;
    }

/*
 * /dev/dsp#
 */

  for (i = 0; i < MAXDEV; i++)
    {
      sprintf (devfile, "/dev/dsp%d", i);
      save_link (f, devfile);
    }

  save_link (f, "/dev/dsp_ac3");
  save_link (f, "/dev/dsp_mmap");
  save_link (f, "/dev/dsp_multich");
  save_link (f, "/dev/dsp_spdifout");
  save_link (f, "/dev/dsp_spdifin");

/*
 * /dev/mixer#
 */

  for (i = 0; i < MAXDEV; i++)
    {
      sprintf (devfile, "/dev/mixer%d", i);
      save_link (f, devfile);
    }

/*
 * /dev/midi##
 */

  for (i = 0; i < MAXDEV; i++)
    {
      sprintf (devfile, "/dev/midi%02d", i);
      save_link (f, devfile);
    }

  fclose (f);
}

int
main (int argc, char *argv[])
{
  int i,c;

  if ((mixerfd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      perror ("/dev/mixer");
      exit (-1);
    }

  if (ioctl (mixerfd, SNDCTL_SYSINFO, &si) == -1)
    {
      if (errno == ENXIO)
	{
	  fprintf (stderr,
		   "OSS has not detected any supported sound hardware ");
	  fprintf (stderr, "in your system.\n");
	  exit (-1);
	}
      else
	{
	  perror ("SNDCTL_SYSINFO");
	  if (errno == EINVAL)
	    fprintf (stderr, "Error: OSS version 4.0 or later is required\n");
	  exit (-1);
	}
    }

  while ((c = getopt (argc, argv, "vr")) != EOF)
  switch (c)
    {
    case 'r':
      recreate_all = 1;
      break;

    case 'v':
      verbose++;
      break;
    }

  if (verbose < 1)
    freopen ("/dev/null", "w", stdout);

  create_dsplinks ();
  create_mixerlinks ();
  create_midilinks ();

  close (mixerfd);

  save_links ();

  return 0;
}
