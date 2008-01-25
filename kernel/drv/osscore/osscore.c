/*
 * Purpose: OSS core pseudo driver (for Solaris)
 *
 * The osscore driver is used under Solaris to load the configuration settings 
 * (osscore.conf) and to install the /dev/sndstat device.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2007. All rights reserved.

#include "osscore_cfg.h"

int
osscore_attach (oss_device_t * osdev)
{
  oss_register_device (osdev, "OSS common devices");
  install_sndstat (osdev);
  install_dev_mixer (osdev);
  return 1;
}

int
osscore_detach (oss_device_t * osdev)
{
  if (oss_disable_device (osdev) < 0)
    return 0;

  oss_unregister_device (osdev);

  return 1;
}
