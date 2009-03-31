/*
  Purpose: This program has been used to verify that some of the ioctl calls work
 * Copyright (C) 4Front Technologies, 2009. Released under GPLv2/CDDL.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <soundcard.h>
#ifdef _AIX
#include <sys/select.h>
#endif

typedef void (*measure_f_t)(int fd);

static void measure_getoptr(int fd);

measure_f_t measure_f = measure_getoptr;

char *dspdev = "/dev/dsp";
int mode = 0;
int fd = -1;
int speed = 48000;
int bits=16;
int fmt = AFMT_S16_LE;
int channels = 2;
unsigned char silence = 0;
int fragsize = 0;	/* Use default */
int fragcount = 0x7fff; /* Unlimited */
int write_size = 4096;
int write_byte = 0;

int data_rate, buffer_size;

static void
player (int fd)
{
	char *buffer;

	if ((buffer = malloc (write_size)) == NULL)
	{
		fprintf (stderr, "Out of memory\n");
		exit (EXIT_FAILURE);
	}

	memset (buffer, silence, write_size);

	while (1)
	{
		if (write (fd, buffer, write_size) != write_size)
		{
			perror ("write");
			exit (EXIT_FAILURE);
		}

		measure_f (fd);

		write_byte += write_size;
	}
}

static void
print_spacing(int i)
{
	if ((i % 10) == 0)
	{
		printf ("%d", i / 10);
		return;
	}

	printf (".");
}

static
void measure_getoptr(int fd)
{
	count_info ci;
	int i, n;

	if (ioctl (fd, SNDCTL_DSP_GETOPTR, &ci) == -1)
	{
		perror("SNDCTL_DSP_GETOPTR");
		exit (EXIT_FAILURE);
	}

	n = (100 * ci.ptr+ci.ptr) / buffer_size;
	
	printf ("Write byte %8d, t=%8d ms, p=%6d : ", write_byte, 1000 * write_byte / data_rate, ci.ptr);

	for (i=0;i < n; i++)
	    print_spacing (i);
	printf ("*");
	for (i=n+1;i <100; i++)
	    print_spacing (i);
	printf ("%%\n");
	fflush (stdout);
}

int
main (int argc, char *argv[])
{
	int i;
	int tmp;
	audio_buf_info bi;

	while ((i = getopt(argc, argv, "d:m:s:c:b:f:n:")) != EOF)
	switch (i)
	{
	case 'd':
		dspdev = optarg;
		break;

	case 'm':
		mode = atoi(optarg);
		break;

	case 's':
		speed = atoi(optarg);
		if (speed < 200)
		   speed *= 1000;
		break;

	case 'c':
		channels = atoi(optarg);
		break;

	case 'b':
		bits = atoi(optarg);
		break;

	case 'f':
		fragsize = atoi(optarg);
		break;

	case 'n':
		fragcount = atoi(optarg);
		break;
	}

	switch (bits)
	{
	case 8:		bits = AFMT_U8; silence = 0x80; break;
	case 16:	bits = AFMT_S16_NE; break;
	case 32:	bits = AFMT_S32_LE; break;
	default:
		fprintf(stderr, "Bad numer of bits %d\n", bits);
		exit (EXIT_FAILURE);
	}

	if ((fd=open(dspdev, O_WRONLY, 0)) == -1)
	{
		perror (dspdev);
		exit (EXIT_FAILURE);
	}

	if (fragsize != 0)
	{
		fragsize = (fragsize & 0xffff) | ((fragcount & 0x7fff) << 16);
		ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &fragsize); /* Ignore errors */
	}

	tmp = fmt;
	if (ioctl (fd, SNDCTL_DSP_SETFMT, &tmp) == -1)
	{
		perror("SNDCTL_DSP_SETFMT");
		exit (EXIT_FAILURE);
	}

	if (tmp != fmt)
	{
		fprintf (stderr, "Failed to select the requested sample format (%x, %x)\n", fmt, tmp);
		exit (EXIT_FAILURE);
	}

	tmp = channels;
	if (ioctl (fd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
	{
		perror("SNDCTL_DSP_CHANNELS");
		exit (EXIT_FAILURE);
	}

	if (tmp != channels)
	{
		fprintf (stderr, "Failed to select the requested #channels (%d, %d)\n", channels, tmp);
		exit (EXIT_FAILURE);
	}

	tmp = speed;
	if (ioctl (fd, SNDCTL_DSP_SPEED, &tmp) == -1)
	{
		perror("SNDCTL_DSP_SPEED");
		exit (EXIT_FAILURE);
	}

	if (tmp != speed)
	{
		fprintf (stderr, "Failed to select the requested rate (%d, %d)\n", speed, tmp);
		exit (EXIT_FAILURE);
	}

	if (ioctl (fd, SNDCTL_DSP_GETOSPACE, &bi) == -1)
	{
		perror("SNDCTL_DSP_GETOSPACE");
		exit (EXIT_FAILURE);
	}

	buffer_size = bi.fragsize * bi.fragstotal;

	data_rate = speed * channels * (bits / 8);

	printf ("fragsize %d, nfrags %d, total buffer %d (bytes)\n", bi.fragsize, bi.fragstotal, buffer_size);
	write_size = bi.fragsize;

	printf ("Data rate %d bytes / second\n", data_rate);
	printf ("Fragment time %d ms\n", 1000*bi.fragsize / data_rate);
	printf ("Buffer time %d ms\n", 1000*buffer_size / data_rate);

	printf ("\n");
	printf ("Starting test %d\n", mode);
	printf ("\n");

	player(fd);
	exit(0);
}
