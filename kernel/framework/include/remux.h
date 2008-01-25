/*
 * Purpose: Definitions for the audio remux engine
 *
 * This header file is to be included in all drivers that use the remux engine
 * for multi channel audio.
 */

#define COPYING17 Copyright (C) Hannu Savolainen and Dev Mazumdar 1996-2007. All rights reserved.

extern void
remux_install (char *name, oss_device_t * osdev, int frontdev, int reardev,
	       int center_lfe_dev, int surrounddev);

#define MAX_SUBDEVS	4

typedef struct remux_devc
{
  oss_device_t *osdev;
  oss_mutex_t mutex;

  int audio_dev;

  int n_physdevs;
  int physdev[MAX_SUBDEVS];

  int maxchannels;
  struct fileinfo finfo[MAX_SUBDEVS];

  int open_mode;
  int channels;
  int speed;
} remux_devc;
