/*
 * Purpose: SoftOSS driver - The mixing loop.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 1997. All rights reserved.

#include "softoss_cfg.h"

#if 0
# define PRT_STATUS(v) OUTB(NULL, (v)&0xff, 0x378)
#else
# define PRT_STATUS(v)
#endif

#include "softoss.h"

#define MAXSAMPLE	(32767*256)
#define MINSAMPLE	(-32768*256)

#ifdef USE_EQ
typedef int fixint_t;
#include "eq1.h"

static __inline__ fixint_t
__mulnx (int32_t a, int32_t b)
{
  int64_t c = ((int64_t) a) * b >> 31;
  return (fixint_t) c;
}

#define MACS(a,x,y, r) r = (a) + __mulnx(x,y)
#define MACSINT(a,x,y,r) r = (a) + ((x)*(y))
#define MOVE(a,b) b=(a)
#define ACC3(a,x,y,r) r=(a)+(x)+(y)

typedef struct
{
  fixint_t x0, x1, x2, y0, y1, y2;
}
flt_mem_t;

static __inline__ fixint_t
peaking (softoss_devc * devc, flt_mem_t * b1, flt_mem_t * b2, flt_mem_t * b3,
	 flt_mem_t * b4, const fixint_t * b1_CST, const fixint_t * b2_CST,
	 const fixint_t * b3_CST, const fixint_t * b4_CST, fixint_t VALUE)
{
#define PASS_BAND(N)\
\
    MOVE(N->x1,N->x2);\
    MOVE(N->x0,N->x1);\
    MOVE(VALUE, N->x0);\
\
    MOVE(N->y1,N->y2);\
    MOVE(N->y0,N->y1);\
    MOVE(0,N->y0);\
\
    MACSINT(0,N->x0,0x00001000,N->x0);\
\
    MACS(0,    N->x0,N##_CST[0],N->y0);   /* B0 */   \
    MACS(N->y0, N->x1,N##_CST[1],N->y0);   /* B1  */  \
    MACS(N->y0, N->y1,N##_CST[2],N->y0);   /* -B1 */  \
    MACS(N->y0, N->x2,N##_CST[3],N->y0);   /* B2  */  \
    MACS(N->y0, N->y2,N##_CST[4],N->y0);   /* -A2 */  \
\
    MACSINT(0,N->y0,8,N->y0);\
\
    MACS(0,N->y0,0x00080000,VALUE);

  VALUE = (VALUE * devc->eq_prescale + 128) / 255;
  PASS_BAND (b4);
  PASS_BAND (b3);
  PASS_BAND (b2);
  PASS_BAND (b1);

  ACC3 (0, 44, VALUE, VALUE);	/* DC offset correction */

#if 0
  if (VALUE > MAXSAMPLE)
    VALUE = MAXSAMPLE;
  else if (VALUE < MINSAMPLE)
    VALUE = MINSAMPLE;
#endif

  return VALUE;
}
#endif

#ifdef USE_DCKILL
static __inline__ void
killDC (avoice_info * v, int *l, int *r)
{
  v->DCfeedbackBuf[v->DCptr][0] = *l;
  v->DCfeedbackBuf[v->DCptr][1] = *r;

  v->DCptr++;
  v->DCptr &= DCPOINTS_MASK;

  v->DCfeedbackVal[0] += *l;
  v->DCfeedbackVal[1] += *r;

  /*
   * notice, that different DCptr value is used. The delay is
   * organized due to buffer wraparound
   */
  v->DCfeedbackVal[0] -= v->DCfeedbackBuf[v->DCptr][0];
  v->DCfeedbackVal[1] -= v->DCfeedbackBuf[v->DCptr][1];

  *l -= (v->DCfeedbackVal[0] >> DCPOINTS_FRAC);
  *r -= (v->DCfeedbackVal[1] >> DCPOINTS_FRAC);
}

static __inline__ void
killDC1 (avoice_info * v, int *l)
{
  v->DCfeedbackBuf[v->DCptr][0] = *l;

  v->DCptr++;
  v->DCptr &= DCPOINTS_MASK;

  v->DCfeedbackVal[0] += *l;

  /*
   * notice, that different DCptr value is used. The delay is
   * organized due to buffer wraparound
   */
  v->DCfeedbackVal[0] -= v->DCfeedbackBuf[v->DCptr][0];

  *l -= (v->DCfeedbackVal[0] >> DCPOINTS_FRAC);
}
#else
#define killDC(x,y,z)
#define killDC1(x,y)
#endif

#if !defined(__OpenBSD__)
static __inline__ short
swap16 (short x)
{
  return ((x & 0xff) << 8) | ((x >> 8) & 0xff);
}

static __inline__ int
swap32 (int x)
{
  return ((x & 0x000000ff) << 24) |
    ((x & 0x0000ff00) << 8) |
    ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24);
}
#endif

static void
output_16_bits (softoss_devc * devc, short *buf, int loops, int *front_buf,
		int *rear_buf)
{
  int iloop;
  int tmp, leftvu, rightvu;

  leftvu = rightvu = 0;

  for (iloop = 0; iloop < loops; iloop++)
    {				/* Output the accumulated samples */
      int left = *front_buf++, right = *front_buf++;
      int rearleft = *rear_buf++, rearright = *rear_buf++;

      if (left > MAXSAMPLE)
	left = MAXSAMPLE;
      if (left < MINSAMPLE)
	left = MINSAMPLE;
      if (right > MAXSAMPLE)
	right = MAXSAMPLE;
      if (right < MINSAMPLE)
	right = MINSAMPLE;

      if (rearleft > MAXSAMPLE)
	rearleft = MAXSAMPLE;
      if (rearleft < MINSAMPLE)
	rearleft = MINSAMPLE;
      if (rearright > MAXSAMPLE)
	rearright = MAXSAMPLE;
      if (rearright < MINSAMPLE)
	rearright = MINSAMPLE;

      tmp = left;
      if (tmp < 0)
	tmp = -tmp;
      if (tmp > leftvu)
	leftvu = tmp;
      tmp = right;
      if (tmp < 0)
	tmp = -tmp;
      if (tmp > rightvu)
	rightvu = tmp;

      if (devc->chendian)
	{
	  /* Perform byte swapping */
	  *buf++ = swap16 (left / 256);
	  *buf++ = swap16 (right / 256);
	  if (devc->multich)
	    {
	      int i;

	      *buf++ = swap16 (rearleft / 256);
	      *buf++ = swap16 (rearright / 256);

	      for (i = devc->channels; i < devc->hw_channels; i++)
		*buf++ = 0;
	    }
	}
      else
	{
	  *buf++ = left / 256;
	  *buf++ = right / 256;
	  if (devc->multich)
	    {
	      int i;

	      *buf++ = rearleft / 256;
	      *buf++ = rearright / 256;

	      for (i = devc->channels; i < devc->hw_channels; i++)
		*buf++ = 0;
	    }
	}
    }				/* Output the samples */
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > devc->left_vu)
    devc->left_vu = leftvu;
  if (rightvu > devc->right_vu)
    devc->right_vu = rightvu;

  devc->dev_ptr =
    (devc->dev_ptr +
     loops * sizeof (short) * devc->hw_channels) % devc->dmap->bytes_in_use;
  devc->dmap->user_counter += loops * sizeof (short) * devc->hw_channels;
}

static void
output_32_bits (softoss_devc * devc, int *buf, int loops, int *front_buf,
		int *rear_buf)
{
  int iloop;
  int tmp, leftvu, rightvu;

  leftvu = rightvu = 0;

  for (iloop = 0; iloop < loops; iloop++)
    {				/* Output the accumulated samples */
      int left = *front_buf++, right = *front_buf++;
      int rearleft = *rear_buf++, rearright = *rear_buf++;

      if (left > MAXSAMPLE)
	left = MAXSAMPLE;
      if (left < MINSAMPLE)
	left = MINSAMPLE;
      if (right > MAXSAMPLE)
	right = MAXSAMPLE;
      if (right < MINSAMPLE)
	right = MINSAMPLE;

      if (rearleft > MAXSAMPLE)
	rearleft = MAXSAMPLE;
      if (rearleft < MINSAMPLE)
	rearleft = MINSAMPLE;
      if (rearright > MAXSAMPLE)
	rearright = MAXSAMPLE;
      if (rearright < MINSAMPLE)
	rearright = MINSAMPLE;

      tmp = left;
      if (tmp < 0)
	tmp = -tmp;
      if (tmp > leftvu)
	leftvu = tmp;
      tmp = right;
      if (tmp < 0)
	tmp = -tmp;
      if (tmp > rightvu)
	rightvu = tmp;

      if (devc->chendian)
	{
	  /* Perform byte swapping */
	  *buf++ = swap32 (left * 256);
	  *buf++ = swap32 (right * 256);
	  if (devc->multich)
	    {
	      int i;

	      *buf++ = swap32 (rearleft * 256);
	      *buf++ = swap32 (rearright * 256);

	      for (i = devc->channels; i < devc->hw_channels; i++)
		*buf++ = 0;
	    }
	}
      else
	{
	  *buf++ = left * 256;
	  *buf++ = right * 256;
	  if (devc->multich)
	    {
	      int i;

	      *buf++ = rearleft * 256;
	      *buf++ = rearright * 256;

	      for (i = devc->channels; i < devc->hw_channels; i++)
		*buf++ = 0;
	    }
	}
    }				/* Output the samples */
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > devc->left_vu)
    devc->left_vu = leftvu;
  if (rightvu > devc->right_vu)
    devc->right_vu = rightvu;
  devc->dev_ptr =
    (devc->dev_ptr +
     loops * sizeof (int) * devc->hw_channels) % devc->dmap->bytes_in_use;
  devc->dmap->user_counter += loops * sizeof (int) * devc->hw_channels;
}

