/*
 * Purpose: Virtual mixing audio driver
 *
 * This is just a load stub. The actual vmix subsystem has beem moved to
 * the vmix_core directory in the framework.
 *
 * This driver will go away in the future when vmix gets fully integrated
 * with the audio framework.
 *
 * The main purpose of this stub is to load the configuration settings from
 * vmix.conf.
 *
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2007. All rights reserved.

#define VMIX_MAIN
#include "oss_vmix_cfg.h"

/*
 * Config options
 */
extern int vmix_instances;
extern int vmix_disable;

extern int vmix1_masterdev;
extern int vmix1_numoutputs;
extern int vmix1_numloops;
extern int vmix1_inputdev;
extern int vmix1_rate;

extern int vmix2_masterdev;
extern int vmix2_numoutputs;
extern int vmix2_numloops;
extern int vmix2_inputdev;
extern int vmix2_rate;

extern int vmix3_masterdev;
extern int vmix3_numoutputs;
extern int vmix3_numloops;
extern int vmix3_inputdev;
extern int vmix3_rate;

extern int vmix4_masterdev;
extern int vmix4_numoutputs;
extern int vmix4_numloops;
extern int vmix4_inputdev;
extern int vmix4_rate;

extern int vmix5_masterdev;
extern int vmix5_numoutputs;
extern int vmix5_numloops;
extern int vmix5_inputdev;
extern int vmix5_rate;

extern int vmix6_masterdev;
extern int vmix6_numoutputs;
extern int vmix6_numloops;
extern int vmix6_inputdev;
extern int vmix6_rate;

extern int vmix7_masterdev;
extern int vmix7_numoutputs;
extern int vmix7_numloops;
extern int vmix7_inputdev;
extern int vmix7_rate;

extern int vmix8_masterdev;
extern int vmix8_numoutputs;
extern int vmix8_numloops;
extern int vmix8_inputdev;
extern int vmix8_rate;

extern int vmix_core_attach (oss_device_t * osdev);
extern void oss_create_vmix (void *devc, int masterdev, int inputdev,
			     int numoutputs, int numloops, int rate);

int
oss_vmix_attach (oss_device_t * osdev)
{
  void *devc;

  if (vmix_instances < 1)	/* All devices disabled */
    return 1;

  if (vmix_disable)
    return 1;

  if (!vmix_core_attach (osdev))
  {
    /*
     * Return 1 instead of 0 because vmix is probably already running. Returning false would just make the 
     * framework to delete resources that should stay allocated.
     */
    return 1;
  }

  devc = osdev->devc;

  oss_create_vmix (devc, vmix1_masterdev, vmix1_inputdev, vmix1_numoutputs,
		   vmix1_numloops, vmix1_rate);

  if (vmix_instances >= 2)
    oss_create_vmix (devc, vmix2_masterdev, vmix2_inputdev, vmix2_numoutputs,
		     vmix2_numloops, vmix2_rate);

  if (vmix_instances >= 3)
    oss_create_vmix (devc, vmix3_masterdev, vmix3_inputdev, vmix3_numoutputs,
		     vmix3_numloops, vmix3_rate);

  if (vmix_instances >= 4)
    oss_create_vmix (devc, vmix4_masterdev, vmix4_inputdev, vmix4_numoutputs,
		     vmix4_numloops, vmix4_rate);

  if (vmix_instances >= 5)
    oss_create_vmix (devc, vmix5_masterdev, vmix5_inputdev, vmix5_numoutputs,
		     vmix5_numloops, vmix5_rate);

  if (vmix_instances >= 6)
    oss_create_vmix (devc, vmix6_masterdev, vmix6_inputdev, vmix6_numoutputs,
		     vmix6_numloops, vmix6_rate);

  if (vmix_instances >= 7)
    oss_create_vmix (devc, vmix7_masterdev, vmix7_inputdev, vmix7_numoutputs,
		     vmix7_numloops, vmix7_rate);

  if (vmix_instances >= 8)
    oss_create_vmix (devc, vmix8_masterdev, vmix8_inputdev, vmix8_numoutputs,
		     vmix8_numloops, vmix8_rate);
  /* Also update MAX_INSTANCES in vmix.h if adding more instances here */

  return 1;
}

/*ARGSUSED*/
int
oss_vmix_detach (oss_device_t * osdev)
{
/*
 * The vmix core (that is part of the osscommon module) cannot be stopped
 * without unloading the osscommon module.
 *
 * However this vmix pseudo driver can be removed but it leaves vmix still
 * active in the system.
 */
  return 1;			/* Do it unconditionally */
}
