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
#include <local_config.h>

static char *progname = NULL;
static int mixerfd = -1, nrext = 0, quiet = 0, verbose = 0;

static oss_mixext *extrec;
static oss_mixext_root *root;

static void change_level (int, char *, char *);
static void dump_all (void);
static void dump_devinfo (int);
static int findenum (char *, oss_mixext *, char *);
static int find_name (char *);
static void load_devinfo (int);
static void print_description (char *);
static void show_devinfo (int);
static void show_level (int, char *);
static char * showchoices (char *, oss_mixext *);
static char * showenum (char *, oss_mixext *, int);
static void usage (void);
static void verbose_devinfo (int);
#ifdef CONFIG_OSS_MIDI
static void midi_set (int, int, int);
static void midi_mixer (int, char *, char **, int, int);
static void smurf (int, int);
#endif

static void
usage (void)
{
  printf ("Usage: %s -h		Displays help (this screen)\n", progname);
  printf ("Usage: %s [-d<devno>] [arguments]\n", progname);
  printf ("arguments:\n");
  printf ("\t-D			Display device information\n");
  printf ("\t-c			Dump mixer settings for all mixers\n");
  printf ("\tctrl# value		Change value of a mixer control\n");
  printf ("\t-q			Quiet mode\n");
  printf ("\t-v1|-v2		Verbose mode (-v2 is more verbose).\n");
  printf ("\t<no arguments>	Display current/possible settings\n");
  exit (-1);
}

static void
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

