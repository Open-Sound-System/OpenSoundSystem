/*
 * Purpose: Sources for the ossmix command line mixer shipped with OSS
 *
 * Description:
 * The {!xlink ossmix}  program was originally developed as a test bed
 * program for the new mixer API. However it has been included in the
 * oss package because there is need for a command line mixer.
 *
 * Due to the history ofg this utility it's probably not the most
 * clean one to be used as an sample program. The {!nlink mixext.c}
 * test program is must smaller and easier to read than this.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <soundcard.h>
#include <sys/ioctl.h>

static char *progname = NULL;
static int mixerfd = -1;
static int quiet = 0;

oss_mixext *extrec;
oss_mixext_root *root;
int nrext = 0;

void
usage (void)
{
  printf ("Usage: %s -h		Displays help (this screen)\n", progname);
  printf ("Usage: %s [-d<devno>] [arguments]\n", progname);
  printf ("arguments:\n");
  printf ("\t-D			Display device information\n");
  printf ("\t-c			Dump mixer settings for all mixers\n");
  printf ("\tctrl# value		Change value of a mixer control\n");
  printf ("\t<no arguments>	Display current/possible settings\n");

#if 0
  fprintf (stderr,
	   "\nNOTE! OSS VERSION 4.0 OR LATER IS REQUIRED WITH THIS PROGRAM\n");
#endif
  exit (-1);
}

void
load_devinfo (int dev)
{
  int i, n;
  oss_mixext *thisrec;

  n = dev;

  if (ioctl (mixerfd, SNDCTL_MIX_NREXT, &n) == -1)
    {
      switch (errno)
	{
	case EINVAL:
	  fprintf (stderr, "Error: OSS version 3.9 or later is required\n");
	  break;

	case ENODEV:
	  fprintf (stderr, "Open Sound System is not loaded\n");
	  break;

	case ENXIO:
	  fprintf (stderr, "Mixer device %d doesn't exist.\n", dev);
	  break;

	default:
	  perror ("SNDCTL_MIX_NREXT");
	}
      exit (-1);
    }

  if (n < 1)
    {
      fprintf (stderr, "Mixer device %d has no functionality\n", dev);
      exit (-1);
    }

  if ((extrec = malloc ((n + 1) * sizeof (oss_mixext))) == NULL)
    {
      fprintf (stderr, "malloc of %d entries failed\n", n);
      exit (-1);
    }

  nrext = n;
  for (i = 0; i < n; i++)
    {
      thisrec = &extrec[i];
      thisrec->dev = dev;
      thisrec->ctrl = i;

      if (ioctl (mixerfd, SNDCTL_MIX_EXTINFO, thisrec) == -1)
	{
	  if (errno == EINVAL)
	    {
	      fprintf (stderr, "Incompatible OSS version\n");
	      exit (-1);
	    }
	  perror ("SNDCTL_MIX_EXTINFO");
	  exit (-1);
	}

      if (thisrec->type == MIXT_DEVROOT)
	root = (oss_mixext_root *) thisrec->data;
    }
}

void
verbose_devinfo (int dev)
{
  int i;
  oss_mixext *thisrec;

  for (i = 0; i < nrext; i++)
    {
      oss_mixer_value val;

      thisrec = &extrec[i];
      printf ("%2d: ", i);

      switch (thisrec->type)
	{
	case MIXT_DEVROOT:
	  printf ("\nDevice root '%s' / %s\n", root->id, root->name);
	  break;

	case MIXT_GROUP:
	  printf ("Group: '%s', parent=%d\n", thisrec->id, thisrec->parent);
	  break;

	case MIXT_STEREOSLIDER:
	case MIXT_STEREODB:
	  printf ("Stereo slider: '%s' (%s), parent=%d, max=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_STEREOSLIDER16:
	  printf ("Stereo slider: '%s' (%s), parent=%d, max=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%08x\n", val.value);
	  break;

	case MIXT_3D:
	  printf ("3D control: '%s' (%s), parent=%d, max=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%08x\n", val.value);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  printf ("Stereo peak meter: '%s' (%s), parent=%d, max=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_MONOSLIDER:
	case MIXT_MONOSLIDER16:
	case MIXT_SLIDER:
	case MIXT_MONODB:
	  printf ("Mono slider: '%s' (%s), parent=%d, max=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_MONOPEAK:
	  printf ("Mono peak meter: '%s' (%s), parent=%d, max=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(monopeak)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_ONOFF:
	  printf ("On/off switch: '%s' (%s), parent=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(onoff)");
	  printf ("  Current value=%x (%s)\n", val.value,
		  val.value ? "ON" : "OFF");
	  break;

	case MIXT_ENUM:
	  printf
	    ("Enumerated control: '%s' (%s), parent=%d, flags=%x, mask=%02x%02x",
	     thisrec->id, thisrec->extname, thisrec->parent, thisrec->flags,
	     thisrec->enum_present[1], thisrec->enum_present[0]);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(enum)");
	  printf ("  Current value=%x\n", val.value);
	  break;

	case MIXT_VALUE:
	  printf ("Decimal value: '%s' (%s), parent=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(value)");
	  printf ("  Current value=%d\n", val.value);
	  break;

	case MIXT_HEXVALUE:
	  printf ("Hexadecimal value: '%s' (%s), parent=%d, flags=%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->flags);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(hex)");
	  printf ("  Current value=%x\n", val.value);
	  break;

	case MIXT_MARKER:
	  printf ("******* Extension entries ********\n");
	  break;

	default:
	  printf ("Unknown record type %d\n", thisrec->type);
	}

    }
}

/*ARGSUSED*/
char *
showenum (char *extname, oss_mixext * rec, int val)
{
  static char tmp[512];
  oss_mixer_enuminfo ei;

  *tmp = 0;

  ei.dev = rec->dev;
  ei.ctrl = rec->ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_ENUMINFO, &ei) != -1)
    {
      if (val >= ei.nvalues)
	{
	  sprintf (tmp, "%d(too large (a=%d)?)", val, ei.nvalues);
	  return tmp;
	}

      strcpy (tmp, ei.strings + ei.strindex[val]);

      return tmp;
    }

  if (val > rec->maxvalue)
    {
      sprintf (tmp, "%d(too large (b=%d)?)", val, rec->maxvalue);
      return tmp;
    }

  if (*tmp == 0)
    sprintf (tmp, "%d", val);
  return tmp;
}

