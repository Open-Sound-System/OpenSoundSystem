#ifdef CONFIG_OSS_VMIX_FLOAT
/*
 * Purpose: Local output buffer to device export routine for vmix (FP version)
 */
#define COPYING4 Copyright (C) Hannu Savolainen and Dev Mazumdar 2006-2007. All rights reserved.

int i, ch;
float vol;

vol = vmix_db_table[eng->outvol / 5];

for (ch = 0; ch < channels; ch++)
  {
    float vu;
    float *chbuf;

    op = (SAMPLE_TYPE *) outbuf;
    op += ch;

    chbuf = chbufs[ch];

    vu = eng->vu[ch % 2];
    vu = vu / 255.0;

    for (i = 0; i < samples; i++)
      {
	float tmp;

#if 0 && defined(SINE_DEBUG)
	tmp = sine_table[sine_phase[ch]];
	sine_phase[ch] = (sine_phase[ch] + 1 + ch / 2) % SINE_SIZE;
#else
	tmp = *chbuf++;
#endif

	tmp *= vol;

/*
 * Check for clipping. Decrease volume if necessary.
 */
	if (tmp < -1.0)
	  {
	    vol /= -tmp;
	    eng->outvol--;
	    tmp = -1.0;
	  }
	else if (tmp > 1.0)
	  {
	    vol /= tmp;
	    eng->outvol--;
	    tmp = 1.0;
	  }

	*op = VMIX_BYTESWAP ((SAMPLE_TYPE) (tmp * SAMPLE_RANGE));
	op += channels;

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
#include "outexport_int.inc"
#endif
