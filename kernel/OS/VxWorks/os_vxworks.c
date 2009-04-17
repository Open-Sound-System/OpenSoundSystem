/*
 * oss_vxworks.c: Common entry points for OSS under VxWorks.
 */

#include <oss_config.h>

#if 0
// TODO: Obsoletew
typedef struct oss_device_handle
{
  DEV_HDR devHdr;
  int minor;
  int valid;			/* 1=valid, 0=undefined */
  struct fileinfo finfo;
}
oss_device_handle;
#endif

static int oss_driver_num = ERROR;
static int oss_expired = 0;
static oss_device_t *core_osdev = NULL;

void
oss_cmn_err (int level, const char *s, ...)
{
  char tmp[1024], *a[6];
  va_list ap;
  int i, n = 0;

  va_start (ap, s);

  for (i = 0; i < strlen (s); i++)
    if (s[i] == '%')
      n++;

  for (i = 0; i < n && i < 6; i++)
    a[i] = ( (sizeof(char *) == 32) ? ( *((char * **)(ap += ((sizeof(char * *)+sizeof(int)-1) & ~(sizeof(int)-1))))[-1] ) : ( ((char * *)(ap += ((sizeof(char *)+sizeof(int)-1) & ~(sizeof(int)-1))))[-1] ));
    //a[i] = va_arg (ap, char *); // This was supposed to be used instead of above. Unfortunately va_arg() seems to be buggy

  for (i = n; i < 6; i++)
    a[i] = NULL;

  if (level == CE_CONT)
    {
      sprintf (tmp, s, a[0], a[1], a[2], a[3], a[4], a[5], NULL,
	       NULL, NULL, NULL);
      printf ("%s", tmp);
    }
  else
    {
      strcpy (tmp, "osscore: ");
      sprintf (tmp + strlen (tmp), s, a[0], a[1], a[2], a[3], a[4], a[5],
	       NULL, NULL, NULL, NULL);
      if (level == CE_PANIC)
	panic (tmp);

      printf ("%s", tmp);
    }

  va_end (ap);
}

int
oss_uiomove (void *addr, size_t nbytes, enum uio_rw rwflag, uio_t * uio)
{
/*
 * NOTE! Returns 0 upon success and EFAULT on failure (instead of -EFAULT
 * (for Solaris/BSD compatibilityi)).
 */

  int c;
  char *address = addr;

  if (rwflag != uio->rw)
    {
      oss_cmn_err (CE_WARN, "uiomove: Bad direction\n");
      return EFAULT;
    }

  if (uio->resid < nbytes)
    {
      oss_cmn_err (CE_WARN, "uiomove: Bad count %d (%d)\n", nbytes,
		   uio->resid);
      return EFAULT;
    }

  if (uio->kernel_space)
    return EFAULT;

#if 0
  // TODO
  switch (rwflag)
    {
    case UIO_READ:
      c = nbytes;
      if (c > 10)
	c = 0;

      if ((c = copy_to_user (uio->ptr, address, nbytes) != 0))
	{
	  uio->resid -= nbytes;
	  oss_cmn_err (CE_CONT, "copy_to_user(%d) failed (%d)\n", nbytes, c);
	  return EFAULT;
	}
      break;

    case UIO_WRITE:
      if (copy_from_user (address, uio->ptr, nbytes) != 0)
	{
	  oss_cmn_err (CE_CONT, "copy_from_user failed\n");
	  uio->resid -= nbytes;
	  return EFAULT;
	}
      break;
    }
#endif

  uio->resid -= nbytes;
  uio->ptr += nbytes;

  return 0;
}

int
oss_create_uio (uio_t * uio, char *buf, size_t count, uio_rw_t rw,
		int is_kernel)
{
  memset (uio, 0, sizeof (*uio));

  if (is_kernel)
    {
      oss_cmn_err (CE_CONT,
		   "oss_create_uio: Kernel space buffers not supported\n");
      return -EIO;
    }

  uio->ptr = buf;
  uio->resid = count;
  uio->kernel_space = is_kernel;
  uio->rw = rw;

  return 0;
}

