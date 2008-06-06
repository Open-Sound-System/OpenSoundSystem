/*
 * Purpose: OSS device autodetection utility for Linux
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
#include <sys/dir.h>

#define PCI_PASS	0
#define USB_PASS	1
#define PSEUDO_PASS	2
#define MAX_PASS	3

/* static char *osslibdir = "/usr/lib/oss"; */

static int usb_ok = 0;

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

static unsigned short
get_uint16 (unsigned char *p)
{
  return *p + (p[1] << 8);
}

static int
decode_descriptor (unsigned char *d, int desclen)
{
  switch (d[1])
    {
    case 0x04:
      if (d[5] == 1)		// Audio
	return 1;
      break;
    }

  return 0;
}

static int
is_audio (unsigned char *desc, int datalen)
{
  int l, pos, desclen;

  if (desc[0] == 9 && desc[1] == 2)	/* Config descriptor */
    {
      l = get_uint16 (desc + 2);

      if (datalen > l)
	datalen = l;
    }

  pos = 0;

  while (pos < datalen)
    {
      unsigned char *d;
      desclen = desc[pos];

      if (desclen < 2 || desclen > (datalen - pos))
	{
	  return 0;
	}

      d = &desc[pos];
      if (decode_descriptor (d, desclen))
	return 1;
      pos += desclen;
    }

  return 0;
}

static void
usb_checkdevice (char *fname)
{
  int fd, l;
  unsigned char buf[4096];
  char tmp[64];
  int vendor, device;

  if ((fd = open (fname, O_RDONLY, 0)) == -1)
    {
      perror (fname);
      return;
    }

  if ((l = read (fd, buf, sizeof (buf))) == -1)
    {
      perror (fname);
      return;
    }

  close (fd);

  if (l < 12 || buf[1] != 1)
    return;

  vendor = buf[8] | (buf[9] << 8);
  device = buf[10] | (buf[11] << 8);

  sprintf (tmp, "usb%x,%x", vendor, device);
  if (add_drv (tmp, USB_PASS))
    return;

  sprintf (tmp, "usbif%x,%x", vendor, device);
  if (add_drv (tmp, USB_PASS))
    return;

  if (is_audio (buf, l))	/* USB audio class */
    {
      if (add_drv ("usbif,class1", USB_PASS))
	return;
    }
}

static void
usb_scandir (char *dirname)
{
  char path[PATH_MAX];
  DIR *dr;
  struct dirent *de;
  struct stat st;

  if ((dr = opendir (dirname)) == NULL)
    {
      return;
    }

  while ((de = readdir (dr)) != NULL)
    {
      if (de->d_name[0] < '0' || de->d_name[0] > '9')	/* Ignore non-numeric names */
	continue;

      sprintf (path, "%s/%s", dirname, de->d_name);

      if (stat (path, &st) == -1)
	continue;

      if (S_ISDIR (st.st_mode))
	{
	  usb_scandir (path);
	  continue;
	}

      usb_checkdevice (path);
    }

  closedir (dr);
}

static void
usb_detect (void)
{
  struct stat st;
#if 0
  char path[512];

  sprintf (path, "%s/modules/oss_usb", osslibdir);

  if (stat (path, &st) == -1)	/* USB module not available */
    return;

#endif
  if (stat ("/proc/bus/usb", &st) == -1)
    return;
  usb_ok = 1;
  usb_scandir ("/proc/bus/usb");
}

static void
pci_checkdevice (char *path)
{
  unsigned char buf[256];
  char id[32];
  int fd;
  int vendor, device;
  int subvendor, subdevice;

  if ((fd = open (path, O_RDONLY, 0)) == -1)
    {
      perror (path);
      return;
    }

  if (read (fd, buf, sizeof (buf)) != sizeof (buf))
    {
      perror (path);
      close (fd);
      return;
    }
  close (fd);
  vendor = buf[0] | (buf[1] << 8);
  device = buf[2] | (buf[3] << 8);
  subvendor = buf[0x2c] | (buf[0x2d] << 8);
  subdevice = buf[0x2e] | (buf[0x2f] << 8);
  sprintf (id, "pcs%x,%x", subvendor, subdevice);
  if (add_drv (id, PCI_PASS))
    return;
  sprintf (id, "pci%x,%x", vendor, device);
  add_drv (id, PCI_PASS);
}
static void
pci_detect (char *dirname)
{
  char path[PATH_MAX];
  DIR *dr;
  struct dirent *de;
  struct stat st;

  if (dirname == NULL)
    {
      dirname = "/proc/bus/pci";
    }

  if ((dr = opendir (dirname)) == NULL)
    {
      return;
    }

  while ((de = readdir (dr)) != NULL)
    {
      if (de->d_name[0] < '0' || de->d_name[0] > '9')	/* Ignore non-numeric names */
	continue;

      sprintf (path, "%s/%s", dirname, de->d_name);

      if (stat (path, &st) == -1)
	continue;

      if (S_ISDIR (st.st_mode))
	{
	  pci_detect (path);
	  continue;
	}

      pci_checkdevice (path);
    }

  closedir (dr);
}

static void
create_devlinks (void)
{
  FILE *f;
  char line[256], tmp[300], *p, *s;

  system ("rm -rf /dev/oss");
  mkdir ("/dev/oss", 0755);

  if ((f = fopen ("/proc/opensound/devfiles", "r")) == NULL)
    {
      perror ("/proc/opensound/devfiles");
      fprintf (stderr, "Cannot connect to the OSS kernel module.\n");
      fprintf (stderr, "Perhaps you need to execute 'soundon' to load OSS\n");
      exit (-1);
    }

  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      char dev[64] = "/dev/";
      int minor, major;
      s = line + strlen (line) - 1;
      *s = 0;

      if (sscanf (line, "%s %d %d", dev + 5, &major, &minor) != 3)
	{
	  fprintf (stderr, "Syntax error in /proc(opensound/devfiles\n");
	  fprintf (stderr, "%s\n", line);
	  exit (-1);
	}

/*
 * Check if the device is located in a subdirectory (say /dev/oss/sblive0/pcm0).
 */
      strcpy (tmp, dev);

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
	  *p = 0;		/* Extract the directory name part */
	  mkdir (tmp, 0755);
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

int
main (int argc, char *argv[])
{
  struct stat st;
  int i, pass;
  FILE *f;

  load_license ("/usr/lib/oss/etc/license.asc");

  load_devlist ("/usr/lib/oss/etc/devices.list", 0);

  if (stat ("/etc/oss_3rdparty", &st) != -1)
    load_devlist ("/etc/oss_3rdparty", 1);

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

  pci_detect (NULL);
  usb_detect ();

  if (usb_ok)
    {
      //printf ("USB support available in the system\n");
      add_drv ("usbif,class1", USB_PASS);
    }
  else
    printf ("No USB support detected in the system - skipping USB\n");

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

  exit (0);
}
