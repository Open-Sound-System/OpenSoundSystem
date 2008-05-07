/*
 * Purpose: Definitions for internal use by OSS
 *
 * Definitions for private use by the ossctl program. Everything defined
 * in this file is likely to change without notice between OSS versions.
 *
 * This file will probably be removed from OSS in the near future.
 *
 */
#define COPYING41 Copyright (C) Hannu Savolainen and Dev Mazumdar 1997-2007. All rights reserved.

#ifndef _INTERNAL_H_
#define _INTERNAL_H_

#define OSS_MAXERR 200
typedef struct
{
  int nerrors;
  int errors[OSS_MAXERR];
  int error_parms[OSS_MAXERR];
}
oss_error_info;
#define BOOTERR_BAD_PCIIRQ				  1
#define BOOTERR_AC97CODEC				  2
#define BOOTERR_IRQSTORM				  3
#define BOOTERR_BIGMEM					  4

extern oss_error_info oss_booterrors;

#if 0
typedef struct
{
  int mode;			/* OPEN_READ and/or OPEN_WRITE */
  oss_devlist_t devlist;
}
oss_reroute_t;
#endif

typedef struct
{
/*
 * Private structure for renumbering legacy dsp, mixer and MIDI devices
 */
  int n;
  short map[OSS_MAX_CDEVS];
} oss_renumber_t;
/*
 * Some internal use only ioctl calls ('X', 200-255)
 */
#if 0
#define OSSCTL_GET_REROUTE	__SIOWR('X', 200, oss_reroute_t)
#define OSSCTL_SET_REROUTE	__SIOW ('X', 200, oss_reroute_t)
#endif

#ifdef APPLIST_SUPPORT
/*
 * Application redirection list for audio.c.
 */
typedef struct
{
  char name[32 + 1];		/* Command name (such as xmms) */
  int mode;			/* OPEN_READ|OPEN_WRITE */
  int dev;			/* "Target" audio device number */
  int open_flags;		/* Open flags to be passed to oss_audio_open_engine */
} app_routing_t;

#define APPLIST_SIZE	64
extern app_routing_t oss_applist[APPLIST_SIZE];
extern int oss_applist_size;

#define OSSCTL_RESET_APPLIST	__SIO  ('X', 201)
#define OSSCTL_ADD_APPLIST	__SIOW ('X', 201, app_routing_t)
#endif

/*
 * Legacy device file numbering calls
 */
#define OSSCTL_RENUM_AUDIODEVS	__SIOW ('X', 202, oss_renumber_t)
#define OSSCTL_RENUM_MIXERDEVS	__SIOW ('X', 203, oss_renumber_t)
#define OSSCTL_RENUM_MIDIDEVS	__SIOW ('X', 204, oss_renumber_t)

#ifdef DO_TIMINGS
#define DFLAG_ALL		0x00000001
#define DFLAG_PROFILE		0x00000002
/*
 * Time counters 
 */
#define DF_IDDLE	0
#define DF_WRITE	1
#define DF_READ		2
#define DF_INTERRUPT	3
#define DF_SLEEPWRITE	4
#define DF_SLEEPREAD	5
#define DF_SRCWRITE	6
#define DF_SRCREAD	7
#define DF_NRBINS	8
#endif

#endif
