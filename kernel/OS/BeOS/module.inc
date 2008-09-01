/* -*- c -*-
 * Purpose: OSS module wrapper for BeOS/Haiku
 *
 * This file will be included from the auto-generated drv_cfg.c files. Under
 * UnixWare and OpenServer this will will be compiled during the initial build
 * of OSS (in the development system).
 */
/*
 *
 * This file is part of Open Sound System.
 *
 * Copyright (C) 4Front Technologies 1996-2007.
 *
 * This this source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution. Please contact sales@opensound.com for further info.
 *
 */

#include <PCI.h>

//pci_module_info *gPCI = NULL;
extern pci_module_info *gPCI;

#if DRIVER_TYPE==DRV_VIRTUAL
static oss_device_t *osdev = NULL;
#endif

#if DRIVER_TYPE==DRV_PCI
int DRIVER_PROBE()
{
  oss_device_t *osdev;
  pci_info pcii;
  uint32 pci_index = 0;
  uint32 count = 0;
  int err = ENOENT;
  static int instance = 0;
  FENTRY();
  
  /* while there are more pci devices */
  while ((*gPCI->get_nth_pci_info)(pci_index, &pcii) == B_NO_ERROR)
    {
      int vendor = 0;

      /* if we match a supported vendor */
      while (id_table[vendor].vendor)
        {
          if (id_table[vendor].vendor == pcii.vendor_id &&
              id_table[vendor].product == pcii.device_id)
            {
              dev_info_t *di;
              dprintf("oss: matching pci %04x,%04x\n", pcii.vendor_id, pcii.device_id);
              di = PMALLOC(NULL, sizeof(dev_info_t));
              if (!di)
                break;
              memcpy(di, &pcii, sizeof(pcii));
              if ((osdev =
                   osdev_create (di, DRIVER_TYPE, instance, DRIVER_NICK, NULL)) == NULL)
                {
                  break;
                }

              /* should be called once, but we need an osdev */
              oss_load_options (osdev, local_driver_options);

              if (!DRIVER_ATTACH (osdev))
                {
                  cmn_err (CE_WARN, "Attach failed\n");
                  osdev_delete (osdev);
                  break;
                }

              instance++;
              count++;
              break;
            }
          vendor++;
        }
      /* next pci_info struct, please */
      pci_index++;
    }

  oss_audio_delayed_attach ();

  if (count)
    err = 0;
err:
  FEXITR(err);
  return err;
}
#endif

#if DRIVER_TYPE==DRV_VIRTUAL
int DRIVER_PROBE()
{
  int err = EIO;
  FENTRY();
  if ((osdev =
       osdev_create (NULL, DRIVER_TYPE, 0, DRIVER_NICK, NULL)) == NULL)
    {
      goto err;
    }

  oss_load_options (osdev, local_driver_options);

  if (!DRIVER_ATTACH (osdev))
    {
      cmn_err (CE_WARN, "Attach failed\n");
      osdev_delete (osdev);
      goto err;
    }

  err = 0;
err:
  FEXITR(err);
  return err;
}
#endif






static status_t
stdops(int32 op, ...)
{
  status_t err;
  switch (op)
    {
    case B_MODULE_INIT:
      err = get_module(B_PCI_MODULE_NAME, (module_info **)&gPCI);
      return err;

    case B_MODULE_UNINIT:
      //err = unload_driver(DRIVER_NICK);
      //if (err < B_OK)
      //  return err;
      put_module(B_PCI_MODULE_NAME);
      return B_OK;

    }
  return B_ERROR;
}

oss_drv_module_info DRIVER_MODULE_OBJECT = {
	{
		OSS_MAKE_DRV_MOD_NAME(DRIVER_NICK),
		0,
		stdops,
	},
	DRIVER_PROBE,
	DRIVER_ATTACH,
	DRIVER_DETACH
};