#undef DO_DEBUG
#ifdef DO_DEBUG
static int
sin_gen (void)
{

  static int phase = 0, v;

  static short sinebuf[48] = {
    0, 4276, 8480, 12539, 16383, 19947, 23169, 25995,
    28377, 30272, 31650, 32486, 32767, 32486, 31650, 30272,
    28377, 25995, 23169, 19947, 16383, 12539, 8480, 4276,
    0, -4276, -8480, -12539, -16383, -19947, -23169, -25995,
    -28377, -30272, -31650, -32486, -32767, -32486, -31650, -30272,
    -28377, -25995, -23169, -19947, -16383, -12539, -8480, -4276
  };
  v = sinebuf[phase] * 256;
  phase = (phase + 1) % 48;

  return v;
}
#endif

static void
output_24_bits_packed (softoss_devc * devc, unsigned char *buf, int loops,
		       int *front_buf, int *rear_buf)
{
  int iloop;
  int tmp, leftvu, rightvu;

  leftvu = rightvu = 0;

#define pack_out(buf, val) \
	*buf++ = (val) & 0xff; \
	*buf++ = (val>>8) & 0xff; \
	*buf++ = (val>>16) & 0xff

  for (iloop = 0; iloop < loops; iloop++)
    {				/* Output the accumulated samples */
      int left = *front_buf++, right = *front_buf++;
      int rearleft = *rear_buf++, rearright = *rear_buf++;

      if (left > MAXSAMPLE)
	left = MAXSAMPLE;
      if (left < MINSAMPLE)
	left = MINSAMPLE;
      if (right > MAXSAMPLE)
	right = MAXSAMPLE;
      if (right < MINSAMPLE)
	right = MINSAMPLE;

      if (rearleft > MAXSAMPLE)
	rearleft = MAXSAMPLE;
      if (rearleft < MINSAMPLE)
	rearleft = MINSAMPLE;
      if (rearright > MAXSAMPLE)
	rearright = MAXSAMPLE;
      if (rearright < MINSAMPLE)
	rearright = MINSAMPLE;

      tmp = left;
      if (tmp < 0)
	tmp = -tmp;
      if (tmp > leftvu)
	leftvu = tmp;
      tmp = right;
      if (tmp < 0)
	tmp = -tmp;
      if (tmp > rightvu)
	rightvu = tmp;

      pack_out (buf, left);
      pack_out (buf, right);
      if (devc->multich)
	{
	  int i;

	  pack_out (buf, rearleft);
	  pack_out (buf, rearright);

	  for (i = devc->channels; i < devc->hw_channels; i++)
	    {
	      pack_out (buf, 0);
	    }
	}
    }				/* Output the samples */
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > devc->left_vu)
    devc->left_vu = leftvu;
  if (rightvu > devc->right_vu)
    devc->right_vu = rightvu;
  devc->dev_ptr =
    (devc->dev_ptr +
     loops * 3 * devc->hw_channels) % devc->dmap->bytes_in_use;
  devc->dmap->user_counter += loops * 3 * devc->hw_channels;
}

