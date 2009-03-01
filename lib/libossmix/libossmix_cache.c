#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.
/*
 * Settings cache for libossmix
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <soundcard.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#define OSSMIX_REMOTE

#include "libossmix.h"
#include "libossmix_impl.h"

//static int num_mixers=0;
static local_mixer_t *mixers[MAX_TMP_MIXER] = {NULL};

void
mixc_add_node(int mixernum, int node, oss_mixext *ext)
{
	local_mixer_t *lmixer;
	oss_mixext *lnode;
	
	if (mixers[mixernum] == NULL)
	   {
		mixers[mixernum]=lmixer=malloc(sizeof(*lmixer));
		if (lmixer == NULL)
		{
			fprintf(stderr, "mixc_add_node: Out of memory\n");
			exit(EXIT_FAILURE);
		}

		memset(lmixer, 0, sizeof(*lmixer));
	   }
	else
	   lmixer=mixers[mixernum];

	if (ext->ctrl >= lmixer->nrext)
	   lmixer->nrext=ext->ctrl+1;

	if (node >= MAX_TMP_NODES)
	{
	   fprintf(stderr, "mixc_add_node: Node number too large %d\n", node);
	   exit(EXIT_FAILURE);
	}

	lnode = lmixer->nodes[node];

	if (lnode == NULL)
	{
		lmixer->nodes[node]=lnode=malloc(sizeof(*lnode));

		if (lnode==NULL)
		{
			fprintf(stderr, "mixc_get_node: Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}

	memcpy(lnode, ext, sizeof(*ext));

}

oss_mixext *
mixc_get_node(int mixernum, int node)
{
	local_mixer_t *lmixer;
	oss_mixext *lnode;
	
	if (mixers[mixernum] == NULL)
	   {
		   return NULL;
	   }
	lmixer=mixers[mixernum];

	if (node >= MAX_TMP_NODES)
	{
	   fprintf(stderr, "mixc_get_node: Node number too large %d\n", node);
	   exit(EXIT_FAILURE);
	}

	lnode = lmixer->nodes[node];

	return lnode;
}
