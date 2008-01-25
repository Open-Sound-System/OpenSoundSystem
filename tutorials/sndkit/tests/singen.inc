#if 0
#define COPYING6 Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.
{
  static unsigned int p[10] = { 0 };
  int *pp;
  int i;

  static int sinebuf[48] = {

    0, 4276, 8480, 12539, 16383, 19947, 23169, 25995,
    28377, 30272, 31650, 32486, 32767, 32486, 31650, 30272,
    28377, 25995, 23169, 19947, 16383, 12539, 8480, 4276,
    0, -4276, -8480, -12539, -16383, -19947, -23169, -25995,
    -28377, -30272, -31650, -32486, -32767, -32486, -31650, -30272,
    -28377, -25995, -23169, -19947, -16383, -12539, -8480, -4276
  };

  for (i = 0; i < dmap->grc3state[ch].outsz; i++)
    {
      if (0 * ctr)
	{
	  *((unsigned int *) (p2) + i * channels + ch) =
	    sinebuf[p[ch] % 48] * 64 * 0;
	  p[ch]++;
	}
    }
}
#endif