static void
softoss_resample_inner_loop (softoss_devc * devc, char *buf, int loops)
{
  static int front_buf[MAX_ILOOP * 2];
  static int rear_buf[MAX_ILOOP * 2];
  static int i, iloop, voice;
  static voice_info *v;

  for (i = 0; i < loops * 2; i++)
    {
      front_buf[i] = 0;
      rear_buf[i] = 0;
    }

  if (devc->softoss_opened)
    {				/* Handle synth */
      for (voice = 0; voice < devc->maxvoice; voice++)
	if (voice_active[voice])
	  {
	    if (softoss_voices[voice].wave == NULL)
	      voice_active[voice] = 0;
	    else
	      {			/* Do a voice */
		static int ptr, mode, step, endloop, startloop, leftvol,
		  rightvol, rearleftvol, rearrightvol;
		static short *wave;
		static int *front;
		static int *rear;

		front = front_buf;
		rear = rear_buf;

		v = &softoss_voices[voice];
		ptr = v->ptr;
		mode = v->mode;
		leftvol = v->leftvol;
		rightvol = v->rightvol;
		rearleftvol = v->rearleftvol;
		rearrightvol = v->rearrightvol;
		step = v->step;
		endloop = v->endloop;
		wave = v->wave;
		startloop = v->startloop;

		for (iloop = 0; iloop < loops; iloop++)
		  {		/* One step */
		    static int ix;

		    ix = (ptr) >> 9;

		    /* Interpolation */
		    {
		      static int accum, v1;

		      v1 = wave[ix];
		      accum =
			v1 + ((((wave[ix + 1] - v1)) * (ptr & 0x1ff)) >> 9);

		      *front++ += (accum * leftvol * 256) / 100;	/* Left */
		      *front++ += (accum * rightvol * 256) / 100;	/* Left */
		      *rear++ += (accum * rearleftvol * 256) / 100;	/* rear left */
		      *rear++ += (accum * rearrightvol * 256) / 100;	/* rear right */

		      /* Update sample pointer */

		      ptr += step;
		      if (ptr > endloop)
			{
			  if (mode & WAVE_LOOPING)
			    {
			      if (mode & WAVE_BIDIR_LOOP)
				{
				  mode ^= WAVE_LOOP_BACK;	/* Turn around */
				  step *= -1;
				}
			      else
				{
				  ptr -= v->looplen;
				}
			    }
			  else
			    ptr -= step;	/* Stay here */
			}

		      if (mode & WAVE_LOOP_BACK && ptr < startloop)
			{
			  if (mode & WAVE_BIDIR_LOOP)
			    {
			      mode ^= WAVE_LOOP_BACK;	/* Turn around */
			      step *= -1;
			    }
			  else
			    {
			      ptr += v->looplen;
			    }
			}
		    }
		  }		/* One step */

		/* Save modified parameters */
		v->ptr = ptr;
		v->mode = mode;
		v->step = step;
	      }			/* Do a voice */
	  }

    }				/* Handle synth */

  if (devc->nr_opened_audio_engines > 0)
    {
      /* Do audio mixing */
      for (voice = 0; voice < devc->nr_avoices; voice++)
	if (avoice_active[voice])
	  {
	    if (softoss_avoices[voice].mixer == NULL)
	      {
		cmn_err (CE_CONT, "Voice %d - mixer==NULL\n", voice);
		continue;
	      }
	    softoss_avoices[voice].mixer (voice, front_buf, loops);
	  }
    }

#ifdef USE_ALGORITHMS
  apply_effects (devc, front_buf, loops);
#endif

#ifdef USE_EQ
  /* Handle equalizer */
  if (!devc->eq_bypass)
    for (i = 0; i < loops; i++)
      {

	static flt_mem_t b1_l, b2_l, b3_l, b4_l;
	static flt_mem_t b1_r, b2_r, b3_r, b4_r;

	front_buf[i * 2] = peaking (devc, &b1_l, &b2_l, &b3_l, &b4_l,
				    (fixint_t *) & eq_band1_data[devc->
								 eq_bands[0]]
				    [0],
				    (fixint_t *) & eq_band2_data[devc->
								 eq_bands[1]]
				    [0],
				    (fixint_t *) & eq_band3_data[devc->
								 eq_bands[2]]
				    [0],
				    (fixint_t *) & eq_band4_data[devc->
								 eq_bands[3]]
				    [0], front_buf[i * 2] / 256) * 256;

	front_buf[i * 2 + 1] = peaking (devc, &b1_r, &b2_r, &b3_r, &b4_r,
					(fixint_t *) & eq_band1_data[devc->
								     eq_bands
								     [0]][0],
					(fixint_t *) & eq_band2_data[devc->
								     eq_bands
								     [1]][0],
					(fixint_t *) & eq_band3_data[devc->
								     eq_bands
								     [2]][0],
					(fixint_t *) & eq_band4_data[devc->
								     eq_bands
								     [3]][0],
					front_buf[i * 2 + 1] / 256) * 256;
      }
#endif

  /* Finally output the samples */

  if (devc->bits == 16)
    output_16_bits (devc, (short *) buf, loops, front_buf, rear_buf);
  else if (devc->bits == 24)
    output_24_bits_packed (devc, (unsigned char *) buf, loops, front_buf,
			   rear_buf);
  else
    output_32_bits (devc, (int *) buf, loops, front_buf, rear_buf);
}

