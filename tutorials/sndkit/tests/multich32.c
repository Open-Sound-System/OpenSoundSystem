/*
 * Multi channel audio test.
 *
 * NOTE! ***** THIS PROGRAM SEEMS TO BE BROKEN. DON'T USE IT AS A TEMPLATE FOR
 * 	 NEW PROGRAMS. *****
 *
 * This program is intended to test playback of 32 bit samples using 4 or more
 * channels at 48000 Hz. The program plays sine wave pulses sequentially on
 * channels 0 to N-1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

static short sinedata[] = {
  0x9080, 0xb0a0, 0xcdbf, 0xe4d9, 0xf5ed, 0xfdfa, 0xfdff, 0xf5fa,
  0xe4ed, 0xcdd9, 0xb0bf, 0x90a0, 0x6f7f, 0x4f5f, 0x3240, 0x1b26,
  0x0a12, 0x0205, 0x0201, 0x0a05, 0x1b12, 0x3226, 0x4f40, 0x6f5f,
  0x9080, 0xb0a0, 0xcdbf, 0xe4d9, 0xf5ed, 0xfdfa, 0xfdff, 0xf5fa,
  0xe4ed, 0xcdd9, 0xb0bf, 0x90a0, 0x6f7f, 0x4f5f, 0x3240, 0x1b26,
  0x0a12, 0x0205, 0x0201, 0x0a05, 0x1b12, 0x3226, 0x4f40, 0x6f5f,
  0x9080, 0xb0a0, 0xcdbf, 0xe4d9, 0xf5ed, 0xfdfa, 0xfdff, 0xf5fa,
  0xe4ed, 0xcdd9, 0xb0bf, 0x90a0, 0x6f7f, 0x4f5f, 0x3240, 0x1b26,
  0x0a12, 0x0205, 0x0201, 0x0a05, 0x1b12, 0x3226, 0x4f40, 0x6f5f,
  0x9080, 0xb0a0, 0xcdbf, 0xe4d9, 0xf5ed, 0xfdfa, 0xfdff, 0xf5fa,
  0xe4ed, 0xcdd9, 0xb0bf, 0x90a0, 0x6f7f, 0x4f5f, 0x3240, 0x1b26,
  0x0a12, 0x0205, 0x0201, 0x0a05, 0x1b12, 0x3226, 0x4f40, 0x6f5f,
};

int
main (int argc, char *argv[])
{
  char *dev = "/dev/dsp";
  int fd, l, i, n = 0, ch, p = 0, arg, channels;
  int nch = 10;

  int buf[1024];

  if (argc > 1)
    nch = atoi (argv[1]);
  if (argc > 2)
    dev = argv[2];

  if ((fd = open (dev, O_WRONLY, 0)) == -1)
    {
      perror (dev);
      exit (-1);
    }

  arg = nch;
  if (ioctl (fd, SNDCTL_DSP_CHANNELS, &arg) == -1)
    perror ("SNDCTL_DSP_CHANNELS");
  channels = arg;
  fprintf (stderr, "Channels %d\n", arg);

  arg = AFMT_S32_LE;
  if (ioctl (fd, SNDCTL_DSP_SETFMT, &arg) == -1)
    perror ("SNDCTL_DSP_SETFMT");
  fprintf (stderr, "Format %x\n", arg);

  arg = 44100;
  if (ioctl (fd, SNDCTL_DSP_SPEED, &arg) == -1)
    perror ("SNDCTL_DSP_SPEED");
  printf ("Using sampling rate %d\n", arg);

  l = sizeof (sinedata) / 2;

  while (1)
    {
      for (ch = 0; ch < channels; ch++)
	{
#if 0
	  i = n / (ch + 1);
	  i %= l;
	  buf[p] = sinedata[i] << 16;
#else
	  i = n % l;
	  if (((n / 4800) % channels) == ch)
	    buf[p] = sinedata[i] << 16;
	  else
	    buf[p] = 0;
#endif
	  p++;

	  if (p >= 1024)
	    {
	      if (write (fd, buf, p * 4) != p * 4)
		perror ("write");
	      p = 0;
	    }
	}
      n++;
    }

  exit (0);
}