static void
verbose_devinfo (int dev)
{
  int i;
  oss_mixext *thisrec;
  oss_mixer_value val;
  val.dev = dev;

  for (i = 0; i < nrext; i++)
    {
      thisrec = &extrec[i];
      printf ("%2d: ", i);

      val.ctrl = i;
      val.timestamp = thisrec->timestamp;
      val.value = -1;

      switch (thisrec->type)
	{
	case MIXT_DEVROOT:
	  printf ("\nDevice root '%s' / %s\n", root->id, root->name);
	  break;

	case MIXT_GROUP:
	  printf ("Group: '%s', parent=%d, flags=0x%x\n", thisrec->id,
                  thisrec->parent, thisrec->flags);
	  break;

	case MIXT_STEREOSLIDER:
	case MIXT_STEREODB:
	  printf ("Stereo slider: '%s' (%s), parent=%d, max=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_STEREOSLIDER16:
	  printf ("Stereo slider: '%s' (%s), parent=%d, max=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%08x\n", val.value);
	  break;

	case MIXT_3D:
	  printf ("3D control: '%s' (%s), parent=%d, max=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%08x\n", val.value);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  printf ("Stereo peak meter: '%s' (%s), parent=%d, max=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_MONOSLIDER:
	case MIXT_MONOSLIDER16:
	case MIXT_SLIDER:
	case MIXT_MONODB:
	  printf ("Mono slider: '%s' (%s), parent=%d, max=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_MONOPEAK:
	  printf ("Mono peak meter: '%s' (%s), parent=%d, max=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->maxvalue, thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(monopeak)");
	  printf ("  Current value=0x%04x\n", val.value);
	  break;

	case MIXT_ONOFF:
	case MIXT_MUTE:
	  printf ("On/off switch: '%s' (%s), parent=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(onoff)");
	  printf ("  Current value=0x%x (%s)\n", val.value,
		  val.value ? "ON" : "OFF");
	  break;

	case MIXT_ENUM:
	  printf
	    ("Enumerated control: '%s' (%s), parent=%d, flags=0x%x,"
             " mask=%02x%02x", thisrec->id, thisrec->extname, thisrec->parent,
             thisrec->flags, thisrec->enum_present[1],
             thisrec->enum_present[0]);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(enum)");
	  printf ("  Current value=0x%x\n", val.value);
	  break;

	case MIXT_VALUE:
	  printf ("Decimal value: '%s' (%s), parent=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(value)");
	  printf ("  Current value=%d\n", val.value);
	  break;

	case MIXT_HEXVALUE:
	  printf ("Hexadecimal value: '%s' (%s), parent=%d, flags=0x%x",
		  thisrec->id, thisrec->extname, thisrec->parent,
		  thisrec->flags);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(hex)");
	  printf ("  Current value=0x%x\n", val.value);
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
static char *
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
static char *
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
static int
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
      return -1;
    }

  return n;
}

static void
print_description (char *descr)
{
	/*
	 * Print the description string. If verbose==1 then print only the 
	 * first line. Otherwise print the subsequent lines too.
	 */

	char *p = descr;

	while (*p != 0)
	{
		while (*p && *p != '\n')
			p++;

		if (*p=='\n')
		   *p++ = 0;

		printf("  %s\n", descr);

		if (verbose < 2) /* Print only the first line */
		   return;

		descr = p;
	}
}

static void
show_devinfo (int dev)
{
  int i, vl, vr, rflag;
  oss_mixext *thisrec;
  oss_mixer_value val;

  val.dev = dev;
  printf ("Selected mixer %d/%s\n", dev, root->name);
  printf ("Known controls are:\n");
  for (i = 0; i < nrext; i++)
    {

      thisrec = &extrec[i];
      rflag = 1;
      val.ctrl = i;
      val.timestamp = thisrec->timestamp;
      val.value = -1;

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
	  rflag = 0;
	  break;

	case MIXT_STEREOSLIDER:
	case MIXT_STEREODB:
	  printf ("%s [<leftvol>:<rightvol>]", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf (" (currently %d:%d)", val.value & 0xff,
		  (val.value >> 8) & 0xff);
	  break;

	case MIXT_STEREOSLIDER16:
	  printf ("%s [<leftvol>:<rightvol>]", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  if (thisrec->flags & MIXF_CENTIBEL)
	    {
	      vl = val.value & 0xffff;
	      vr = (val.value >> 16) & 0xffff;
	      printf (" (currently %d.%d:%d.%d dB)", vl / 10, vl % 10,
		      vr / 10, vr % 10);
	    }
	  else
	    printf (" (currently %d:%d)", val.value & 0xffff,
		    (val.value >> 16) & 0xffff);
	  break;

	case MIXT_3D:
	  printf ("%s <vol:distance:angle>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf (" (currently %d:%d:%d)", val.value & 0x00ff,
		  (val.value >> 8) & 0xff, (val.value >> 16) & 0xffff);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  printf ("%s [<leftVU>:<rightVU>]", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf (" (currently %d:%d)", val.value & 0xff,
		  (val.value >> 8) & 0xff);
	  break;

	case MIXT_ENUM:
	  printf ("%s <%s>", thisrec->extname,
		  showchoices (thisrec->extname, thisrec));
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(enum2)");
	  printf (" (currently %s)",
		  showenum (extrec[i].extname, thisrec, val.value & 0xff));
	  break;

	case MIXT_MONOSLIDER:
	case MIXT_MONODB:
	  printf ("%s <monovol>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf (" (currently %d)", val.value & 0xff);
	  break;

	case MIXT_SLIDER:
	  printf ("%s <monovol>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf (" (currently %d)", val.value);
	  break;

	case MIXT_MONOSLIDER16:
	  printf ("%s <monovol>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  if (thisrec->flags & MIXF_CENTIBEL)
	    {
	      vl = val.value & 0xffff;
	      printf (" (currently %d.%d dB)", vl / 10, vl % 10);
	    }
	  else
	    printf (" (currently %d)", val.value & 0xffff);
	  break;

	case MIXT_MONOVU:
	  printf ("%s <monoVU>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf (" (currently %d)", val.value & 0xff);
	  break;

	case MIXT_VALUE:
	  printf ("%s <decimal value>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(value2)");
	  printf (" (currently %d)", val.value);
	  break;

	case MIXT_HEXVALUE:
	  printf ("%s <hexadecimal value>", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(hex2)");
	  printf (" (currently 0x%x)", val.value);
	  break;

	case MIXT_ONOFF:
	case MIXT_MUTE:
	  printf ("%s ON|OFF", thisrec->extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(onoff)");
	  printf (" (currently %s)", val.value ? "ON" : "OFF");
	  break;

	default:
	  printf ("Unknown mixer extension type %d", thisrec->type);
	}

      if (rflag)
	{
	  if ((thisrec->flags & MIXF_WRITEABLE) == 0) printf(" (Read-only)");
	  printf ("\n");
	}

          if (verbose && (thisrec->flags & MIXF_DESCR))
  	    {
              oss_mixer_enuminfo ei;

              ei.dev = dev;
              ei.ctrl = i;
              if (ioctl (mixerfd, SNDCTL_MIX_DESCRIPTION, &ei) == -1)
	        {
		  perror ("SNDCTL_MIX_DESCRIPTION");
		  continue;
		}

              print_description (ei.strings);
  	    }
    }
}

static void
dump_devinfo (int dev)
{
  char ossmix[256];
  int i, enabled = 0;
  oss_mixext *thisrec;
  oss_mixer_value val;

  val.dev = dev;
  snprintf (ossmix, sizeof (ossmix), "!ossmix -d%d", dev);

  for (i = 0; i < nrext; i++)
    {
      thisrec = &extrec[i];

      val.ctrl = i;
      val.timestamp = thisrec->timestamp;
      val.value = -1;

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
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf ("%d:%d\n", val.value & 0xff, (val.value >> 8) & 0xff);
	  break;

	case MIXT_STEREOSLIDER16:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(stereo2)");
	  printf ("%d:%d\n", val.value & 0xffff, (val.value >> 16) & 0xffff);
	  break;

	case MIXT_3D:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(3D)");
	  printf ("%d:%d:%d\n",
		  val.value & 0x00ff, (val.value >> 8) & 0x00ff,
		  (val.value >> 16) & 0xffff);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  break;

	case MIXT_ENUM:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(enum2)");
	  printf ("%s\n",
		  showenum (extrec[i].extname, thisrec, val.value & 0xff));
	  break;

	case MIXT_MONOSLIDER:
	case MIXT_MONODB:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf ("%d\n", val.value & 0xff);
	  break;

	case MIXT_SLIDER:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf ("%d\n", val.value);
	  break;

	case MIXT_MONOSLIDER16:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(mono2)");
	  printf ("%d\n", val.value & 0xffff);
	  break;

	case MIXT_MONOVU:
	  break;

	case MIXT_VALUE:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(value2)");
	  printf ("%d\n", val.value);
	  break;

	case MIXT_HEXVALUE:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(hex2)");
	  printf ("0x%x\n", val.value);
	  break;

	case MIXT_ONOFF:
	case MIXT_MUTE:
	  printf ("%s %s ", ossmix, extrec[i].extname);
	  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
	    perror ("SNDCTL_MIX_READ(onoff)");
	  printf ("%s\n", val.value ? "ON" : "OFF");
	  break;

	default:
	  printf ("Unknown mixer extension type %d\n", thisrec->type);
	}

    }
}

static int
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

static void
change_level (int dev, char *cname, char *arg)
{
  int ctrl, lefti, righti, dist = 0, vol = 0;
  float left = -1, right = 0;
  oss_mixer_value val;
  oss_mixext extrec;

  if ((ctrl = find_name (cname)) == -1)
    {
      fprintf (stderr, "Bad mixer control name(%d) '%s'\n", __LINE__, cname);
      exit (1);
    }

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
      if (val.value < 0) exit (1);
    }
  else if (extrec.type == MIXT_HEXVALUE)
    {
      if (sscanf (arg, "%x", &lefti) != 1)
	goto argerror;
      left = lefti;
    }
  else if (extrec.type == MIXT_VALUE)
    {
      if (sscanf (arg, "%f", &left) != 1)
	goto argerror;
    }
  else if (extrec.type == MIXT_3D)
    {
      if (sscanf (arg, "%d:%d:%f", &vol, &dist, &right) != 3)
	{
	  fprintf (stderr, "Bad 3D position '%s'\n", arg);
	  exit (1);
	}
    }
  else if (strcmp (arg, "ON") == 0 || strcmp (arg, "on") == 0)
    left = 1;
  else if (strcmp (arg, "OFF") == 0 || strcmp (arg, "off") == 0)
    left = 0;
  else if (sscanf (arg, "%f:%f", &left, &right) != 2)
    {
      if (sscanf (arg, "%f", &left) != 1)
	goto argerror;
      else
	right = left;
    }

  if (left < 0)
    left = 0;
  if (right < 0)
    right = 0;
  if (extrec.flags & MIXF_CENTIBEL)
    {
      lefti = left * 10;
      righti = right * 10;
    }
  else
    {
      lefti = left;
      righti = right;
    }

  val.dev = dev;
  val.ctrl = ctrl;

  switch (extrec.type)
    {
      case MIXT_STEREOSLIDER16:
      case MIXT_MONOSLIDER16:
        if (lefti > 0xffff) lefti = 0xffff;
        if (righti > 0xffff) righti = 0xffff;
        val.value = (lefti & 0xffff) | ((righti & 0xffff) << 16);
        break;
      case MIXT_MONOSLIDER:
      case MIXT_MONODB:
      case MIXT_MONOVU:
      case MIXT_MONOPEAK:
	if (lefti > 255) lefti = 255;
	if (righti > 255) righti = 255;
        break;
      case MIXT_3D:
        if (vol > 255) vol = 255;
        if (righti > 0xffff) righti = 0xffff;
        if (dist < 0) dist = 0;
        if (dist > 255) dist = 255;

        val.value =
	    (vol & 0x00ff) | ((righti & 0xffff) << 16) | ((dist & 0xff) << 8);
        break;
      case MIXT_HEXVALUE:
      case MIXT_VALUE:
	val.value = left;
        break;
      case MIXT_ENUM:
        break;
      default:
	val.value = (lefti & 0x00ff) | ((righti & 0x00ff) << 8);
        break;
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
      lefti = val.value & 0xffff;
      righti = (val.value >> 16) & 0xffff;
    }
  else if (extrec.type == MIXT_3D)
    {
      vol = val.value & 0x00ff;
      dist = (val.value >> 8) & 0xff;
      righti = (val.value >> 16) & 0xffff;
    }
  else
    {
      lefti = val.value & 0xff;
      righti = (val.value >> 8) & 0xff;
    }

  if (extrec.flags & MIXF_CENTIBEL)
    {
      left = (float)lefti / 10;
      right = (float)righti / 10;
    }
  else
    {
      left = lefti;
      right = righti;
    }

  switch (extrec.type)
    {
      case MIXT_ONOFF:
      case MIXT_MUTE:
        printf ("Value of mixer control %s set to %s\n", cname,
	        val.value ? "ON" : "OFF");
        break;
      case MIXT_3D:
        printf ("Value of mixer control %s set to %d:%d:%.1f\n", cname,
                 vol, dist, right);
        break;
      case MIXT_ENUM:
        printf ("Value of mixer control %s set to %s\n", cname,
                showenum (cname, &extrec, val.value));
        break;
      case MIXT_STEREOSLIDER:
      case MIXT_STEREOSLIDER16:
      case MIXT_STEREODB:
      case MIXT_STEREOPEAK:
      case MIXT_STEREOVU:
	printf ("Value of mixer control %s set to %.1f:%.1f\n",
		cname, left, right);
        break;
      default:
        printf ("Value of mixer control %s set to %.1f\n", cname, left);
        break;
    }

  return;

argerror:
  fprintf (stderr, "Bad mixer level '%s'\n", arg);
  exit (1);
}

static void
show_level (int dev, char *cname)
{
  int ctrl, left = 0, right = 0;
  oss_mixer_value val;
  oss_mixext extrec;
  int mask = 0xff;
  int shift = 8;

  if ((ctrl = find_name (cname)) == -1)
    {
      fprintf (stderr, "Bad mixer control name(%d) '%s'\n", __LINE__, cname);
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

  switch (extrec.type)
    {
      case MIXT_ONOFF:
      case MIXT_MUTE:
        printf ("Value of mixer control %s is currently set to %s\n", cname,
	        val.value ? "ON" : "OFF");
        break;
      case MIXT_ENUM:
        printf ("Value of mixer control %s set to %s\n", cname,
	        showenum (cname, &extrec, val.value));
        break;
      case MIXT_STEREOSLIDER:
      case MIXT_STEREOSLIDER16:
      case MIXT_STEREODB:
      case MIXT_STEREOPEAK:
      case MIXT_STEREOVU:
        left = val.value & mask;
        right = (val.value >> shift) & mask;

        if (extrec.flags & MIXF_CENTIBEL)
  	  printf
	    ("Value of mixer control %s is currently set to %d.%d:%d.%d (dB)\n",
	     cname, left / 10, left % 10, right / 10, right % 10);
        else
 	  printf ("Value of mixer control %s is currently set to %d:%d\n",
		  cname, left, right);
        break;
      case MIXT_VALUE:
        printf ("Value of mixer control %s set to %d\n", cname, val.value);
        break;
      case MIXT_HEXVALUE:
        printf ("Value of mixer control %s set to 0x%x\n", cname, val.value);
        break;
      case MIXT_3D:
         printf ("Value of mixer control %s is currently set to %0d:%d:%d\n",
 	         cname, (val.value >> shift) & mask, val.value & mask,
	         (val.value & 0xffff0000) >> 16);
         break;
      default:
        left = val.value & mask;

        if (extrec.flags & MIXF_CENTIBEL)
  	  printf ("Value of mixer control %s is currently set to %d.%d (dB)\n",
	 	  cname, left / 10, left % 10);
        else
  	  printf ("Value of mixer control %s is currently set to %d\n", cname,
		  left);
        break;
    }
}

#ifdef CONFIG_OSS_MIDI
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

static void
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
	  fprintf (stderr, "Bad mixer control name(%d) '%s'\n", __LINE__,
                   argv[argp]);
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
  int dev, nummixers;

  if (ioctl (mixerfd, SNDCTL_MIX_NRMIX, &nummixers) == -1)
    {
      perror ("SNDCTL_MIX_NRMIX");
      if (errno == EINVAL)
	fprintf (stderr, "Error: OSS version 4.0 or later is required\n");
      exit (-1);
    }

  for (dev = 0; dev < nummixers; dev++)
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

  while ((c = getopt (argc, argv, "Dcd:hmqv:")) != EOF)
	{
	  switch (c)
	    {
	    case 'q':
	      quiet = 1;
	      verbose = 0;
	      break;

	    case 'v':
	      verbose = atoi (optarg);
	      if (verbose==0)
		 verbose = 1;
	      quiet = 0;
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

#ifdef CONFIG_OSS_MIDI
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
