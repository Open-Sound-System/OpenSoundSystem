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

/*
 * OSS specific includes. Use correct -I setting when compiling. Typically
 * -I/usr/lib/oss/include/sys or -I/usr/include/sys
 */
#include <soundcard.h>
#include <oss_userdev_exports.h>

#define SERVER_DEVNAME		"/dev/oss/oss_userdev0/server"

int server_fd = -1;	/* File descriptor for the server side device. */

int
main(int argc, char *argv[])
{
	userdev_create_t crea = {0};

	if ((server_fd = open(SERVER_DEVNAME, O_RDWR, 0))==-1)
	{
		perror(SERVER_DEVNAME);
		exit(-1);
	}

	strcpy(crea.name, "Acme test");
	crea.flags = USERDEV_F_VMIX_ATTACH;

	if (ioctl(server_fd, USERDEV_CREATE_INSTANCE, &crea)==-1)
	{
		perror("USERDEV_CREATE_INSTANCE");
		exit(-1);
	}

printf("Created instance, devnode=%s\n", crea.devnode);
	exit(0);
}
