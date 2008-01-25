/*
 * Purpose: Common definitions for the ACME Labs Evil Audio driver.
 *
 * The intial comment usually doesn't contain much information.
 */

/*
 * As in the C sources you need to include a placeholder define for the 
 * copyright notice. To avoid getting multiple define warnings for the COPYING
 * macro the header files should use macro name like COPYING2..COPYING9.
 */

#define COPYING2 Copyright (C) ACME Laboratories  2000-2006. All rights reserved.

/*
 * Each device instance should have a per-device data structure that contains
 * variables common to all sub-devices of the card. By convenntion this
 * structure is called devc. Driver designers may use different terminology.
 * However use of devc is highly recomended in all OSS drivers because it
 * will make maintenance of the code easier.
 */

typedef struct _myossdev_devc_t *myoss_devc_t;

struct _myossdev_devc_t
{
  oss_device_t *osdev;		/* A handle to the device given by the OSS core. */
  oss_mutex_t *mutex;		/* A lock/mutex variable for the device */
};
