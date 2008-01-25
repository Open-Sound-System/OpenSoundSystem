/*
 * Purpose: OSS device autodetection utility for SCO OpenServer
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mkdev.h>
#include <sys/stat.h>
#include <sys/param.h>

dev_t default_dev = 0;

#define PCI_PASS	0
#define USB_PASS	1
#define PSEUDO_PASS	2
#define MAX_PASS	3

static char *osslibdir = "/usr/lib/oss";
static char *vmix = "SOFTOSS";

static int verbose = 0;

typedef struct
{
  char *key, *driver, *name;
  int is_3rdparty;
  int detected;
  int pass;
} driver_def_t;

#define MAX_DRIVERS	1000
static driver_def_t drivers[MAX_DRIVERS];
static int ndrivers = 0;

static void
load_license (char *fname)
{
  struct stat st;
  char cmd[256];

  if (stat (fname, &st) == -1)
    return;			/* Doesn't exist */

  if (stat ("/usr/sbin/osslic", &st) == -1)
    return;			/* No osslic utility in the system. No need to install license. */

  sprintf (cmd, "/usr/sbin/osslic -q %s", fname);
  system (cmd);
}

static void
load_devlist (const char *fname, int is_3rdparty)
{
  FILE *f;
  char line[256], *p;
  char *driver, *key, *name;

  if ((f = fopen (fname, "r")) == NULL)
    {
      perror (fname);
      exit (-1);
    }

  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      p = line;
      while (*p)
	{
	  if (*p == '#' || *p == '\n')
	    *p = 0;
	  p++;
	}

      /* Drivers with upper case names are unsupported ones */
      if (*line >= 'A' && *line <= 'Z')
	continue;

      driver = line;
      p = line;

      while (*p && *p != '\t' && *p != ' ')
	p++;
      if (*p)
	*p++ = 0;
      key = p;

      while (*p && *p != '\t')
	p++;
      if (*p)
	*p++ = 0;
      name = p;

      if (verbose > 1)
	printf ("device=%s, name=%s, driver=%s\n", key, name, driver);

      if (ndrivers >= MAX_DRIVERS)
	{
	  printf ("Too many drivers defined in drivers.list\n");
	  exit (-1);
	}

      drivers[ndrivers].key = strdup (key);
      drivers[ndrivers].driver = strdup (driver);
      drivers[ndrivers].name = strdup (name);
      drivers[ndrivers].is_3rdparty = is_3rdparty;
      drivers[ndrivers].detected = 0;

      ndrivers++;
    }

  fclose (f);
}

static int
add_drv (char *id, int pass)
{
  int i;

  for (i = 0; i < ndrivers; i++)
    {
      if (strcmp (id, drivers[i].key) == 0)
	{
	  if (verbose > 0)
	    fprintf (stderr, "Detected %s\n", drivers[i].name);
	  drivers[i].detected = 1;
	  drivers[i].pass = pass;
	  return 1;
	}
    }

  return 0;
}

static void
create_node (char *drvname, char *name, int devno)
{
  struct stat st;
  char tmp[64], *s, *p;
  char cmd[128];
  dev_t dev;
  mode_t perm;

  sprintf (tmp, "/dev/%s", drvname);

  if (stat (tmp, &st) == -1)
    dev = default_dev;
  else
    {
      if (!S_ISCHR (st.st_mode))
	return;
      dev = st.st_rdev;
    }

  if (dev == 0)
    return;

/*
 * Check if the device is located in a subdirectory (say /dev/oss/sblive0/pcm0).
 */
  sprintf (tmp, "/dev/%s", name);

  s = tmp + 5;
  p = s;
  while (*s)
    {
      if (*s == '/')
	p = s;
      s++;
    }

  if (*p == '/')
    {
      *p = 0;			/* Extract the directory name part */
      mkdir ("/dev/oss", 0755);
      mkdir (tmp, 0755);
    }

  sprintf (tmp, "/dev/%s", name);
  dev += devno;
  unlink (tmp);

  perm = umask (0);
  if (mknod (tmp, S_IFCHR | 0666, dev) == -1)
    perror (tmp);
  umask (perm);
}

