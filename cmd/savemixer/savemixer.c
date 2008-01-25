#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <oss_config.h>
#include <sys/ioctl.h>

char ossetcdir[512] = "/usr/lib/oss/etc";	/* This is the usual place */
oss_sysinfo sysinfo;
oss_mixerinfo mixerinfo;
oss_mixext *mixerdefs = NULL;
int fd;
int verbose = 0;

#ifdef MANAGE_DEV_DSP
static void
reorder_dspdevs (void)
{
  char line[1024], *p, *s;
  oss_reroute_t reroute[3] = { {0} };
  int i, j, n, m;
  FILE *f;

  sprintf (line, "%s/dspdevs.map", ossetcdir);

  if ((f = fopen (line, "r")) == NULL)
    return;

  n = 0;
  while (n < 3 && fgets (line, sizeof (line) - 1, f))
    {
      for (i = 0; i < strlen (line); i++)
	if (line[i] == '\n')
	  line[i] = 0;
      m = 0;

      s = line;
      while (*s)
	{
	  int v;

	  p = s;

	  while (*p && *p != ' ')
	    p++;
	  if (*p)
	    *p++ = 0;

	  if (m > MAX_PCM_DEV || sscanf (s, "%d", &v) != 1)
	    {
	      fprintf (stderr, "Bad info in dspdevs.map\n");
	      fclose (f);
	      return;
	    }
	  while (*p == ' ')
	    p++;

	  s = p;

	  reroute[n].devlist.devices[m++] = v;
	  reroute[n].devlist.ndevs = m;
	}
      n++;
    }
  fclose (f);

  for (i = 0; i < n; i++)
    {
      reroute[i].mode = i + 1;

      if (ioctl (fd, OSSCTL_SET_REROUTE, &reroute[i]) == -1)
	{
	  if (errno == EINVAL)
	    {
	      fprintf (stderr,
		       "Device configuration changed - use ossctl to update device lists\n");
	      return;
	    }

	  perror ("reroute");
	  return;
	}

      if (verbose)
	{
	  switch (i + 1)
	    {
	    case OPEN_READ:
	      fprintf (stderr, "/dev/dsp input assignment: ");
	      break;
	    case OPEN_WRITE:
	      fprintf (stderr, "/dev/dsp output assignment: ");
	      break;
	    case OPEN_READ | OPEN_WRITE:
	      fprintf (stderr, "/dev/dsp output assignment: ");
	      break;
	    }

	  for (j = 0; j < reroute[i].devlist.ndevs; j++)
	    fprintf (stderr, "%d ", reroute[i].devlist.devices[j]);
	  fprintf (stderr, "\n");
	}
    }

}
#endif

#ifdef APPLIST_SUPPORT
static void
load_applist (void)
{
  char line[1024];

  FILE *f;

  sprintf (line, "%s/applist.conf", ossetcdir);

  if ((f = fopen (line, "r")) == NULL)
    return;

  if (ioctl (fd, OSSCTL_RESET_APPLIST, NULL) == -1)
    {
      perror ("OSSCTL_RESET_APPLIST");
      fclose (f);
      return;
    }

  while (fgets (line, sizeof (line) - 1, f))
    {
      int i;
      char *s, *name, *mode, *dev, *flag;
      app_routing_t rout;

      if (*line == '#')
	continue;

      memset (&rout, 0, sizeof (rout));

      for (i = 0; i < strlen (line); i++)
	if (line[i] == '\n' || line[i] == '#')
	  line[i] = 0;

      s = name = line;

      /* Find the field separator (LWSP) */
      while (*s && (*s != ' ' && *s != '\t'))
	s++;
      while (*s == ' ' || *s == '\t')
	*s++ = 0;

      strncpy (rout.name, name, 32);
      rout.name[32] = 0;

      mode = s;

      /* Find the field separator (LWSP) */
      while (*s && (*s != ' ' && *s != '\t'))
	s++;
      while (*s == ' ' || *s == '\t')
	*s++ = 0;

      for (i = 0; i < strlen (mode); i++)
	switch (mode[i])
	  {
	  case 'r':
	    rout.mode |= OPEN_READ;
	    break;
	  case 'w':
	    rout.mode |= OPEN_WRITE;
	    break;

	  default:
	    fprintf (stderr, "Bad open mode flag '%c' in applist.conf\n",
		     mode[i]);
	  }

      dev = s;

      /* Find the field separator (LWSP) */
      while (*s && (*s != ' ' && *s != '\t'))
	s++;
      while (*s == ' ' || *s == '\t')
	*s++ = 0;

      if (sscanf (dev, "%d", &rout.dev) != 1)
	{
	  fprintf (stderr, "bad audio device number '%s' in applist.conf\n",
		   dev);
	  continue;
	}

      while (*s)
	{
	  flag = s;

	  while (*s && *s != '|')
	    s++;
	  while (*s == '|')
	    *s++ = 0;

	  if (strcmp (flag, "MMAP") == 0)
	    {
	      rout.open_flags |= OF_MMAP;
	      continue;
	    }

	  if (strcmp (flag, "BLOCK") == 0)
	    {
	      rout.open_flags |= OF_BLOCK;
	      continue;
	    }

	  if (strcmp (flag, "NOCONV") == 0)
	    {
	      rout.open_flags |= OF_NOCONV;
	      continue;
	    }

	  fprintf (stderr, "Bad option '%s' in applist.conf\n", flag);
	}

      if (ioctl (fd, OSSCTL_ADD_APPLIST, &rout) == -1)
	{
	  if (errno != ENXIO)
	    perror ("OSSCTL_ADD_APPLIST");
	}
    }

  fclose (f);
}
#endif

