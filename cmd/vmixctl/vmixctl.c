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

#ifndef CONFIG_OSS_VMIX
int
main(int argc, char *argv[])
{
	fprintf (stderr, "%s: Virtual mixer is not included in this version of OSS\n", argv[0]);
	exit(1);
}
#else

static void
usage(char *cmd)
{
	fprintf (stderr, "Usage:\n");
	fprintf (stderr, "%s attach devname\n", cmd);
	fprintf (stderr, "%s attach devname inputdev\n", cmd);
	fprintf (stderr, "%s detach devname\n", cmd);
	fprintf (stderr, "%s rate devname samplerate\n", cmd);
	fprintf (stderr, "\n");
	fprintf (stderr, "Use ossinfo -a to find out the devname and inputdev parameters\n");
	fprintf (stderr, "Use ossinfo -a -v2 to find out the sample rate.\n");

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
	fprintf (stderr, "detach not implemented.\n");
	return 0;
}

static int
vmix_rate(int argc, char **argv)
{
	fprintf (stderr, "rate not implemented.\n");
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 3)
	{
		usage (argv[0]);
	}

	if (strcmp(argv[1], "attach")==0)
	   exit(vmix_attach(argc, argv));

	if (strcmp(argv[1], "detach")==0)
	   exit(vmix_detach(argc, argv));

	if (strcmp(argv[1], "rate")==0)
	   exit(vmix_rate(argc, argv));

	usage(argv[0]);
	exit(0);
}
#endif
