/*
 * Purpose: Local output buffer to device export routine for vmix (int)
 */
#define COPYING4 Copyright (C) Hannu Savolainen and Dev Mazumdar 2006-2007. All rights reserved.

int i, ch, och;
int vol;

#define RANGE_MAX	((1<<24)-1)
#define RANGE_MIN	-(1<<24)

vol = vmix_db_table[eng->outvol / 5];

for (ch = 0; ch < channels; ch++)
  {
    int vu;
    int *chbuf;

    och = eng->channel_order[ch];
    op = (SAMPLE_TYPE *) outbuf;
    op += och;

    chbuf = chbufs[ch];

    vu = eng->vu[och % 2];
    vu = vu * 65536;

    for (i = 0; i < samples; i++)
      {
	int tmp;

	tmp = *chbuf++;

	tmp = (tmp * vol) / VMIX_VOL_SCALE;

/*
 * Check for clipping. Decrease volume if necessary.
 */
	if (tmp<RANGE_MIN)
           {
	   	tmp=RANGE_MIN;
	        eng->outvol -= 1;
		vol /= 2;
	   }
	else
	   if (tmp>RANGE_MAX)
	      {
	   	tmp=RANGE_MAX;
	        eng->outvol -= 1;
		vol /= 2;
	      }
	
	*op = VMIX_BYTESWAP(INT_EXPORT(tmp));
	op += channels;

	/* VU meter */
	if (tmp < 0)
	  tmp = -tmp;
	if (tmp > vu)
	  vu = tmp;
      }

    if (och < 2)
      {
	vu = vu / 65536;
	eng->vu[och] = vu;
      }
  }
