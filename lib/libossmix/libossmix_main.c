#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2009. All rights reserved.
/*
 * Main module for libossmix
 */
#include <stdio.h>
#include <soundcard.h>

#include "libossmix.h"
#include "libossmix_impl.h"

static ossmix_driver_t *mixer_driver=NULL;
int mixlib_trace=0;
static int num_mixers=0;

int
ossmix_init(void)
{
	if (mixlib_trace > 0)
	fprintf(stderr, "ossmix_init() called\n");
	return 0;
}

void
ossmix_close(void)
{
	if (mixlib_trace > 0)
	fprintf(stderr, "ossmix_close() called\n");
}

int
ossmix_connect(const char *hostname, int port)
{
	if (mixlib_trace > 0)
	fprintf(stderr, "ossmix_connect(%s, %d)) called\n", hostname, port);

	if (hostname==NULL)
	   mixer_driver=&ossmix_local_driver;
	else
	   mixer_driver=&ossmix_tcp_driver;

	return mixer_driver->connect(hostname, port);
}

int
ossmix_get_fd(ossmix_select_poll_t *cb)
{
	return mixer_driver->get_fd(cb);
}

void
ossmix_disconnect(void)
{
	if (mixlib_trace > 0)
	fprintf(stderr, "ossmix_disconnect() called\n");

	mixer_driver->disconnect();
}

int
ossmix_get_nmixers(void)
{
	if (mixlib_trace > 0)
	fprintf(stderr, "ossmix_get_nmixes() called\n");
	return (num_mixers=mixer_driver->get_nmixers());
}

int
ossmix_get_mixerinfo(int mixernum, oss_mixerinfo *mi)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_get_mixerinfo: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}

	return mixer_driver->get_mixerinfo(mixernum, mi);
}

int
ossmix_open_mixer(int mixernum)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_open_mixer: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}
	return mixer_driver->open_mixer(mixernum);
}

void
ossmix_close_mixer(int mixernum)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_close_mixer: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return;
	}

	mixer_driver->close_mixer(mixernum);
}

int
ossmix_get_nrext(int mixernum)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_get_nrext: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}
	return mixer_driver->get_nrext(mixernum);
}

int
ossmix_get_nodeinfo(int mixernum, int node, oss_mixext *ext)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_get_nodeinfo: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}
	return mixer_driver->get_nodeinfo(mixernum, node, ext);
}

int
ossmix_get_enuminfo(int mixernum, int node, oss_mixer_enuminfo *ei)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_get_enuminfo: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}
	return mixer_driver->get_enuminfo(mixernum, node, ei);
}

int
ossmix_get_description(int mixernum, int node, oss_mixer_enuminfo *desc)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_get_description: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}
	return mixer_driver->get_description(mixernum, node, desc);
}

int
ossmix_get_value(int mixernum, int node, int timestamp)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_get_value: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return -1;
	}
	return mixer_driver->get_value(mixernum, node, timestamp);
}

void
ossmix_set_value(int mixernum, int node, int timestamp, int value)
{
	if (mixernum >= num_mixers)
	{
	   fprintf(stderr, "ossmix_set_value: Bad mixer number (%d >= %d)\n",
			   mixernum, num_mixers);
	   return;
	}

	mixer_driver->set_value(mixernum, node, timestamp, value);
}