/*ARGSUSED*/
char *
showchoices (char *extname, oss_mixext * rec)
{
  int i;
  static char tmp[4096], *s = tmp;
  oss_mixer_enuminfo ei;

  ei.dev = rec->dev;
  ei.ctrl = rec->ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_ENUMINFO, &ei) != -1)
    {
      int n = ei.nvalues;
      char *p;

      if (n > rec->maxvalue)
	n = rec->maxvalue;

      s = tmp;
      *s = 0;

      for (i = 0; i < rec->maxvalue; i++)
	if (rec->enum_present[i / 8] & (1 << (i % 8)))
	  {
	    p = ei.strings + ei.strindex[i];

	    if (s > tmp)
	      *s++ = '|';
	    s += sprintf (s, "%s", p);
	  }

      return tmp;
    }

#if 0
  perror ("SNDCTL_MIX_ENUMINFO");
  exit (-1);
#else
  *tmp = 0;
  s = tmp;
  for (i = 0; i < rec->maxvalue; i++)
    {
      if (i > 0)
	*s++ = ' ';
      s += sprintf (s, "%d", i);
    }
  return tmp;
#endif
}

/*ARGSUSED*/
int
findenum (char *extname, oss_mixext * rec, char *arg)
{
  int i, n;
  oss_mixer_enuminfo ei;

  ei.dev = rec->dev;
  ei.ctrl = rec->ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_ENUMINFO, &ei) != -1)
    {
      int n = ei.nvalues;
      char *p;

      if (n > rec->maxvalue)
	n = rec->maxvalue;

      for (i = 0; i < rec->maxvalue; i++)
	if (rec->enum_present[i / 8] & (1 << (i % 8)))
	  {
	    p = ei.strings + ei.strindex[i];
	    if (strcmp (p, arg) == 0)
	      return i;
	  }
    }

  if (sscanf (arg, "%d", &n) < 1 || n < 0)
    {
      fprintf (stderr, "Invalid enumerated value '%s'\n", arg);
      return 0;
    }

  return n;
}