void
oss_cmn_err (int level, const char *s, ...)
{
  char tmp[1024], *a[6];
  va_list ap;
  int i, n = 0;

  va_start (ap, s);

  for (i = 0; i < strlen (s); i++)
    if (s[i] == '%')
      n++;

  for (i = 0; i < n && i < 6; i++)
    a[i] = va_arg (ap, char *);

  for (i = n; i < 6; i++)
    a[i] = NULL;

  if (level == CE_CONT)
    {
      sprintf (tmp, s, a[0], a[1], a[2], a[3], a[4], a[5], NULL,
	       NULL, NULL, NULL);
      printf ("%s", tmp);
    }
  else
    {
      strcpy (tmp, "osscore: ");
      sprintf (tmp + strlen (tmp), s, a[0], a[1], a[2], a[3], a[4], a[5],
	       NULL, NULL, NULL, NULL);
      if (level == CE_PANIC)
	panic (tmp);

      printf ("%s", tmp);
    }
#if 0
  /* This may cause a crash under SMP */
  if (sound_started)
    store_msg (tmp);
#endif

  va_end (ap);
}

static int
grow_array(oss_device_t *osdev, oss_cdev_t ***arr, int *size, int increment)
{
	oss_cdev_t **old=*arr, **new = *arr;
	int old_size = *size;
	int new_size = *size;
		
	new_size += increment;

	if ((new=PMALLOC(osdev, new_size * sizeof (oss_cdev_t *)))==NULL)
	   return 0;

	memset(new, 0, new_size * sizeof(oss_cdev_t *));
	if (old != NULL)
	   memcpy(new, old, old_size * sizeof(oss_cdev_t *));

	*size = new_size;
	*arr = new;

	if (old != NULL)
	   PMFREE(osdev, old);

	return 1;
}

static void
register_chrdev(oss_cdev_t *cdev, char *name)
{
    if (iosDevAdd ((void *)cdev, name, oss_driver_num) == ERROR)
      {
	cmn_err (CE_WARN, "Failed to add device %s\n", name);
      }
}

void
oss_install_chrdev (oss_device_t * osdev, char *name, int dev_class,
		    int instance, oss_cdev_drv_t * drv, int flags)
{
/*
 * oss_install_chrdev creates a character device (minor). However if
 * name==NULL the device will not be exported (made visible to userland
 * clients).
 */

  int num;
  oss_cdev_t *cdev = NULL;

  if (dev_class != OSS_DEV_STATUS)
    if (oss_expired && instance > 0)
      return;

      if (oss_num_cdevs >= OSS_MAX_CDEVS)
	{
	   if (!grow_array(osdev, &oss_cdevs, &oss_max_cdevs, 100))
	   {
	  	cmn_err (CE_WARN, "Out of minor numbers.\n");
	  	return;
	   }
	}

      if ((cdev = PMALLOC (NULL, sizeof (*cdev))) == NULL)
	{
	  cmn_err (CE_WARN, "Cannot allocate character device desc.\n");
	  return;
	}

      num = oss_num_cdevs++;

  memset (cdev, 0, sizeof (*cdev));
  cdev->dev_class = dev_class;
  cdev->instance = instance;
  cdev->d = drv;
  cdev->osdev = osdev;
  if (name != NULL)
    strncpy (cdev->name, name, sizeof (cdev->name));
  else
    strcpy (cdev->name, "NONE");
  cdev->name[sizeof (cdev->name) - 1] = 0;
  oss_cdevs[num] = cdev;

/*
 * Export the device only if name != NULL
 */
  if (name != NULL)
    {
      strcpy (cdev->name, name);
      register_chrdev (cdev, name);
    }
}

int
oss_find_minor (int dev_class, int instance)
{
  int i;

  for (i = 0; i < oss_num_cdevs; i++)
    {
      if (oss_cdevs[i]->d != NULL && oss_cdevs[i]->dev_class == dev_class
	  && oss_cdevs[i]->instance == instance)
	return i;
    }

  return OSS_ENXIO;
}

static inline int
cpy_file (int mode, struct fileinfo *fi)
{
  fi->mode = 0;
  fi->acc_flags = mode;

  if ((fi->acc_flags & O_ACCMODE) == O_RDWR)
    fi->mode = OPEN_READWRITE;
  if ((fi->acc_flags & O_ACCMODE) == O_RDONLY)
    fi->mode = OPEN_READ;
  if ((fi->acc_flags & O_ACCMODE) == O_WRONLY)
    fi->mode = OPEN_WRITE;

  return fi->mode;
}