char *
get_mapname (void)
{
  FILE *f;
  char tmp[256];
  static char name[256];
  struct stat st;

  if (stat ("/etc/oss", &st) != -1)	/* Use /etc/oss/mixer.save */
    {
      strcpy (name, "/etc/oss/mixer.save");
      strcpy (ossetcdir, "/etc/oss");
      return name;
    }

  if ((f = fopen ("/etc/oss.conf", "r")) == NULL)
    {
      perror ("/etc/oss.conf");
      exit (-1);
    }

  while (fgets (tmp, 255, f) != NULL)
    {
      int l = strlen (tmp);
      if (l > 0 && tmp[l - 1] == '\n')
	tmp[l - 1] = 0;

      if (strncmp (tmp, "OSSLIBDIR=", 10) == 0)
	{
	  sprintf (name, "%s/etc/mixer.save", &tmp[10]);
	  sprintf (ossetcdir, "%s/etc", &tmp[10]);
	  fclose (f);
	  return name;
	}
    }

  fclose (f);

  fprintf (stderr, "Error: OSSLIBDIR not set in /etc/oss.conf\n");
  exit (-1);
  return NULL;
}

static int
find_mixerdev (char *handle)
{
/*
 * Find the mixer device (number) which matches the given handle.
 */

  int i;

  if (mixerdefs != NULL)
    free (mixerdefs);
  mixerdefs = NULL;

  for (i = 0; i < sysinfo.nummixers; i++)
    {
      int j;

      mixerinfo.dev = i;

      if (ioctl (fd, SNDCTL_MIXERINFO, &mixerinfo) == -1)
	{
	  perror ("SNDCTL_MIXERINFO");
	  exit (-1);
	}

      if (strcmp (mixerinfo.handle, handle) == 0)	/* Match */
	{
	  mixerdefs = malloc (sizeof (*mixerdefs) * mixerinfo.nrext);
	  if (mixerdefs == NULL)
	    {
	      fprintf (stderr, "Out of memory\n");
	      exit (-1);
	    }

	  for (j = 0; j < mixerinfo.nrext; j++)
	    {
	      oss_mixext *ext = mixerdefs + j;

	      ext->dev = i;
	      ext->ctrl = j;

	      if (ioctl (fd, SNDCTL_MIX_EXTINFO, ext) == -1)
		{
		  perror ("SNDCTL_MIX_EXTINFO");
		  exit (-1);
		}
	    }

	  return i;
	}
    }

  return -1;
}

static void
change_mixer (char *line)
{
  int value, i;
  char name[256];

  if (sscanf (line, "%s %x", name, &value) != 2)
    {
      fprintf (stderr, "Bad line in mixer.save\n");
      fprintf (stderr, "%s\n", line);
    }

  for (i = 0; i < mixerinfo.nrext; i++)
    {
      oss_mixext *ext = mixerdefs + i;
      oss_mixer_value val;

      if (strcmp (ext->extname, name) == 0)
	{

	  if (!(ext->flags & MIXF_WRITEABLE))
	    continue;

	  if (ext->type == MIXT_GROUP)
	    continue;
	  if (ext->type == MIXT_DEVROOT)
	    continue;
	  if (ext->type == MIXT_MARKER)
	    continue;

	  val.dev = ext->dev;
	  val.ctrl = ext->ctrl;
	  val.timestamp = ext->timestamp;
	  val.value = value;

	  if (ioctl (fd, SNDCTL_MIX_WRITE, &val) == -1)
	    {
	      perror ("SNDCTL_MIX_WRITE");
	      fprintf (stderr, "%s (%d)=%04x\n", name, val.ctrl, value);
	      continue;
	    }
	  return;
	}
    }
}