static void
create_devlinks (void)
{
  FILE *drvf;
  FILE *f;
  struct stat st;
  char drvname[32], name[32], line[64], *s, tmp[256];

  if ((drvf = fopen ("/usr/lib/oss/etc/installed_drivers", "r")) == NULL)
    {
      perror ("/usr/lib/oss/etc/installed_drivers");
      return;
    }

  while (fgets (drvname, sizeof (drvname) - 1, drvf) != NULL)
    {
      s = drvname + strlen (drvname) - 1;
      *s = 0;			/* Remove the LF character */

      /* Remove the device full name (comment) field from the line */
      s = drvname;
      while (*s && *s != ' ' && *s != '#')
	s++;
      *s = 0;

      sprintf (name, "/dev/%s0", drvname);

      if (stat (name, &st) == -1)
	continue;
      default_dev = st.st_rdev;

      if ((f = fopen (name, "r")) == NULL)
	{
	  perror (name);
	  continue;
	}

      mkdir ("/dev/oss", 0755);

      while (fgets (line, sizeof (line) - 1, f) != NULL)
	{
	  int minor;

	  if (sscanf (line, "%s %s %d", name, drvname, &minor) != 3)
	    {
	      fprintf (stderr,
		       "ossdetect: Unexpected line in the drvinfo file %s\n",
		       name);
	      fprintf (stderr, "'%s'\n", line);
	      exit (-1);
	    }

	  create_node (drvname, name, minor);
	}

      fclose (f);
      break;
    }

  fclose (drvf);
}

static void
pci_detect (void)
{
  FILE *f;
  char line[256];

  if ((f = popen ("echo pcishort|/usr/sbin/ndcfg -q", "r")) == NULL)
    {
      perror ("pcishort|/usr/sbin/ndcfg -q");
      fprintf (stderr, "Scanning PCI devices failed\n");
      exit (-1);
    }

  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      int dummy;
      int vendor, device;
      char name[32];

      if (sscanf (line, "%d %x %x %d %d %x %x",
		  &dummy, &dummy, &dummy, &dummy, &dummy,
		  &vendor, &device) != 7)
	{
	  fprintf (stderr, "Bad line returned by ndcfg\n");
	  fprintf (stderr, "%s", line);
	  exit (-1);
	}

      sprintf (name, "pci%x,%x", vendor, device);

      add_drv (name, PCI_PASS);
    }

  pclose (f);
}

int
main (int argc, char *argv[])
{
  struct stat st;
  int i, pass;
  FILE *f;

  if (stat ("/usr/lib/oss/modules/vmix/Driver.o", &st) != -1)
    vmix = "VMIX";

  for (i = 1; i < argc; i++)
    if (argv[i][0] == '-')
      switch (argv[i][1])
	{
	case 'v':
	  verbose++;
	  break;

	case 'd':
	  create_devlinks ();
	  exit (0);
	  break;

	case 'i':
	  add_drv ("IMUX", PSEUDO_PASS);
	  break;

	case 'l':
	  load_license ("/usr/lib/oss/etc/license.asc");
	  exit (0);
	  break;

	default:
	  fprintf (stderr, "%s: bad usage\n", argv[0]);
	  exit (-1);
	}

  load_devlist ("/usr/lib/oss/etc/devices.list", 0);

  if (stat ("/etc/oss_3rdparty", &st) != -1)
    load_devlist ("/etc/oss_3rdparty", 1);

  pci_detect ();

  add_drv (vmix, PSEUDO_PASS);

  if ((f = fopen ("/usr/lib/oss/etc/installed_drivers", "w")) == NULL)
    {
      perror ("/usr/lib/oss/etc/installed_drivers");
      exit (-1);
    }

  for (pass = 0; pass < MAX_PASS; pass++)
    for (i = 0; i < ndrivers; i++)
      if (drivers[i].pass == pass && drivers[i].detected)
	{
	  /* fprintf (f, "%s #%s\n", drivers[i].driver, drivers[i].name); */
	  fprintf (f, "%s\n", drivers[i].driver);
	}

  fclose (f);

  load_license ("/usr/lib/oss/etc/license.asc");

  exit (0);
}