void
softoss_resample_loop (softoss_devc * devc, int loops)
{
  int n, max, qt;
  char *buf;
  dmap_t *dmap = audio_engines[devc->masterdev]->dmap_out;

  while (loops > 0)
    {
      n = loops;
      max = devc->control_rate - devc->control_counter;

      if (n > max)
	n = max;
      if (n > MAX_ILOOP)
	n = MAX_ILOOP;

      qt = devc->dev_ptr;
      buf = (char *) (dmap->dmabuf + qt);

      PRT_STATUS (0x40);
      softoss_resample_inner_loop (devc, buf, n);
      PRT_STATUS (0x00);

      devc->control_counter += n;
      loops -= n;

      while (devc->control_counter >= devc->control_rate)
	{
	  devc->control_counter -= devc->control_rate;
	  softsyn_control_loop (devc);
	}
    }
}

void
update_buffering (int voice, unsigned int origptr, unsigned int ptr)
{
  int i, n = 0, dev = softoss_avoices[voice].audiodev;
  int fragsize = softoss_avoices[voice].fragsize;

  if (ptr < origptr)		/* Wrapped */
    ptr += softoss_avoices[voice].endloop;

  ptr = (ptr >> 9) / fragsize;
  origptr = (origptr >> 9) / fragsize;

  for (i = origptr; i < ptr && n++ < 5; i++)
    {
      oss_audio_outputintr (dev, 1);
    }
}

