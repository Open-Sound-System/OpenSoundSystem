/*
 * Sample server program that uses the oss_userdev driver.
 *
 * Copyright (C) 4Front Technologies. Released under the BSD license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

/*
 * OSS specific includes. Use correct -I setting when compiling. Typically
 * -I/usr/lib/oss/include/sys or -I/usr/include/sys
 */
#include <soundcard.h>
#include <oss_userdev_exports.h>

#define SERVER_DEVNAME		"/dev/oss/oss_userdev0/server"

int server_fd = -1;	/* File descriptor for the server side device. */

static void
terminator(int sig)
{
	wait();
	exit(0);
}

int
main(int argc, char *argv[])
{
	userdev_create_t crea = {0};
	char cmd[512];

	int rate = 48000;
	int fmt = AFMT_S16_LE;
	int channels = 2;
	int tmp;
	int fragsize=0;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <command>\n", argv[0]);
		exit(-1);
	}

printf("SERVER_DEVNAME=%s\n", SERVER_DEVNAME);
	if ((server_fd = open(SERVER_DEVNAME, O_RDWR, 0))==-1)
	{
		perror(SERVER_DEVNAME);
		exit(-1);
	}

	strcpy(crea.name, "Acme test");
	crea.flags = USERDEV_F_VMIX_ATTACH;
	crea.match_method = UD_MATCH_PGID;
	crea.match_key = setpgrp();
	crea.poll_interval = 10; /* In milliseconds */
printf("PGID=%d\n", crea.match_key);

	if (ioctl(server_fd, USERDEV_CREATE_INSTANCE, &crea)==-1)
	{
		perror("USERDEV_CREATE_INSTANCE");
		exit(-1);
	}

	tmp=0;
	ioctl(server_fd, SNDCTL_DSP_COOKEDMODE, &tmp); /* Turn off conversions */

	if (ioctl(server_fd, SNDCTL_DSP_SETFMT, &fmt)==-1)
	   perror("SNDCTL_DSP_SETFMT");

	if (ioctl(server_fd, SNDCTL_DSP_CHANNELS, &channels)==-1)
	   perror("SNDCTL_DSP_CHANNELS");

	if (ioctl(server_fd, SNDCTL_DSP_SPEED, &rate)==-1)
	   perror("SNDCTL_DSP_SPEED");

	if (ioctl(server_fd, SNDCTL_DSP_GETBLKSIZE, &fragsize)==-1)
	   fragsize = 1024;

printf("Fragment size = %d bytes\n", fragsize);

printf("Created instance, devnode=%s\n", crea.devnode);

	if (fork())
	{
		/*
		 * Server side code. In this case we have just a simple echo loop
		 * that writes back everything everything received from the client side.
		 */
		int l;

		char *buffer;
		signal(SIGCHLD, terminator);

		buffer = malloc (fragsize);
		memset(buffer, 0, fragsize);

		write(server_fd, buffer, fragsize);
		write(server_fd, buffer, fragsize);

		while ((l=read(server_fd, buffer, fragsize))>0)
		{
			if (write(server_fd, buffer, l)!=l)
			{
				perror("write");
				exit(-1);
			}
		}

		exit(0);
	}

	sprintf(cmd, "OSS_AUDIODEV=%s", crea.devnode);
	putenv(cmd);

	sprintf(cmd, argv[1], crea.devnode, crea.devnode, crea.devnode);
	printf("Running '%s'\n", cmd);

	system(cmd);
	exit(0);
}
