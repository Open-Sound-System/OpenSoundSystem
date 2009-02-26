#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2009. All rights reserved.
/*
 * Local driver for libossmix
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <soundcard.h>

#include "libossmix.h"
#include "libossmix_impl.h"

static int global_fd=-1;

#define MAX_MIXERS 256
static int mixer_fd[MAX_MIXERS];

static int
local_connect(const char *hostname, int port)
{
  char *devmixer;
  int i;

	if (mixlib_trace > 0)
fprintf(stderr, "Entered local_connect()\n");

  for (i=0;i<MAX_MIXERS;i++)
	  mixer_fd[i] = -1;

  if ((devmixer=getenv("OSS_MIXERDEV"))==NULL)
     devmixer = "/dev/mixer";

/*
 *	Open the mixer device
 */
  if ((global_fd = open (devmixer, O_RDWR, 0)) == -1)
    {
      perror (devmixer);
      return -1;
    }

	return 0;
}

static void
local_disconnect(void)
{
	if (mixlib_trace > 0)
fprintf(stderr, "Entered local_disconnect()\n");

	if (global_fd >= 0)
	   close(global_fd);

	global_fd=-1;
}

static int
local_get_nmixers(void)
{
	oss_sysinfo si;

	if (ioctl(global_fd, SNDCTL_SYSINFO, &si)==-1)
	{
		perror("SNDCTL_SYSINFO");
		return -1;
	}

	return si.nummixers;
}

static int
local_get_mixerinfo(int mixernum, oss_mixerinfo *mi)
{
	mi->dev=mixernum;

	if (ioctl(global_fd, SNDCTL_MIXERINFO, mi)==-1)
	{
		perror("SNDCTL_MIXERINFO");
		return -1;
	}

	return 0;
}

static int
local_open_mixer(int mixernum)
{
	oss_mixerinfo mi;

	if (mixer_fd[mixernum] > -1)
	   return 0;

	if (ossmix_get_mixerinfo(mixernum, &mi)<0)
	   return -1;

//fprintf(stderr, "local_open_mixer(%d: %s)\n", mixernum, mi.devnode);

	if ((mixer_fd[mixernum] = open(mi.devnode, O_RDWR, 0))==-1)
	{
		perror(mi.devnode);
		return -1;
	}

	return 0;
}

static void
local_close_mixer(int mixernum)
{
//fprintf(stderr, "local_close_mixer(%d)\n", mixernum);

	if (mixer_fd[mixernum] == -1)
	   return;

	close(mixer_fd[mixernum]);
	mixer_fd[mixernum] = -1;
}

static int
local_get_nrext(int mixernum)
{
	int n=-1;

	if (ioctl(mixer_fd[mixernum], SNDCTL_MIX_NREXT, &n)==-1)
	{
		perror("SNDCTL_MIX_NREXT");
		return -1;
	}

	return n;
}

static int
local_get_nodeinfo(int mixernum, int node, oss_mixext *ext)
{
	ext->dev=mixernum;
	ext->ctrl=node;

	if (ioctl(mixer_fd[mixernum], SNDCTL_MIX_EXTINFO, ext)==-1)
	{
		perror("SNDCTL_MIX_EXTINFO");
		return -1;
	}

	return 0;
}

static int
local_get_enuminfo(int mixernum, int node, oss_mixer_enuminfo *ei)
{
	ei->dev=mixernum;
	ei->ctrl=node;

	if (ioctl(mixer_fd[mixernum], SNDCTL_MIX_ENUMINFO, ei)==-1)
	{
		perror("SNDCTL_MIX_ENUMINFO");
		return -1;
	}

	return 0;
}

static int
local_get_description(int mixernum, int node, oss_mixer_enuminfo *desc)
{
	desc->dev=mixernum;
	desc->ctrl=node;

	if (ioctl(mixer_fd[mixernum], SNDCTL_MIX_DESCRIPTION, desc)==-1)
	{
		perror("SNDCTL_MIX_DESCRIPTION");
		return -1;
	}

	return 0;
}

static int
local_get_value(int mixernum, int ctl, int timestamp)
{
	oss_mixer_value val;

	val.dev=mixernum;
	val.ctrl=ctl;
	val.timestamp=timestamp;

	if (ioctl(mixer_fd[mixernum], SNDCTL_MIX_READ, &val)==-1)
	{
		perror("SNDCTL_MIX_READ");
		return -1;
	}

	return val.value;
}

static void
local_set_value(int mixernum, int ctl, int timestamp, int value)
{
	oss_mixer_value val;

	val.dev=mixernum;
	val.ctrl=ctl;
	val.timestamp=timestamp;
	val.value=value;

	if (ioctl(mixer_fd[mixernum], SNDCTL_MIX_WRITE, &val)==-1)
	{
		perror("SNDCTL_MIX_WRITE");
	}
}

ossmix_driver_t ossmix_local_driver =
{
	local_connect,
	local_disconnect,
	local_get_nmixers,
	local_get_mixerinfo,
	local_open_mixer,
	local_close_mixer,
	local_get_nrext,
	local_get_nodeinfo,
	local_get_enuminfo,
	local_get_description,
	local_get_value,
	local_set_value
};