void
mix_avoice_mono8 (int voice, int *buf, int loops)
{
  int iloop;
  avoice_info *v;
  static unsigned char *wave;
  static int step;
  static unsigned int ptr;
  unsigned int origptr, endloop;
  int leftvol, rightvol;
  int tmp, leftvu, rightvu;

  v = &softoss_avoices[voice];
  leftvol = v->active_left_vol;
  rightvol = v->active_right_vol;
  leftvu = rightvu = 0;

  wave = (unsigned char *) v->wave;

  if (wave == NULL)
    return;

  ptr = origptr = v->ptr;
  step = v->step;
  endloop = v->endloop;

  if (step != 1 << 9)		/* SRC needed */
    {
      for (iloop = 0; iloop < loops; iloop++)
	{
	  static int accum;
	  static int ix;

	  static int v1;

	  ix = ptr >> 9;
	  v1 = (char) (wave[ix] ^ 0x80);	/* Convert to signed */
	  ix = ((ptr + 512) % endloop) >> 9;
	  accum =
	    (v1 +
	     ((((((char) (wave[ix] ^ 0x80)) - v1)) * ((int) (ptr & 0x1ff))) >>
	      9)) << 8;
	  killDC1 (v, &accum);
	  *buf++ += (tmp = (accum * leftvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > leftvu)
	    leftvu = tmp;
	  *buf++ += (tmp = (accum * rightvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > rightvu)
	    rightvu = tmp;

	  ptr = (ptr + step) % endloop;
	}
    }
  else
    {
      static unsigned char *wavep;
      for (iloop = 0, wavep = &wave[ptr >> 9]; iloop < loops; iloop++)
	{
	  *buf++ += (tmp =
		     ((((char) (*wavep ^ 0x80)) << 8) * leftvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > leftvu)
	    leftvu = tmp;
	  *buf++ += (tmp =
		     ((((char) (*wavep++ ^ 0x80)) << 8) * rightvol * 256) /
		     100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > rightvu)
	    rightvu = tmp;
	}
      ptr = (ptr + (step * loops)) % endloop;
    }

  v->ptr = ptr;
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > v->left_vu)
    v->left_vu = leftvu;
  if (rightvu > v->right_vu)
    v->right_vu = rightvu;
  update_buffering (voice, origptr, ptr);
}

void
mix_avoice_mono16 (int voice, int *buf, int loops)
{
  int iloop;
  avoice_info *v;
  static short *wave;
  static int step;
  static unsigned int ptr;
  unsigned int origptr, endloop;
  int leftvol, rightvol;
  int leftvu, rightvu, tmp;

  v = &softoss_avoices[voice];
  leftvol = v->active_left_vol;
  rightvol = v->active_right_vol;
  leftvu = rightvu = 0;

  wave = v->wave;

  if (wave == NULL)
    return;

  ptr = origptr = v->ptr;
  step = v->step;
  endloop = v->endloop;

  if (step != 1 << 9)
    {
      for (iloop = 0; iloop < loops; iloop++)
	{
	  static int accum;
	  static int ix;

	  static int v1;

	  ix = ptr >> 9;
	  v1 = (short) wave[ix];
	  ix = ((ptr + 512) % endloop) >> 9;
	  accum = v1 + ((((wave[ix] - v1)) * ((int) (ptr & 0x1ff))) >> 9);
	  killDC1 (v, &accum);
	  *buf++ += (tmp = (accum * leftvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > leftvu)
	    leftvu = tmp;
	  *buf++ += (tmp = (accum * rightvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > rightvu)
	    rightvu = tmp;

	  ptr = (ptr + step) % endloop;
	}
    }
  else
    {
      static short *wavep;
      for (iloop = 0, wavep = &wave[ptr >> 9]; iloop < loops; iloop++)
	{
	  *buf++ += (tmp = (*wavep * leftvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > leftvu)
	    leftvu = tmp;
	  *buf++ += (tmp = (*wavep++ * rightvol * 256) / 100);
	  if (tmp < 0)
	    tmp = -tmp;
	  if (tmp > rightvu)
	    rightvu = tmp;

	}
      ptr = (ptr + (step * loops)) % endloop;
    }

  v->ptr = ptr;
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > v->left_vu)
    v->left_vu = leftvu;
  if (rightvu > v->right_vu)
    v->right_vu = rightvu;
  update_buffering (voice, origptr, ptr);
}

void
mix_avoice_stereo8 (int voice, int *buf, int loops)
{
  int iloop;
  avoice_info *v;
  static unsigned char *wave;
  static int step;
  static unsigned int ptr;
  unsigned int origptr, endloop, fragmodulo;
  int leftvol, rightvol;
  int leftvu, rightvu;
  int tmpl, tmpr;

  v = &softoss_avoices[voice];
  leftvol = v->active_left_vol;
  rightvol = v->active_right_vol;
  leftvu = rightvu = 0;

  wave = (unsigned char *) v->wave;

  if (wave == NULL)
    return;

  ptr = origptr = v->ptr;
  step = v->step;
  endloop = v->endloop;
  fragmodulo = v->fragmodulo;

  if (step != 1 << 9)
    {
      for (iloop = 0; iloop < loops; iloop++)
	{

	  static int ix;

	  /* Interpolation */
	  static int v1, ix0, v2;

	  ix0 = ix = (ptr >> 9) * 2;
	  v1 = (char) (wave[ix] ^ 0x80);
	  ix = (ix0 + 2) % fragmodulo;
	  v2 = (char) (wave[ix] ^ 0x80);	/* This is really required */
	  tmpl = (v1 + ((((v2 - v1)) * ((int) (ptr & 0x1ff))) >> 9)) << 8;

	  ix = (ix0 + 1) % fragmodulo;
	  v1 = (char) (wave[ix] ^ 0x80);
	  ix = (ix0 + 3) % fragmodulo;
	  v2 = (char) (wave[ix] ^ 0x80);
	  tmpr = (v1 + ((((v2 - v1)) * ((int) (ptr & 0x1ff))) >> 9)) << 8;

	  killDC (v, &tmpl, &tmpr);
	  tmpl = (tmpl * leftvol * 256 + 50) / 100;
	  *buf++ += tmpl;
	  if (tmpl < 0)
	    tmpl = -tmpl;
	  if (tmpl > leftvu)
	    leftvu = tmpl;

	  tmpr = (tmpr * rightvol * 256 + 50) / 100;
	  *buf++ += tmpr;
	  if (tmpr < 0)
	    tmpr = -tmpr;
	  if (tmpr > rightvu)
	    rightvu = tmpr;

	  ptr = (ptr + step) % endloop;
	}
    }
  else
    {
      static unsigned char *wavep;
      for (iloop = 0, wavep = &wave[(ptr >> 9) << 1]; iloop < loops; iloop++)
	{
	  tmpl =
	    ((((char) (*wavep++ ^ 0x80)) << 8) * leftvol * 256 + 50) / 100;
	  tmpr =
	    ((((char) (*wavep++ ^ 0x80)) << 8) * rightvol * 256 + 50) / 100;
	  killDC (v, &tmpl, &tmpr);
	  *buf++ += tmpl;
	  if (tmpl < 0)
	    tmpl = -tmpl;
	  if (tmpl > leftvu)
	    leftvu = tmpl;
	  *buf++ += tmpr;
	  if (tmpr < 0)
	    tmpr = -tmpr;
	  if (tmpr > rightvu)
	    rightvu = tmpr;
	}
      ptr = (ptr + (step * loops)) % endloop;
    }
  v->ptr = ptr;
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > v->left_vu)
    v->left_vu = leftvu;
  if (rightvu > v->right_vu)
    v->right_vu = rightvu;
  update_buffering (voice, origptr, ptr);
}

void
mix_avoice_stereo16 (int voice, int *buf, int loops)
{
  int iloop;
  avoice_info *v;
  static short *wave;
  static int step;
  static unsigned int ptr;
  unsigned int origptr, endloop, fragmodulo;
  static int leftvol, rightvol;
  int leftvu, rightvu;
  int tmpl, tmpr;

  v = &softoss_avoices[voice];
  leftvol = v->active_left_vol;
  rightvol = v->active_right_vol;
  leftvu = rightvu = 0;

  wave = v->wave;

  if (wave == NULL)
    return;

  ptr = origptr = v->ptr;
  step = v->step;
  endloop = v->endloop;
  fragmodulo = v->fragmodulo;

  if (step != 1 << 9)
    {
      for (iloop = 0; iloop < loops; iloop++)
	{
	  static int ix;

	  /* Interpolation */
	  static int v1, ix0, v2;

	  ix0 = ix = (ptr >> 9) << 1;
	  v1 = (short) wave[ix];
	  ix = (ix0 + 2) % fragmodulo;
	  v2 = (short) wave[ix];	/* This is really required */
	  tmpl =
	    (((v1 +
	       ((((v2 -
		   v1)) * ((int) (ptr & 0x1ff))) >> 9))) * leftvol * 256 +
	     50) / 100;

	  ix = (ix0 + 1) % fragmodulo;
	  v1 = (short) wave[ix];
	  ix = (ix0 + 3) % fragmodulo;
	  v2 = (short) wave[ix];
	  tmpr =
	    (((v1 +
	       ((((v2 -
		   v1)) * ((int) (ptr & 0x1ff))) >> 9))) * rightvol * 256 +
	     50) / 100;

	  killDC (v, &tmpl, &tmpr);

	  *buf++ += tmpl;
	  if (tmpl < 0)
	    tmpl = -tmpl;
	  if (tmpl > leftvu)
	    leftvu = tmpl;
	  *buf++ += tmpr;
	  if (tmpr < 0)
	    tmpr = -tmpr;
	  if (tmpr > rightvu)
	    rightvu = tmpr;
	  ptr = (ptr + step) % endloop;
	}
    }
  else
    {
      static short *wavep;
      for (iloop = 0, wavep = &wave[(ptr >> 9) << 1]; iloop < loops; iloop++)
	{
	  tmpl = (*wavep++ * leftvol * 256 + 50) / 100;
	  tmpr = (*wavep++ * rightvol * 256 + 50) / 100;
	  killDC (v, &tmpl, &tmpr);
	  *buf++ += tmpl;
	  if (tmpl < 0)
	    tmpl = -tmpl;
	  if (tmpl > leftvu)
	    leftvu = tmpl;
	  *buf++ += tmpr;
	  if (tmpr < 0)
	    tmpr = -tmpr;
	  if (tmpr > rightvu)
	    rightvu = tmpr;

	}
      ptr = (ptr + (step * loops)) % endloop;
    }
  v->ptr = ptr;
  leftvu >>= 15;
  rightvu >>= 15;
  if (leftvu > v->left_vu)
    v->left_vu = leftvu;
  if (rightvu > v->right_vu)
    v->right_vu = rightvu;
  update_buffering (voice, origptr, v->ptr);

}

/*ARGSUSED*/
void
start_resampling (softoss_devc * devc)
{
}