static void
load_config (char *name)
{
  FILE *f;
  char line[256], *s;
  int dev = -1;

#ifdef MANAGE_DEV_DSP
  reorder_dspdevs ();
#endif
#ifdef APPLIST_SUPPORT
  load_applist ();
#endif

  if ((f = fopen (name, "r")) == NULL)
    {
      /* Nothing to do */
      exit (0);
    }

  /* Remove the EOL character */
  while (fgets (line, sizeof (line) - 1, f) != NULL)
    {
      s = line + strlen (line) - 1;
      if (*s == '\n')
	*s = 0;

      if (*line == '#')
	continue;

      if (*line == 0)
	continue;

      if (*line == '$')
	{
	  if (strcmp (line, "$endmix") == 0)
	    continue;		/* Ignore this */

	  s = line;

	  while (*s && *s != ' ')
	    s++;
	  if (*s == ' ')
	    *s++ = 0;

	  if (strcmp (line, "$startmix") != 0)
	    continue;

	  dev = find_mixerdev (s);

	  continue;
	}

      if (dev < 0)		/* Unknown mixer device? */
	continue;

      change_mixer (line);
    }

  fclose (f);
}

int
main (int argc, char *argv[])
{
  int dev, i;
  char *name;

  FILE *f;

  if ((fd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      if (errno != ENODEV)
	perror ("/dev/mixer");
      exit (-1);
    }

  if (ioctl (fd, SNDCTL_SYSINFO, &sysinfo) == -1)
    {
      perror ("SNDCTL_SYSINFO");
      fprintf (stderr, "Possibly incompatible OSS version\n");
      exit (-1);
    }

  name = get_mapname ();

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] != '-')
	{
	  fprintf (stderr, "%s: Bad usage\n", argv[0]);
	  exit (-1);
	}

      switch (argv[i][1])
	{
	case 'v':
	  verbose++;
	  break;

	case 'L':
	  load_config (name);
	  exit (0);
	  break;

	case 'V':
	  fprintf (stderr, "OSS " OSS_VERSION_STRING " savemixer utility\n");
	  exit (0);
	  break;

	default:
	  fprintf (stderr, "%s: Bad usage\n", argv[0]);
	  exit (-1);
	}
    }

  if (sysinfo.nummixers < 1)
    {
      fprintf (stderr, "No mixers in the system\n");
      exit (0);
    }

  if ((f = fopen (name, "w")) == NULL)
    {
      perror (name);
      exit (-1);
    }
  fprintf (f, "# Automatically generated by OSS savemixer - do not edit\n");

  for (dev = 0; dev < sysinfo.nummixers; dev++)
    {
      mixerinfo.dev = dev;

      if (ioctl (fd, SNDCTL_MIXERINFO, &mixerinfo) == -1)
	{
	  perror ("SNDCTL_MIXERINFO");
	  exit (-1);
	}

      fprintf (f, "\n# %s\n", mixerinfo.name);
      fprintf (f, "$startmix %s\n", mixerinfo.handle);

      for (i = 0; i < mixerinfo.nrext; i++)
	{
	  oss_mixext ext;
	  oss_mixer_value val;

	  ext.dev = dev;
	  ext.ctrl = i;

	  if (ioctl (fd, SNDCTL_MIX_EXTINFO, &ext) == -1)
	    {
	      perror ("SNDCTL_MIX_EXTINFO");
	      exit (-1);
	    }

	  if (!(ext.flags & MIXF_WRITEABLE))
	    continue;

	  if (ext.type == MIXT_GROUP)
	    continue;
	  if (ext.type == MIXT_DEVROOT)
	    continue;
	  if (ext.type == MIXT_MARKER)
	    continue;

	  val.dev = dev;
	  val.ctrl = i;
	  val.timestamp = ext.timestamp;

	  if (ioctl (fd, SNDCTL_MIX_READ, &val) == -1)
	    {
	      perror ("SNDCTL_MIX_READ");
	      continue;
	    }

	  fprintf (f, "%s %04x\n", ext.extname, val.value);
	}

      fprintf (f, "$endmix\n");
    }
  fclose (f);

  return 0;
}
