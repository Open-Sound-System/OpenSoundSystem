/*
 * Purpose: OSS device autodetection utility for FreeBSD
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/pciio.h>
#include </sys/dev/pci/pcireg.h>

#define PCI_PASS	0
#define USB_PASS	1
#define PSEUDO_PASS	2
#define MAX_PASS	3

static char *osslibdir = "/usr/lib/oss";

static int verbose = 0;

static char *vmix = "VMIX";

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
create_devlinks (void)
{
  FILE *f;
  char line[256], tmp[300], *s;

  if ((f = fopen ("/proc/opensound/devfiles", "r")) == NULL)
    {
      perror ("/proc/opensound/devfiles");
      fprintf (stderr, "Cannot connect to the OSS kernel module.\n");
      fprintf (stderr, "Perhaps you need to execute 'soundon' to load OSS\n");
      exit (-1);
    }

  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      char dev[20] = "/dev/";
      int minor, major;
      s = line + strlen (line) - 1;
      *s = 0;

      if (sscanf (line, "%s %d %d", dev + 5, &major, &minor) != 3)
	{
	  fprintf (stderr, "Syntax error in /proc(opensound/devfiles\n");
	  fprintf (stderr, "%s\n", line);
	  exit (-1);
	}

      sprintf (tmp, "mknod %s c %d %d", dev, major, minor);
      unlink (dev);
      if (verbose)
	printf ("%s\n", tmp);
      system (tmp);
      chmod (dev, 0666);
    }

  fclose (f);
}

static void
pci_detect (void)
{
  int fd;
  struct pci_conf_io pc;
  struct pci_conf conf[255], *p;

  if ((fd = open ("/dev/pci", O_RDONLY, 0)) == -1)
    {
      perror ("/dev/pci");
      exit (-1);
    }

  bzero (&pc, sizeof (struct pci_conf_io));
  pc.match_buf_len = sizeof (conf);
  pc.matches = conf;

  do
    {
      if (ioctl (fd, PCIOCGETCONF, &pc) == -1)
	{
	  perror ("ioctl(PCIOCGETCONF)");
	  exit (1);
	}

      /*
       * 255 entries should be more than enough for most people,
       * but if someone has more devices, and then changes things
       * around between ioctls, we'll do the cheezy thing and
       * just bail.  The alternative would be to go back to the
       * beginning of the list, and print things twice, which may
       * not be desireable.
       */
      if (pc.status == PCI_GETCONF_LIST_CHANGED)
	{
	  fprintf (stderr, "PCI device list changed, please try again");
	  exit (1);
	  close (fd);
	  return;
	}
      else if (pc.status == PCI_GETCONF_ERROR)
	{
	  fprintf (stderr, "error returned from PCIOCGETCONF ioctl");
	  exit (1);
	  close (fd);
	  return;
	}
      for (p = conf; p < &conf[pc.num_matches]; p++)
	{

	  char name[32];

	  if (verbose > 2)
	    printf ("%s%d@pci%d:%d:%d:\tclass=0x%06x card=0x%08x "
		    "chip=0x%08x rev=0x%02x hdr=0x%02x\n",
		    (p->pd_name && *p->pd_name) ? p->pd_name :
		    "none",
		    (p->pd_name && *p->pd_name) ? (int) p->pd_unit :
		    p->pc_sel.pc_bus, p->pc_sel.pc_dev,
		    p->pc_sel.pc_func, (p->pc_class << 16) |
		    (p->pc_subclass << 8) | p->pc_progif,
		    (p->pc_subdevice << 16) | p->pc_subvendor,
		    (p->pc_device << 16) | p->pc_vendor,
		    p->pc_revid, p->pc_hdr);

	  sprintf (name, "pcs%x,%x", p->pc_subvendor, p->pc_subdevice);
	  if (add_drv (name, PCI_PASS))
	    continue;

	  sprintf (name, "pci%x,%x", p->pc_vendor, p->pc_device);
	  if (add_drv (name, PCI_PASS))
	    continue;
	}
    }
  while (pc.status == PCI_GETCONF_MORE_DEVS);

  close (fd);
}

int
main (int argc, char *argv[])
{
  struct stat st;
  int i, pass;
  FILE *f;

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
	  fprintf (f, "%s #%s\n", drivers[i].driver, drivers[i].name);
	}

  fclose (f);

  load_license ("/usr/lib/oss/etc/license.asc");

  exit (0);
}