static void *
ossOpen (oss_cdev_t *cdev, char *reminder, int mode)
{
  int tmpdev, retval;
  struct fileinfo fi;

  cpy_file (mode, &fi);

  DDB (cmn_err
       (CE_CONT, "ossOpen(%p): %s, class=%d, instance=%d\n", cdev,
	cdev->name, cdev->dev_class, cdev->instance));

  if (cdev->d->open == NULL)
    {
      errnoSet(ENODEV);
      return (void*)ERROR;
    }

  tmpdev = -1;
  retval =
    cdev->d->open (cdev->instance, cdev->dev_class, &fi, 0, 0, &tmpdev);

  if (retval < 0)
    {
      errnoSet(-retval);
      return (void*)ERROR;
    }

  if (tmpdev != -1)
  {
     if (tmpdev >= 0 && tmpdev < oss_num_cdevs)
        {
	     cdev = oss_cdevs[tmpdev];
        }
     else
        {
      		errnoSet(ENODEV);
      		return (void*)ERROR;
	}
  }

  errnoSet (0);
  memcpy(&cdev->file, &fi, sizeof(struct fileinfo));

  return cdev;
}

static int
ossClose (oss_cdev_t *cdev)
{
  if (cdev->d->close == NULL)
    {
      return OK;
    }

  cdev->d->close (cdev->instance, &cdev->file);

  return OK;
}

static int
ossRead (oss_cdev_t *cdev, char *buf, int count)
{
  int err, len;
  uio_t uio;

  if (cdev->d->read == NULL)
    {
      errnoSet (ENXIO);
      return ERROR;
    }

  if ((err = oss_create_uio (&uio, buf, count, UIO_READ, 0)) < 0)
    {
      errnoSet (-err);
      return ERROR;
    }

  len = cdev->d->read (cdev->instance, &cdev->file, &uio, count);

  if (len >= 0)
     return len;

  errnoSet (-len);
  return ERROR;
}

static int
ossWrite (oss_cdev_t *cdev, char *buf, int count)
{
  int err, len;
  uio_t uio;

  if (cdev->d->write == NULL)
    {
      errnoSet (ENXIO);
      return ERROR;
    }

  if ((err = oss_create_uio (&uio, buf, count, UIO_WRITE, 0)) < 0)
    {
      errnoSet (-err);
      return ERROR;
    }

  len = cdev->d->write (cdev->instance, &cdev->file, &uio, count);

  if (len >= 0)
     return len;

  errnoSet (-len);
  return ERROR;
}

static int
ossIoctl (oss_cdev_t *cdev, int cmd, int *arg)
{
  int err;

  if (cdev->d->ioctl == NULL)
  {
      errnoSet (ENXIO);
      return ERROR;
  }

  if ((err = cdev->d->ioctl (cdev->instance, &cdev->file, cmd, (ioctl_arg) arg)) < 0)
    {
      errnoSet (-err);
      return ERROR;
    }
  return OK;
}

int
ossDrv (void)
{
  if (oss_driver_num != ERROR)
    {
      cmn_err (CE_WARN, "OSS is already running\n");
      return -1;
    }

#ifdef LICENSED_VERSION
  if (!oss_license_handle_time (oss_get_time ()))
    {
      cmn_err (CE_WARN, "This version of Open Sound System has expired\n");
      cmn_err (CE_CONT,
	       "Please download the latest version from www.opensound.com\n");
      oss_expired = 1;
      return -1;
    }
#endif

  oss_driver_num = iosDrvInstall ((FUNCPTR) NULL,	/* create */
				  (FUNCPTR) NULL,	/* delete */
				  (FUNCPTR) ossOpen, (FUNCPTR) ossClose, (FUNCPTR) ossRead, (FUNCPTR) ossWrite, (FUNCPTR) ossIoctl	/* ioctl */
    );
  if (oss_driver_num == ERROR)
    {
      cmn_err (CE_WARN, "Module osscore failed to install\n");
      return -1;
    }

  if ((core_osdev =
       osdev_create (NULL, DRV_UNKNOWN, 0, "osscore", NULL)) == NULL)
    {
      oss_cmn_err (CE_WARN, "Failed to allocate OSDEV structure\n");
      return -1;
    }
  oss_register_device (core_osdev, "OSS core services");

  oss_common_init (core_osdev);

  return oss_driver_num;
}

int
ossDrvRemove (void)
{
#if 1

  return ERROR;
#else
  int i;

  if (oss_driver_num == ERROR)
    return 0;

  for (i = 0; i < SND_NDEVS; i++)
    if (oss_files[i].valid)
      {
	iosDevDelete (&oss_files[i].devHdr);
	oss_files[i].valid = 0;
      }

  if (iosDrvRemove (oss_driver_num, FALSE) == ERROR)
    {
      cmn_err (CE_WARN, "Driver busy - cannot remove.\n");
      return ERROR;
    }

  // TODO
  oss_unload_drivers ();

  oss_driver_num = ERROR;	/* Mark it free */
  return OK;
#endif
}
