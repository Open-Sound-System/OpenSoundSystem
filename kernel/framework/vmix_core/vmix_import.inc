#ifdef CONFIG_OSS_VMIX_FLOAT
/*
 * Purpose: Recording device to local input buffer import routine for vmix
 */
#define COPYING3 Copyright (C) Hannu Savolainen and Dev Mazumdar 2006-2007. All rights reserved.

int i, ch;
float vol;

vol = vmix_db_table[eng->outvol / 5];

for (ch = 0; ch < channels; ch++)
  {
    float vu;
    float *chbuf;

    vu = eng->vu[ch % 2];
    vu = vu / 255.0;

    op = (SAMPLE_TYPE *) inbuf;
    op += ch;

    chbuf = chbufs[ch];

    for (i = 0; i < samples; i++)
      {
	float tmp;

#if 0 && defined(SINE_DEBUG)
	/* Generate internal sine wave test signal */
	tmp = 0;
	if (ch < 2)
	  {
	    tmp = sine_table[sine_phase[ch]];
	    sine_phase[ch] = (sine_phase[ch] + 1) % SINE_SIZE;
	  }
#else
	tmp = VMIX_BYTESWAP (*op);
	tmp /= SAMPLE_RANGE;
	tmp *= vol;

	if (tmp < -1.0)
	  tmp = -1.0;
	else if (tmp > 1.0)
	  tmp = 1.0;

#endif
	op += channels;

	*chbuf++ = tmp;

	/* VU meter */
	if (tmp < 0.0)
	  tmp = -tmp;
	if (tmp > vu)
	  vu = tmp;
      }

    if (ch < 2)
      {
	vu = vu * 255.0;
	eng->vu[ch] = (int)vu;
      }
  }
#else
#include "vmix_import_int.inc"
#endif
