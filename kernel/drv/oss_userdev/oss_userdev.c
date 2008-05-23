/*
 * Purpose: Kernel space support module for user land audio/mixer drivers
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

#include "oss_userdev_cfg.h"
#include "userdev.h"

oss_device_t *userdev_osdev = NULL;

/*
 * Global device lists and the mutex that protects them.
 */
oss_mutex_t userdev_global_mutex;
userdev_devc_t *userdev_active_device_list = NULL;

int
oss_userdev_attach (oss_device_t * osdev)
{
  int i;

  userdev_osdev = osdev;

  osdev->devc = NULL;
  MUTEX_INIT (osdev, userdev_global_mutex, MH_DRV);

  oss_register_device (osdev, "User space audio driver subsystem");

  attach_control_device(void);

  return 1;
}

int
oss_userdev_detach (oss_device_t * osdev)
{
  userdev_devc_t *devc;
  int i;

  if (oss_disable_device (osdev) < 0)
    return 0;

  devc = userdev_active_device_list;

  while (devc != NULL)
  {
	  userdev_devc_t *next = devc->next_instance;

	  delete_device_pair(devc);

	  devc = next;
  }

  oss_unregister_device (osdev);

  MUTEX_CLEANUP(userdev_global_mutex);

  return 1;
}
