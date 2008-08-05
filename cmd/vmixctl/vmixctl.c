/*
 * Purpose: Utility to control the vmix subsystem of Open Sound System
 */

#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2008. All rights reserved.

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <oss_config.h>
#include <sys/ioctl.h>

char *cmdname=NULL;

#ifndef CONFIG_OSS_VMIX
int
main(int argc, char *argv[])
{
	fprintf (stderr, "%s: Virtual mixer is not included in this version of OSS\n", argv[0]);
	exit(1);
}
#else

static void
usage(void)
{
	fprintf (stderr, "Usage:\n");
	fprintf (stderr, "%s attach [options...] devname\n", cmdname);
	fprintf (stderr, "%s attach devname inputdev\n", cmdname);
	fprintf (stderr, "%s detach devname\n", cmdname);
	fprintf (stderr, "%s rate devname samplerate\n", cmdname);
	fprintf (stderr, "\n");
	fprintf (stderr, "Use ossinfo -a to find out the devname and inputdev parameters\n");
	fprintf (stderr, "Use ossinfo -a -v2 to find out a suitable sample rate.\n");

	exit(-1);
}

static int
find_audiodev(char *fname, int mode, int *fd_)
{
	int fd;
	oss_audioinfo ai;

	if ((fd=*fd_=open(fname, mode | O_EXCL, 0))==-1)
	{
		perror(fname);
		exit(-1);
	}

	ai.dev=-1;
	if (ioctl(fd, SNDCTL_ENGINEINFO, &ai)==-1)
	{
		perror("SNDCTL_ENGINEINFO");
		exit(-1);
	}

	return ai.dev;
}

static int
vmix_attach(int argc, char **argv)
{
	int masterfd, inputfd=-1;
	int masterdev, inputdev=-1;

	vmixctl_attach_t att;

	masterdev=find_audiodev(argv[2], O_WRONLY, &masterfd);

	if (argc>3)
	   inputdev=find_audiodev(argv[3], O_RDONLY, &inputfd);
	   
	att.masterdev=masterdev;
	att.inputdev=inputdev;

	if (ioctl(masterfd, VMIXCTL_ATTACH, &att)==-1)
	{
		perror("VMIXCTL_ATTACH");
		exit(-1);
	}

	fprintf (stderr, "Virtual mixer attached to device.\n");

	return 0;
}

static int
vmix_detach(int argc, char **argv)
{
	int masterfd;
	int masterdev;

	vmixctl_attach_t att;

	masterdev=find_audiodev(argv[2], O_WRONLY, &masterfd);

	att.masterdev=masterdev;
	att.inputdev=-1;

	if (ioctl(masterfd, VMIXCTL_DETACH, &att)==-1)
	{
		perror("VMIXCTL_DETACH");
		exit(-1);
	}

	fprintf (stderr, "Virtual mixer detached from device.\n");

	return 0;
}

static int
vmix_rate(int argc, char **argv)
{
	int masterfd;
	int masterdev;

	vmixctl_rate_t rate;

	if (argc<4)
	{
		usage ();
	}

	masterdev=find_audiodev(argv[2], O_WRONLY, &masterfd);

	rate.masterdev=masterdev;
	rate.rate=atoi(argv[3]);

	if (ioctl(masterfd, VMIXCTL_RATE, &rate)==-1)
	{
		perror("VMIXCTL_RATE");
		exit(-1);
	}

	fprintf (stderr, "Virtual mixer rate change requested.\n");

	return 0;
}

int
main(int argc, char **argv)
{
	cmdname=argv[0];

	if (argc < 3)
	{
		usage ();
	}

	if (strcmp(argv[1], "attach")==0)
	   exit(vmix_attach(argc, argv));

	if (strcmp(argv[1], "detach")==0)
	   exit(vmix_detach(argc, argv));

	if (strcmp(argv[1], "rate")==0)
	   exit(vmix_rate(argc, argv));

	usage();
	exit(0);
}
#endif
