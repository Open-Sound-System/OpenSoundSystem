#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2009. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <soundcard.h>
#include <libossmix.h>

static void
print_enum_list(int mixnum, int ctl)
{
	oss_mixer_enuminfo desc;
	int i;
	
	if (ossmix_get_enuminfo(mixnum, ctl, &desc)<0)
	{
		fprintf(stderr, "ossmix_get_enuminfo() failed\n");
		return;
	}

	for (i=0;i<desc.nvalues;i++)
	{
		if (i>0)printf(" | ");
		printf("%s", desc.strings+desc.strindex[i]);
	}

	printf("\n");
}

static void
print_description(int mixnum, int ctl)
{
	oss_mixer_enuminfo desc;
	
	if (ossmix_get_description(mixnum, ctl, &desc)<0)
	{
		fprintf(stderr, "ossmix_get_description() failed\n");
		return;
	}

	printf("%s\n", desc.strings);
}

int
main(int argc, char *argv[])
{
	int err, i;
	char *host=NULL;
	int port=7777;
	int nmixers=0;
	extern int mixlib_trace;

	//mixlib_trace=1;

	while ((i = getopt(argc, argv, "p:h:")) != EOF)
	switch (i)
	{
	case 'p':
		port = atoi(optarg);
		break;

	case 'h':
		host=optarg;
		break;
	}

	if ((err=ossmix_init())<0)
	{
		fprintf(stderr, "ossmix_init() failed, err=%d\n");
		exit(EXIT_FAILURE);
	}

	if ((err=ossmix_connect(host, port))<0)
	{
		fprintf(stderr, "ossmix_connect() failed, err=%d\n", err);
		exit(EXIT_FAILURE);
	}

	if ((nmixers=ossmix_get_nmixers())<0)
	{
		fprintf(stderr, "ossmix_get_nmixers() failed, err=%d\n", nmixers);
		exit(EXIT_FAILURE);
	}

	printf("Number of mixers=%d\n", nmixers);

	for (i=0;i<nmixers;i++)
	{
		oss_mixerinfo mi;
		int n, ctl;

		if (ossmix_get_mixerinfo(i, &mi)<0)
		{
			fprintf(stderr, "ossmix_get_mixerinfo(%d) failed\n", i);
			exit(EXIT_FAILURE);
		}

		printf("Mixer %2d: %s\n", i, mi.name);

		if (ossmix_open_mixer(i)<0)
		{
			fprintf(stderr, "ossmix_open_mixer(%d) failed\n", i);
			exit(EXIT_FAILURE);
		}

		if ((n=ossmix_get_nrext(i))<0)
		{
			fprintf(stderr, "ossmix_get_nrext(%d) failed, err=\n", i, n);
			exit(EXIT_FAILURE);
		}

		printf("Mixer has %d nodes\n", n);

		for (ctl=0;ctl<n;ctl++)
		{
			oss_mixext node;
			int value=0;

			if (ossmix_get_nodeinfo(i, ctl, &node)<0)
			{
				fprintf(stderr, "ossmix_get_nodeinfo(%d, %d) failed\n",
						i, ctl);
				exit(EXIT_FAILURE);

			}

			if (node.type != MIXT_DEVROOT && node.type != MIXT_GROUP && node.type != MIXT_MARKER)
			if ((value=ossmix_get_value(i, ctl, node.timestamp))<0)
			{
				fprintf(stderr, "ossmix_get_value(%d, %d, %d) failed, err=%d\n",
						i, ctl, node.timestamp, value);
			}

			printf("%3d: %s = 0x%08x\n", ctl, node.extname, value);

			if (node.type == MIXT_ENUM)
			   print_enum_list(i, ctl);

			if (node.flags & MIXF_DESCR)
			   print_description(i, ctl);
			   
		}

		ossmix_close_mixer(i);
	}

printf("Disconnecting\n");
	ossmix_disconnect();

	exit(EXIT_SUCCESS);
}