void
show_devinfo (int dev)
{
  int i, vl, vr;
  oss_mixext *thisrec;

  printf ("Selected mixer %d/%s\n", dev, root->name);
  printf ("Known controls are:\n");
  for (i = 0; i < nrext; i++)
    {
      oss_mixer_value val;

      thisrec = &extrec[i];

#if 0
      if (thisrec->id[0] == '-')
	continue;
#endif

      switch (thisrec->type)
	{
	case MIXT_MARKER:
	case MIXT_DEVROOT:
	case MIXT_GROUP:
	case MIXT_MONOPEAK:
	  break;

	case MIXT_STEREOSLIDER:
	case MIXT_STEREODB:
	  printf ("%s <both/leftvol>[:<rightvol>]", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf (" (currently %d:%d)\n", val.value & 0xff,
		  (val.value >> 8) & 0xff);
	  break;

	case MIXT_STEREOSLIDER16:
	  printf ("%s <both/leftvol>[:<rightvol>]", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  if (thisrec->flags & MIXF_CENTIBEL)
	    {
	      vl = val.value & 0xffff;
	      vr = (val.value >> 16) & 0xffff;
	      printf (" (currently %d.%d:%d.%d dB)\n", vl / 10, vl % 10,
		      vr / 10, vr % 10);
	    }
	  else
	    printf (" (currently %d:%d)\n", val.value & 0xffff,
		    (val.value >> 16) & 0xffff);
	  break;

	case MIXT_3D:
	  printf ("%s <distance:vol:angle>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf (" (currently %d:%d:%d)\n", (val.value >> 8) & 0xff,
		  val.value & 0x00ff, (val.value >> 16) & 0xffff);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  printf ("%s <leftVU>:<rightVU>]", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf (" (currently %d:%d)\n", val.value & 0xff,
		  (val.value >> 8) & 0xff);
	  break;

	case MIXT_ENUM:
	  printf ("%s <%s>", extrec[i].extname,
		  showchoices (extrec[i].extname, thisrec));
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(enum2)");
	  printf (" (currently %s)\n",
		  showenum (extrec[i].extname, thisrec, val.value & 0xff));
	  break;

	case MIXT_MONOSLIDER:
	case MIXT_MONODB:
	  printf ("%s <monovol>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf (" (currently %d)\n", val.value & 0xff);
	  break;

	case MIXT_SLIDER:
	  printf ("%s <monovol>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf (" (currently %d)\n", val.value);
	  break;

	case MIXT_MONOSLIDER16:
	  printf ("%s <monovol>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  if (thisrec->flags & MIXF_CENTIBEL)
	    {
	      vl = val.value & 0xffff;
	      printf (" (currently %d.%d dB)\n", vl / 10, vl % 10);
	    }
	  else
	    printf (" (currently %d)\n", val.value & 0xffff);
	  break;

	case MIXT_MONOVU:
	  printf ("%s <monoVU>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf (" (currently %d)\n", val.value & 0xff);
	  break;

	case MIXT_VALUE:
	  printf ("%s <decimal value>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(value2)");
	  printf (" (currently %d)\n", val.value);
	  break;

	case MIXT_HEXVALUE:
	  printf ("%s <hexadecimal value>", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(hex2)");
	  printf (" (currently %x)\n", val.value);
	  break;

	case MIXT_ONOFF:
	  printf ("%s ON|OFF", extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(onoff)");
	  printf (" (currently %s)\n", val.value ? "ON" : "OFF");
	  break;

	default:
	  printf ("Unknown mixer extension type %d\n", thisrec->type);
	}

    }
}

void
dump_devinfo (int dev)
{
  int i, enabled = 0;
  oss_mixext *thisrec;
  char ossmix[256];

  sprintf (ossmix, "!ossmix -d%d", dev);

  for (i = 0; i < nrext; i++)
    {
      oss_mixer_value val;

      thisrec = &extrec[i];

      if (!enabled)
	{
	  if (thisrec->type == MIXT_MARKER)
	    enabled = 1;
	  continue;
	}

      if (thisrec->id[0] == '-')
	continue;

      if (!(thisrec->flags & MIXF_WRITEABLE))
	continue;

      switch (thisrec->type)
	{
	case MIXT_MARKER:
	case MIXT_DEVROOT:
	case MIXT_GROUP:
	case MIXT_MONOPEAK:
	  break;

	case MIXT_STEREOSLIDER:
	case MIXT_STEREODB:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf ("%d:%d\n", val.value & 0xff, (val.value >> 8) & 0xff);
	  break;

	case MIXT_STEREOSLIDER16:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf ("%d:%d\n", val.value & 0xffff, (val.value >> 16) & 0xffff);
	  break;

	case MIXT_3D:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(3D)");
	  printf ("%d:%d:%d\n",
		  (val.value >> 8) & 0x00ff,
		  val.value & 0x00ff, (val.value >> 16) & 0xffff);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  break;

	case MIXT_ENUM:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(enum2)");
	  printf ("%s\n",
		  showenum (extrec[i].extname, thisrec, val.value & 0xff));
	  break;

	case MIXT_MONOSLIDER:
	case MIXT_MONODB:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf ("%d\n", val.value & 0xff);
	  break;

	case MIXT_SLIDER:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf ("%d\n", val.value);
	  break;

	case MIXT_MONOSLIDER16:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf ("%d\n", val.value & 0xffff);
	  break;

	case MIXT_MONOVU:
	  break;

	case MIXT_VALUE:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(value2)");
	  printf ("%d\n", val.value);
	  break;

	case MIXT_HEXVALUE:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(hex2)");
	  printf ("%x\n", val.value);
	  break;

	case MIXT_ONOFF:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = thisrec->timestamp;
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(onoff)");
	  printf ("%s\n", val.value ? "ON" : "OFF");
	  break;

	default:
	  printf ("Unknown mixer extension type %d\n", thisrec->type);
	}

    }
}

int
find_name (char *name)
{
  int i;

  if (name == NULL)
    return -1;

  for (i = 0; i < nrext; i++)
    if (extrec[i].type != MIXT_DEVROOT &&
	extrec[i].type != MIXT_GROUP && extrec[i].type != MIXT_MARKER)
      if (extrec[i].extname != NULL)
	if (strncmp (extrec[i].extname, name, 60) == 0)
	  return i;

  return -1;
}

void
change_level (int dev, char *cname, char *arg)
{
  int ctrl, left = 0, right = 0;
  oss_mixer_value val;
  oss_mixext extrec;

  if ((ctrl = find_name (cname)) == -1)
    {
      fprintf (stderr, "Bad mixer control name(1) '%s'\n", cname);
      exit (1);
    }

  val.value = -1;

  extrec.dev = dev;
  extrec.ctrl = ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_EXTINFO, &extrec) == -1)
    {
      perror ("SNDCTL_MIX_EXTINFO");
      exit (-1);
    }

  if (!(extrec.flags & MIXF_WRITEABLE))
    {
      fprintf (stderr, "Control %s is write protected\n", cname);
      return;
    }

  if (extrec.type == MIXT_ENUM)
    {
      val.value = findenum (cname, &extrec, arg);
      right = 0;
    }
  else if (extrec.type == MIXT_HEXVALUE)
    {
      if (sscanf (arg, "%x", &val.value) != 1 || val.value < 0)
	val.value = 0;
    }
  else if (extrec.type == MIXT_VALUE)
    {
      if (sscanf (arg, "%d", &val.value) != 1 || val.value < 0)
	val.value = 0;
    }
  else if (extrec.type == MIXT_3D)
    {
      int dist;
      if (sscanf (arg, "%d:%d:%d", &dist, &left, &right) != 3)
	{
	  fprintf (stderr, "Bad 3D position '%s'\n", arg);
	  return;
	}
      val.value =
	(left & 0x00ff) | ((right & 0xffff) << 16) | ((dist & 0xff) << 8);
    }
  else if (strcmp (arg, "ON") == 0 || strcmp (arg, "on") == 0)
    left = 1;
  else if (strcmp (arg, "OFF") == 0 || strcmp (arg, "off") == 0)
    left = 0;
  else if (sscanf (arg, "%d:%d", &left, &right) != 2)
    {
      if (sscanf (arg, "%d", &left) != 1)
	{
	  fprintf (stderr, "Bad mixer level '%s'\n", arg);
	  exit (1);
	}
      else
	right = left;
    }

  if (extrec.type != MIXT_STEREOSLIDER && extrec.type != MIXT_STEREODB &&
      extrec.type != MIXT_STEREOVU && extrec.type != MIXT_3D
      && extrec.type != MIXT_STEREOPEAK && extrec.type != MIXT_STEREOSLIDER16)
    right = 0;

  val.dev = dev;
  val.ctrl = ctrl;

  if (extrec.type == MIXT_STEREOSLIDER16 || extrec.type == MIXT_MONOSLIDER16)
    {
      if (extrec.flags & MIXF_CENTIBEL)
	{
	  left *= 10;
	  right *= 10;
	}

      if (left < 0)
	left = 0;
      if (left > 0xffff)
	left = 0xffff;
      if (right < 0)
	right = 0;
      if (right > 0xffff)
	right = 0xffff;

      if (val.value == -1)
	val.value = (left & 0xffff) | ((right & 0xffff) << 16);
    }
  else
    {
      if (left < 0)
	left = 0;
      if (left > 255)
	left = 255;
      if (right < 0)
	right = 0;
      if (right > 255)
	right = 255;

      if (val.value == -1)
	val.value = (left & 0x00ff) | ((right & 0x00ff) << 8);
    }

  val.timestamp = extrec.timestamp;
  if (ioctl (mixerfd, SNDCTL_MIX_WRITE, &val) == -1)
    {
      perror ("SNDCTL_MIX_WRITE");
      exit (-1);
    }
  if (quiet)
    return;

  if (extrec.type == MIXT_STEREOSLIDER16 || extrec.type == MIXT_MONOSLIDER16)
    {
      left = val.value & 0xffff;
      right = (val.value >> 16) & 0xffff;

      if (extrec.flags & MIXF_CENTIBEL)
	{
	  left /= 10;
	  right /= 10;
	}

    }
  else
    {
      left = val.value & 0xff;
      right = (val.value >> 8) & 0xff;
    }

  if (extrec.type == MIXT_ONOFF)
    printf ("Value of mixer control %s set to %s\n", cname,
	    val.value ? "ON" : "OFF");
  else if (extrec.type == MIXT_ENUM)
    printf ("Value of mixer control %s set to %s\n", cname,
	    showenum (cname, &extrec, val.value));
  else
    if (extrec.type != MIXT_STEREOSLIDER && extrec.type != MIXT_STEREODB &&
	extrec.type != MIXT_STEREOVU && extrec.type != MIXT_3D
	&& extrec.type != MIXT_STEREOPEAK
	&& extrec.type != MIXT_STEREOSLIDER16)
    printf ("Value of mixer control %s set to %d\n", cname, left);
  else
    {
      if (extrec.type == MIXT_3D)
	{
	  int dist;
	  left = val.value & 0x00ff;
	  dist = (val.value >> 8) & 0xff;
	  right = (val.value >> 16) & 0xffff;
	  printf ("Value of mixer control %s set to %d:%d:%d\n", cname, dist,
		  left, right);
	}
      else
	printf ("Value of mixer control %s set to %d:%d\n",
		cname, left, right);
    }
}

void
show_level (int dev, char *cname)
{
  int ctrl, left = 0, right = 0;
  oss_mixer_value val;
  oss_mixext extrec;
  int mask = 0xff;
  int shift = 8;

  if ((ctrl = find_name (cname)) == -1)
    {
      fprintf (stderr, "Bad mixer control name(2) '%s'\n", cname);
      exit (1);
    }

  extrec.dev = dev;
  extrec.ctrl = ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_EXTINFO, &extrec) == -1)
    {
      perror ("SNDCTL_MIX_EXTINFO");
      exit (-1);
    }

  val.dev = dev;
  val.ctrl = ctrl;
  val.timestamp = extrec.timestamp;
  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
    {
      perror ("SNDCTL_MIX_READ");
      exit (-1);
    }

  if (extrec.type == MIXT_MONOSLIDER16 || extrec.type == MIXT_STEREOSLIDER16)
    {
      mask = 0xffff;
      shift = 16;
    }

  if (extrec.type == MIXT_ONOFF)
    printf ("Value of mixer control %s is currently set to %s\n", cname,
	    val.value ? "ON" : "OFF");
  else if (extrec.type == MIXT_ENUM)
    printf ("Value of mixer control %s set to %s\n", cname,
	    showenum (cname, &extrec, val.value));
  else if (extrec.type == MIXT_VALUE)
    printf ("Value of mixer control %s set to %d\n", cname, val.value);
  else if (extrec.type == MIXT_3D)
    printf ("Value of mixer control %s is currently set to %0d:%d:%d\n",
	    cname, (val.value >> shift) & mask, val.value & mask,
	    (val.value & 0xffff0000) >> 16);
  else if (extrec.type != MIXT_STEREOSLIDER && extrec.type != MIXT_STEREODB
	   && extrec.type != MIXT_STEREOVU
	   && extrec.type != MIXT_STEREOSLIDER16
	   && extrec.type != MIXT_STEREOPEAK)
    {
      left = val.value & mask;

      if (extrec.flags & MIXF_CENTIBEL)
	printf ("Value of mixer control %s is currently set to %d.%d (dB)\n",
		cname, left / 10, left % 10);
      else
	printf ("Value of mixer control %s is currently set to %d\n", cname,
		left);
    }
  else
    {
      left = val.value & mask;
      right = (val.value >> shift) & mask;

      if (extrec.flags & MIXF_CENTIBEL)
	printf
	  ("Value of mixer control %s is currently set to %d.%d:%d.%d (dB)\n",
	   cname, left / 10, left % 10, right / 10, right % 10);
      else
	printf ("Value of mixer control %s is currently set to %d:%d\n",
		cname, left, right);
    }
}

#ifndef MIDI_DISABLED
int state = 0, ch = 0;
int route[16];
int nch = 0;

static void
midi_set (int dev, int ctrl, int v)
{
  oss_mixext *ext;
  oss_mixer_value val;

  if (ctrl >= nch)
    return;

  ctrl = route[ctrl];
  ext = &extrec[ctrl];

  v = (v * ext->maxvalue) / 127;

  val.dev = dev;
  val.ctrl = ctrl;
  val.timestamp = ext->timestamp;

  val.value = (v & 0x00ff) | ((v & 0x00ff) << 8);
  if (ioctl (mixerfd, SNDCTL_MIX_WRITE, &val) == -1)
    {
      perror ("SNDCTL_MIX_WRITE");
      exit (-1);
    }
}

static void
smurf (int dev, int b)
{
  if (state == 0 && ((b & 0xf0) != 0xb0))
    return;

  switch (state)
    {
    case 0:
      ch = b & 0x0f;
      state = 1;
      break;

    case 1:
      if (b != 7)		/* Not main volume */
	{
	  state = 0;
	  break;
	}
      state = 2;
      break;

    case 2:
      state = 0;
      midi_set (dev, ch, b);
      break;
    }

}

void
midi_mixer (int dev, char *mididev, char *argv[], int argp, int argc)
{
  int n = 0;
  int midifd;
  int i, l;
  unsigned char buf[256];

  if ((midifd = open (mididev, O_RDONLY, 0)) == -1)
    {
      perror (mididev);
      exit (-1);
    }

  load_devinfo (dev);

  while (argp < argc && n < 16)
    {
      int ctrl;

      if ((ctrl = find_name (argv[argp])) == -1)
	{
	  fprintf (stderr, "Bad mixer control name(3) '%s'\n", argv[argp]);
	  exit (1);
	}

      route[n] = ctrl;
      argp++;
      n++;
      nch++;
    }

  if (n == 0)
    exit (0);

  while ((l = read (midifd, buf, 256)) > 0)
    {
      for (i = 0; i < l; i++)
	smurf (dev, buf[i]);
    }

  exit (0);
}
#endif

static void
dump_all (void)
{
  int dev;
  oss_sysinfo info;

  if (ioctl (mixerfd, SNDCTL_SYSINFO, &info) == -1)
    {
      perror ("SNDCTL_SYSINFO");
      if (errno == EINVAL)
	fprintf (stderr, "Error: OSS version 4.0 or later is required\n");
      exit (-1);
    }

  for (dev = 0; dev < info.nummixers; dev++)
    {
      load_devinfo (dev);
      dump_devinfo (dev);
    }
}

int
main (int argc, char *argv[])
{
  int dev = 0, c;
  progname = argv[0];

/*
 *	Open the mixer device
 */
  if ((mixerfd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      perror ("/dev/mixer");
      exit (-1);
    }

  while ((c = getopt (argc, argv, "qd:Dcmh")) != EOF)
	{
	  switch (c)
	    {
	    case 'q':
	      quiet = 1;
	      break;

	    case 'd':
	      dev = atoi (optarg);
	      break;

	    case 'D':
	      load_devinfo (dev);
	      verbose_devinfo (dev);
	      exit (0);
	      break;

	    case 'c':
	      dump_all ();
	      exit (0);
	      break;

#ifndef MIDI_DISABLED
	    case 'm':
	      midi_mixer (dev, optarg, argv, optind, argc);
	      exit (0);
	      break;
#endif

	    case 'h':
	    default:
	      usage ();
	    }
	}

  	if (optind==argc)
	{
      		load_devinfo (dev);
      		show_devinfo (dev);
		exit (0);
	}

	  load_devinfo (dev);
	  if (optind >= argc-1)
	     {
	    	show_level (dev, argv[optind]);
	     }
	  else
	     {
	    	change_level (dev, argv[optind], argv[optind + 1]);
	     }

  close (mixerfd);
  return 0;
}
