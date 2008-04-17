/*
 * Purpose: Old virtual mixing audio driver
 *
 * This driver has been replaced by the new vmix driver. However since vmix
 * doesn't yet work in all systems this driver is still hanging around.
 *
 * There is some MIDI/synth functionality included in softoss (which was
 * originally just a virtual wave table driver). This code will no longer be
 * used since the old sequencer code has been stripped from OSS 4.0. However
 * we have not removed this unused functionality from softoss because it's
 * too fundamental part of the code.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 1997. All rights reserved.

#define SOFTSYN_MAIN
#define HANDLE_LFO

#define ENVELOPE_SCALE		8
#define NO_SAMPLE		0xffff

#include "softoss_cfg.h"

extern int softoss_devices;
static int already_attached = 0;

#include "softoss.h"
#include <midi_core.h>
char voice_active[MAX_VOICE] = { 0 };
voice_info softoss_voices[MAX_VOICE] = { {0} };	/* Voice spesific info */
char avoice_active[MAX_AVOICE] = { 0 };
char recvoice_active[MAX_AVOICE] = { 0 };
avoice_info softoss_avoices[MAX_AVOICE] = { {0} };	/* Voice spesific info */
extern int softoss_disable;

#ifdef USE_LICENSING
extern int options_data;
#endif

extern int softoss_use_src;
int softoss_failed = 0;

#ifdef SCHEDULE_INTERRUPT
static OSS_VOLATILE int intr_pending = 0;
#endif

#ifdef HANDLE_LFO
/*
 * LFO table. Playback at 128 Hz gives 1 Hz LFO frequency.
 */
static int tremolo_table[128] = {
  0, 39, 158, 355, 630, 982, 1411, 1915,
  2494, 3146, 3869, 4662, 5522, 6448, 7438, 8489,
  9598, 10762, 11980, 13248, 14563, 15922, 17321, 18758,
  20228, 21729, 23256, 24806, 26375, 27960, 29556, 31160,
  32768, 34376, 35980, 37576, 39161, 40730, 42280, 43807,
  45308, 46778, 48215, 49614, 50973, 52288, 53556, 54774,
  55938, 57047, 58098, 59088, 60014, 60874, 61667, 62390,
  63042, 63621, 64125, 64554, 64906, 65181, 65378, 65497,
  65536, 65497, 65378, 65181, 64906, 64554, 64125, 63621,
  63042, 62390, 61667, 60874, 60014, 59087, 58098, 57047,
  55938, 54774, 53556, 52288, 50973, 49614, 48215, 46778,
  45308, 43807, 42280, 40730, 39161, 37576, 35980, 34376,
  32768, 31160, 29556, 27960, 26375, 24806, 23256, 21729,
  20228, 18758, 17321, 15922, 14563, 13248, 11980, 10762,
  9598, 8489, 7438, 6448, 5522, 4662, 3869, 3146,
  2494, 1915, 1411, 982, 630, 355, 158, 39
};

static int vibrato_table[128] = {
  0, 1608, 3212, 4808, 6393, 7962, 9512, 11039,
  12540, 14010, 15447, 16846, 18205, 19520, 20788, 22006,
  23170, 24279, 25330, 26320, 27246, 28106, 28899, 29622,
  30274, 30853, 31357, 31786, 32138, 32413, 32610, 32729,
  32768, 32729, 32610, 32413, 32138, 31786, 31357, 30853,
  30274, 29622, 28899, 28106, 27246, 26320, 25330, 24279,
  23170, 22006, 20788, 19520, 18205, 16846, 15447, 14010,
  12540, 11039, 9512, 7962, 6393, 4808, 3212, 1608,
  0, -1608, -3212, -4808, -6393, -7962, -9512, -11039,
  -12540, -14010, -15447, -16846, -18205, -19520, -20788, -22006,
  -23170, -24279, -25330, -26320, -27246, -28106, -28899, -29622,
  -30274, -30853, -31357, -31786, -32138, -32413, -32610, -32729,
  -32768, -32729, -32610, -32413, -32138, -31786, -31357, -30853,
  -30274, -29622, -28899, -28106, -27246, -26320, -25330, -24279,
  -23170, -22006, -20788, -19520, -18205, -16846, -15447, -14010,
  -12540, -11039, -9512, -7962, -6393, -4808, -3212, -1608
};

#endif

static oss_native_word last_resample_jiffies;
static oss_native_word resample_counter;

static OSS_VOLATILE int is_running = 0;
static int softoss_loaded = 0;

static struct synth_info softsyn_info =
  { SYNTH_DRIVER_NAME, 0, SYNTH_TYPE_SAMPLE, SAMPLE_TYPE_GUS, 0, 16, 0,
  MAX_PATCH
};

static struct softoss_devc sdev_info = { 0 };
static softoss_devc *devc = &sdev_info;

static struct voice_alloc_info *voice_alloc = NULL;

static int softsyn_open (int synthdev, int mode);
static void init_voice (softoss_devc * devc, int voice);
static void compute_step (int voice);
static int softoss_avol (int dev, int ctrl, unsigned int cmd, int value);
static int softoss_vu (int dev, int ctrl, unsigned int cmd, int value);
static int softoss_control (int dev, int ctrl, unsigned int cmd, int value);

#ifdef CONFIG_SEQUENCER
static OSS_VOLATILE int tmr_running = 0;
#endif

static int voice_limit = 24;

static void
set_max_voices (int nr)
{
  int i;

  if (nr < 4)
    nr = 4;

  if (nr > voice_limit)
    nr = voice_limit;

  voice_alloc->max_voice = devc->maxvoice = nr;
  devc->afterscale = 5;

  for (i = 31; i > 0; i--)
    if (nr & (1 << i))
      {
	devc->afterscale = i + 2;
	return;
      }
}

static void
update_vibrato (int voice)
{
  voice_info *v = &softoss_voices[voice];

  int x;

  x = vibrato_table[v->vibrato_phase >> 8];
  v->vibrato_phase = (v->vibrato_phase + v->vibrato_step) & 0x7fff;

  x = (x * v->vibrato_depth) >> 15;
  v->vibrato_level = (x * 600) >> 10;

  compute_step (voice);
}

static void
update_tremolo (int voice)
{
  voice_info *v = &softoss_voices[voice];
  int x;

  x = tremolo_table[v->tremolo_phase >> 8];
  v->tremolo_phase = (v->tremolo_phase + v->tremolo_step) & 0x7fff;

  v->tremolo_level = (x * v->tremolo_depth) >> 22;
}

static void
start_vibrato (int voice)
{
  voice_info *v = &softoss_voices[voice];
  int rate;

  if (!v->vibrato_depth)
    return;

  rate = v->vibrato_rate * 6 * 128;
  v->vibrato_step = (rate * devc->control_rate) / devc->speed;

  devc->vibratomap |= (1 << voice);	/* Enable vibrato */
}

static void
start_tremolo (int voice)
{
  voice_info *v = &softoss_voices[voice];
  int rate;

  if (!v->tremolo_depth)
    return;

  rate = v->tremolo_rate * 6 * 128;
  v->tremolo_step = (rate * devc->control_rate) / devc->speed;

  devc->tremolomap |= (1 << voice);	/* Enable tremolo */
}

static void
update_volume (int voice)
{
  voice_info *v = &softoss_voices[voice];
  unsigned int vol, left, right, leftrear, rightrear;

/*
 * Compute plain volume
 */

  vol = (v->velocity * v->expression_vol * v->main_vol) >> 11;

/*
 * Handle LFO
 */

  if (devc->tremolomap & (1 << voice))
    {
      int t;

      t = 32768 - v->tremolo_level;
      vol = (vol * t) >> 15;
      update_tremolo (voice);
    }
/*
 * Envelope
 */
  if (v->mode & WAVE_ENVELOPES && !v->percussive_voice)
    vol = (vol * (v->envelope_vol >> 16)) >> 19;
  else
    vol >>= 4;

/*
 * Handle panning
 */

  if (v->panning < 0)		/* Pan left */
    right = (vol * (128 + v->panning)) / 128;
  else
    right = vol;

  right = (right * devc->synth_volume_right) / 100;

  if (v->panning > 0)		/* Pan right */
    left = (vol * (128 - v->panning)) / 128;
  else
    left = vol;
  left = (left * devc->synth_volume_left) / 100;

  leftrear = left;
  rightrear = right;

  if (v->frontrear != 0)
    {
      if (v->frontrear < 0)	/* Rear volume louder */
	{
	  right = (right * (128 + v->frontrear)) / 128;
	  left = (left * (128 + v->frontrear)) / 128;
	}
      else
	{
	  rightrear = (rightrear * (128 - v->frontrear)) / 128;
	  leftrear = (leftrear * (128 - v->frontrear)) / 128;
	}
    }

  v->leftvol = left;
  v->rightvol = right;
  v->rearleftvol = leftrear;
  v->rearrightvol = rightrear;
}

/*ARGSUSED*/
static void
step_envelope (int voice, int do_release, int velocity)
{
  voice_info *v = &softoss_voices[voice];
  int r, rate, time, dif;
  unsigned int vol;

  if (!voice_active[voice] || v->sample == NULL)
    {
      return;
    }

  if (!do_release)
    if (v->mode & WAVE_SUSTAIN_ON && v->envelope_phase == 3)
      {				/* Stop envelope until note off */
	v->envelope_volstep = 0;
	v->envelope_time = 0x7fffffff;
	if (v->mode & WAVE_VIBRATO)
	  start_vibrato (voice);
	if (v->mode & WAVE_TREMOLO)
	  start_tremolo (voice);
	return;
      }

  if (do_release)
    v->envelope_phase = 4;
  else
    v->envelope_phase++;

  if (v->envelope_phase >= 5)	/* Finished */
    {
      init_voice (devc, voice);
      voice_alloc->map[voice] = 0;
      return;
    }

  vol = v->envelope_target = v->sample->env_offset[v->envelope_phase] << 22;

  rate = v->sample->env_rate[v->envelope_phase];
  r = 3 - ((rate >> 6) & 0x3);
  r *= 3;
  r = (int) (rate & 0x3f) << r;
  rate = (((r * 44100) / devc->speed) * devc->control_rate) << 6;

  if (v->envelope_phase > 3)
    if (rate < (1 << 20))	/* Avoid infinitely "releasing" voices */
      rate = 1 << 20;
  if (rate < 1)
    rate = 1;

  dif = (v->envelope_vol - vol);
  if (dif < 0)
    dif *= -1;
  if (dif < rate * 2)		/* Too close */
    {
      step_envelope (voice, 0, 60);
      return;
    }

  if (vol > v->envelope_vol)
    {
      v->envelope_volstep = rate;
      time = (vol - v->envelope_vol) / rate;
    }
  else
    {
      v->envelope_volstep = -rate;
      time = (v->envelope_vol - vol) / rate;
    }

  time--;
  if (time <= 0)
    time = 1;

  v->envelope_time = time;

}

static void
step_envelope_lfo (int voice)
{
  voice_info *v = &softoss_voices[voice];

/*
 * Update pitch (vibrato) LFO 
 */

  if (devc->vibratomap & (1 << voice))
    update_vibrato (voice);

/* 
 * Update envelope
 */

  if (v->mode & WAVE_ENVELOPES)
    {
      v->envelope_vol += v->envelope_volstep;
      /* Overshoot protection */
      if (v->envelope_vol < 0)
	{
	  v->envelope_vol = v->envelope_target;
	  v->envelope_volstep = 0;
	}

      if (v->envelope_time-- <= 0)
	{
	  v->envelope_vol = v->envelope_target;
	  step_envelope (voice, 0, 60);
	}
    }
}

static void
compute_step (int voice)
{
  voice_info *v = &softoss_voices[voice];

  /*
   * Since the pitch bender may have been set before playing the note, we
   * have to calculate the bending now.
   */

#ifdef CONFIG_SEQUENCER
  /* TODO: Enable this if the synth code is ever needed */
  v->current_freq = compute_finetune (v->orig_freq,
				      v->bender,
				      v->bender_range, v->vibrato_level);
#endif
  v->step = (((v->current_freq << 9) + (devc->speed >> 1)) / devc->speed);

  if (v->mode & WAVE_LOOP_BACK)
    v->step *= -1;		/* Reversed playback */
}

static void
init_voice (softoss_devc * devc, int voice)
{
  voice_info *v = &softoss_voices[voice];

  if (voice < 0 || voice >= MAX_VOICE)
    {
      return;
    }

  voice_active[voice] = 0;
  devc->vibratomap &= ~(1 << voice);
  devc->tremolomap &= ~(1 << voice);
  v->mode = 0;
  v->wave = NULL;
  v->sample = NULL;
  v->ptr = 0;
  v->startloop = 0;
  v->startbackloop = 0;
  v->endloop = 0;
  v->looplen = 0;
  v->bender = 0;
  v->bender_range = 200;
  v->panning = 0;
  v->frontrear = 127;
  v->main_vol = 127;
  v->expression_vol = 127;
  v->patch_vol = 127;
  v->percussive_voice = 0;
  v->sustain_mode = 0;
  v->envelope_phase = 1;
  v->envelope_vol = 1 << 24;
  v->envelope_volstep = 256;
  v->envelope_time = 0;
  v->vibrato_phase = 0;
  v->vibrato_step = 0;
  v->vibrato_level = 0;
  v->vibrato_rate = 0;
  v->vibrato_depth = 0;
  v->tremolo_phase = 0;
  v->tremolo_step = 0;
  v->tremolo_level = 0;
  v->tremolo_rate = 0;
  v->tremolo_depth = 0;
  v->releasing = 0;
  /* voice_alloc->map[voice] = 0; */
  voice_alloc->alloc_times[voice] = 0;
}

static void
reset_samples (softoss_devc * devc)
{
  int i;

  for (i = 0; i < MAX_VOICE; i++)
    voice_active[i] = 0;
  for (i = 0; i < devc->maxvoice; i++)
    {
      init_voice (devc, i);
      softoss_voices[i].instr = 0;
    }

  devc->ram_used = 0;

  for (i = 0; i < MAX_PATCH; i++)
    devc->programs[i] = NO_SAMPLE;

  for (i = 0; i < devc->nrsamples; i++)
    {
      KERNEL_FREE (devc->samples[i]);
      KERNEL_FREE (devc->wave[i]);
      devc->samples[i] = NULL;
      devc->wave[i] = NULL;
    }

  devc->nrsamples = 0;
}

static void
init_engine (softoss_devc * devc)
{
  int i, fz, srate, sz = devc->hw_channels;

  set_max_voices (devc->default_max_voices);
  voice_alloc->timestamp = 0;

  if (devc->bits == 16)
    sz *= 2;
  else if (devc->bits == 32)
    sz *= 4;
  else if (devc->bits == 24)
    sz *= 3;

  fz = devc->fragsize / sz;	/* Samples per fragment */
  devc->samples_per_fragment = fz;
  DDB (cmn_err
       (CE_CONT, "Samples per fragment %d, samplesize %d bytes\n", fz, sz));

  devc->usecs = 0;
  devc->usecs_per_frag = (1000000 * fz) / devc->speed;

  for (i = 0; i < devc->maxvoice; i++)
    {
      init_voice (devc, i);
      softoss_voices[i].instr = 0;
    }

  devc->engine_state = ES_STOPPED;

  start_resampling (devc);

  srate = (devc->speed / 10000);	/* 1 to 4 */
  if (srate <= 0)
    srate = 1;
  devc->delay_size = (DELAY_SIZE * srate) / 4;
  if (devc->delay_size == 0 || devc->delay_size > DELAY_SIZE)
    devc->delay_size = DELAY_SIZE;
}

void
softsyn_control_loop (softoss_devc * devc)
{
  int voice;

/*
 *    Recompute envlope, LFO, etc.
 */
#ifdef DO_TIMINGS
  oss_do_timing ("Entered control_loop");
#endif
  for (voice = 0; voice < devc->maxvoice; voice++)
    if (voice_active[voice])
      {
	update_volume (voice);
	step_envelope_lfo (voice);
      }
#ifdef DO_TIMINGS
  oss_do_timing ("Done control_loop");
#endif
}

void softoss_start_engine (softoss_devc * devc);
extern void softoss_handle_input (softoss_devc * devc);

/*ARGSUSED*/
static void
do_resample (int dummy)
{
  dmap_t *dmap = audio_engines[devc->masterdev]->dmap_out;
  struct voice_info *vinfo;
  oss_uint64_t jif;

  int voice, loops;

  if (softoss_failed)
    return;

#ifdef DO_TIMINGS
  {
    static char tmp[100];
    int i, n;

    n = 0;
    for (i = 0; i < devc->maxvoice; i++)
      if (voice_active[i])
	n++;
    sprintf (tmp, "Entered do_resample, %d voices\n", n);
    oss_do_timing (tmp);
  }
#endif

  if (is_running)
    {
      cmn_err (CE_CONT, "Playback overrun\n");
      return;
    }

  /*
   * Check if there is a whole fragment of space available
   */
  if (dmap->byte_counter + dmap->bytes_in_use - dmap->user_counter <
      dmap->fragment_size)
    return;

  jif = GET_JIFFIES ();
  if (jif == last_resample_jiffies)
    {
      if (resample_counter++ > dmap->nfrags)
	{
	  for (voice = 0; voice < devc->maxvoice; voice++)
	    init_voice (devc, voice);
	  voice_limit--;
	  resample_counter = 0;
	  cmn_err (CE_WARN, "CPU overload. Limiting # of voices to %d\n",
		   voice_limit);
#ifdef DO_TIMINGS
	  oss_do_timing ("Overheat, dropping voices");
#endif

	  if (voice_limit < 10)
	    {
	      voice_limit = 16;
	      devc->speed = (devc->speed * 2) / 3;

#ifdef DO_TIMINGS
	      oss_do_timing ("Still overheat. Giving up.");
#endif
	      cmn_err (CE_WARN,
		       "Dropping sampling rate and stopping the device.\n");
	      softoss_failed = 1;
	    }
	}
    }
  else
    {
      last_resample_jiffies = jif;
      resample_counter = 0;
    }

  is_running = 1;

  if (devc->opened_inputs > 0)
    {
      softoss_handle_input (devc);
    }
/*
 * First verify that all active voices are valid (do this just once per block).
 */
  for (voice = 0; voice < devc->maxvoice; voice++)
    if (voice_active[voice])
      {
	int ptr;

	vinfo = &softoss_voices[voice];
	ptr = vinfo->ptr >> 9;

	if (vinfo->wave == NULL || ptr < 0 || ptr > vinfo->sample->len)
	  init_voice (devc, voice);
	else if (!(vinfo->mode & WAVE_LOOPING) &&
		 (vinfo->ptr + vinfo->step) > vinfo->endloop)
	  voice_active[voice] = 0;
      }

/*
 *    Start the resampling process
 */

  loops = devc->samples_per_fragment;

#ifdef DO_TIMINGS
  {
    char tmp[64];
    int qt;

    qt = devc->dev_ptr;
    sprintf (tmp, "Starting resample_loop %d (at %x)\n", qt,
	     qt * dmap->fragment_size);
    oss_do_timing (tmp);
  }
#endif
  softoss_resample_loop (devc, loops);	/* In Xsoftoss_rs.c */
#ifdef DO_TIMINGS
  oss_do_timing ("Completed resample_loop");
#endif

#ifdef OSS_SYNC_CACHE
  OSS_SYNC_CACHE (dmap, dmap_get_qtail (dmap), OPEN_WRITE);
#endif
  if (devc->nr_loops > 0)
    softloop_callback (devc);

  devc->usecs += devc->usecs_per_frag;

#if 0
  /* TODO: Find a better way for this in OSS 4.1 */

  if (tmr_running)
    {
      sound_timer_interrupt ();
    }
#endif

/*
 *    Execute timer
 */

#ifdef CONFIG_SEQUENCER
  if (!tmr_running)
    if (devc->usecs >= devc->next_event_usecs)
      {
	devc->next_event_usecs = ~0;
	sequencer_timer (0);
      }
#endif
  is_running = 0;
#ifdef DO_TIMINGS
  oss_do_timing ("Completed do_resample");
#endif
}

/*ARGSUSED*/
static void
delayed_resample (int dummy)
{
  dmap_t *dmap = audio_engines[devc->masterdev]->dmap_out;
  int n = 0;

  if (is_running)
    return;

  while (devc->engine_state != ES_STOPPED &&
	 dmap_get_qlen (dmap) < devc->max_playahead && n++ < 2)
    {
      do_resample (0);
    }
#ifdef SCHEDULE_INTERRUPT
  intr_pending = 0;
#endif
}

/*ARGSUSED*/
static void
softsyn_callback (int dev, int parm)
{
#ifdef DO_TIMINGS
  oss_do_timing ("SoftOSS callback");
#endif
#ifdef SCHEDULE_INTERRUPT
  if (!is_running && !intr_pending)
    {
      intr_pending = 1;

      SCHEDULE_INTERRUPT (delayed_resample);
    }
#else
  delayed_resample (0);
#endif
}

int softoss_open_audiodev (void);

void
softoss_start_engine (softoss_devc * devc)
{
  dmap_t *dmap;
  oss_native_word flags;

  if (devc->masterdev < 0 || devc->masterdev >= num_audio_engines)
    {
      return;
    }


  if (!devc->masterdev_opened)
    if (softoss_open_audiodev ())
      {
	return;
      }
  dmap = audio_engines[devc->masterdev]->dmap_out;

  devc->usecs = 0;
  devc->dev_ptr = 0;
  devc->next_event_usecs = ~(oss_native_word) 0;
  devc->control_rate = 64;
  devc->control_counter = 0;
  devc->dmap = audio_engines[devc->masterdev]->dmap_out;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (devc->engine_state == ES_STOPPED)
    {
      int trig, n = 0;

      trig = 0;
      audio_engines[devc->masterdev]->dmap_out->dma_mode = PCM_ENABLE_OUTPUT;
      devc->engine_state = ES_STARTED;
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

      oss_audio_ioctl (devc->masterdev, NULL, SNDCTL_DSP_SETTRIGGER,
		       (ioctl_arg) & trig);
      dmap->audio_callback = softsyn_callback;

      while (dmap_get_qlen (dmap) < devc->max_playahead && n++ < 2)
	do_resample (0);

      last_resample_jiffies = GET_JIFFIES ();
      resample_counter = 0;

      trig = PCM_ENABLE_OUTPUT;
      if (devc->duplex_mode && devc->input_master == devc->masterdev)
	trig |= PCM_ENABLE_INPUT;

      if (oss_audio_ioctl (devc->masterdev, NULL, SNDCTL_DSP_SETTRIGGER,
			   (ioctl_arg) & trig) < 0)
	{
	  cmn_err (CE_WARN, "Trigger failed\n");
	}
      start_record_engine (devc);
    }
  else
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
    }
}

/*ARGSUSED*/
static void
stop_engine (softoss_devc * devc)
{
}

/*ARGSUSED*/
static int
softsyn_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  switch (cmd)
    {

    case SNDCTL_SYNTH_INFO:
      softsyn_info.nr_voices = devc->maxvoice;

      memcpy ((char *) arg, (char *) &softsyn_info, sizeof (softsyn_info));
      return 0;
      break;

    case SNDCTL_SEQ_RESETSAMPLES:
      stop_engine (devc);
      reset_samples (devc);
      return 0;
      break;

    case SNDCTL_SYNTH_MEMAVL:
      return devc->ram_size - devc->ram_used;
      break;

    default:
      return -EINVAL;
    }

}

/*ARGSUSED*/
static int
softsyn_kill_note (int devno, int voice, int note, int velocity)
{
  if (voice < 0 || voice > devc->maxvoice)
    return 0;

  if (softoss_voices[voice].sustain_mode & 1)	/* Sustain controller on */
    {
      softoss_voices[voice].sustain_mode = 3;	/* Note off pending */
      return 0;
    }
  softoss_voices[voice].releasing = 1;

  if (velocity > 127 || softoss_voices[voice].mode & WAVE_FAST_RELEASE)
    {
      init_voice (devc, voice);	/* Mark it inactive */
      voice_alloc->map[voice] = 0;
      return 0;
    }

  if (softoss_voices[voice].mode & WAVE_ENVELOPES)
    step_envelope (voice, 1, velocity);	/* Enter sustain phase */
  else
    {
      init_voice (devc, voice);	/* Mark it inactive */
      voice_alloc->map[voice] = 0;
    }
  return 0;
}

/*ARGSUSED*/
static int
softsyn_set_instr (int dev, int voice, int instr)
{
  if (voice < 0 || voice > devc->maxvoice)
    return 0;

  if (instr < 0 || instr > MAX_PATCH)
    {
      cmn_err (CE_CONT, "Invalid instrument number %d\n", instr);
      return 0;
    }

  softoss_voices[voice].instr = instr;

  return 0;
}

static int
softsyn_start_note (int dev, int voice, int note, int volume)
{
  int instr = 0;
  int best_sample, best_delta, delta_freq, selected;
  oss_native_word note_freq, freq, base_note;
  voice_info *v = &softoss_voices[voice];
  oss_native_word flags;

  struct patch_info *sample;

  if (voice < 0 || voice > devc->maxvoice)
    return 0;

  if (volume == 0)		/* Actually note off */
    {
      softsyn_kill_note (dev, voice, note, volume);
      return 0;
    }

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (voice_active[voice])
    init_voice (devc, voice);

  if (note == 255)
    {				/* Just volume update */
      v->velocity = volume;
      if (voice_active[voice])
	update_volume (voice);
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return 0;
    }

  voice_active[voice] = 0;	/* Stop the voice for a while */
  v->releasing = 0;
  devc->vibratomap &= ~(1 << voice);
  devc->tremolomap &= ~(1 << voice);

  instr = v->instr;
  if (instr < 0 || instr > MAX_PATCH || devc->programs[instr] == NO_SAMPLE)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return 0;
    }

  instr = devc->programs[instr];

  if (instr < 0 || instr >= devc->nrsamples)
    {
      cmn_err (CE_WARN, "Corrupted MIDI instrument %d (%d)\n",
	       v->instr, instr);
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return 0;
    }

#ifdef CONFIG_SEQUENCER
  note_freq = note_to_freq (note);
#else
  note_freq = 0;
#endif

  selected = -1;

  best_sample = instr;
  best_delta = 1000000;

  while (instr != NO_SAMPLE && instr >= 0 && selected == -1)
    {
      delta_freq = note_freq - devc->samples[instr]->base_note;

      if (delta_freq < 0)
	delta_freq = -delta_freq;
      if (delta_freq < best_delta)
	{
	  best_sample = instr;
	  best_delta = delta_freq;
	}
      if (devc->samples[instr]->low_note <= note_freq &&
	  note_freq <= devc->samples[instr]->high_note)
	selected = instr;
      else
	instr = devc->samples[instr]->key;	/* Link to next sample */

      if (instr < 0 || instr >= devc->nrsamples)
	instr = NO_SAMPLE;
    }

  if (selected == -1)
    instr = best_sample;
  else
    instr = selected;

  if (instr < 0 || instr == NO_SAMPLE || instr > devc->nrsamples)
    {
      cmn_err (CE_WARN, "Unresolved MIDI instrument %d\n", v->instr);
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return 0;
    }

  sample = devc->samples[instr];
  v->sample = sample;

  if (v->percussive_voice)	/* No key tracking */
    {
      v->orig_freq = sample->base_freq;	/* Fixed pitch */
    }
  else
    {
      base_note = sample->base_note / 100;
      note_freq /= 100;

      freq = sample->base_freq * note_freq / base_note;
      v->orig_freq = freq;
    }

  if (!(sample->mode & WAVE_LOOPING))
    {
      sample->loop_end = sample->len;
    }

  v->wave = devc->wave[instr];

  if (volume < 0)
    volume = 0;
  else if (volume > 127)
    volume = 127;

  v->ptr = 0;
  v->startloop = sample->loop_start * 512;
  v->startbackloop = 0;
  v->endloop = sample->loop_end * 512;
  v->looplen = (sample->loop_end - sample->loop_start) * 512;
  v->leftvol = 64;
  v->rightvol = 64;
  v->rearleftvol = 64;
  v->rearrightvol = 64;
  v->patch_vol = sample->volume;
  v->velocity = volume;
  v->mode = sample->mode;
  v->releasing = 0;
  v->vibrato_phase = 0;
  v->vibrato_step = 0;
  v->vibrato_level = 0;
  v->vibrato_rate = 0;
  v->vibrato_depth = 0;
  v->tremolo_phase = 0;
  v->tremolo_step = 0;
  v->tremolo_level = 0;
  v->tremolo_rate = 0;
  v->tremolo_depth = 0;

  if (!(v->mode & WAVE_LOOPING))
    v->mode &= ~(WAVE_BIDIR_LOOP | WAVE_LOOP_BACK);
  else if (v->mode & WAVE_LOOP_BACK)
    {
      v->ptr = sample->len;
      v->startbackloop = v->startloop;
    }

  if (v->mode & WAVE_VIBRATO)
    {
      v->vibrato_rate = sample->vibrato_rate;
      v->vibrato_depth = sample->vibrato_depth;
    }

  if (v->mode & WAVE_TREMOLO)
    {
      v->tremolo_rate = sample->tremolo_rate;
      v->tremolo_depth = sample->tremolo_depth;
    }

  if (v->mode & WAVE_ENVELOPES)
    {
      v->envelope_phase = -1;
      v->envelope_vol = 0;
      step_envelope (voice, 0, 60);
    }
  update_volume (voice);
  compute_step (voice);

  voice_active[voice] = 1;	/* Mark it active */

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return 0;
}

static int
setup_16bit_sampling (void)
{
  int fmt, fmt2;

  if ((audio_engines[devc->masterdev]->oformat_mask & AFMT_S16_NE))
    {
      devc->chendian = 0;	/* No byte swapping needed */
      fmt2 = fmt = AFMT_S16_NE;
    }
  else if ((audio_engines[devc->masterdev]->oformat_mask & AFMT_S16_OE))
    {
      devc->chendian = 1;	/* Swap bytes during output */
      fmt2 = fmt = AFMT_S16_OE;
    }
  else
    return 0;			/* 16 bits not supported */

  fmt =
    audio_engines[devc->masterdev]->d->adrv_set_format (devc->masterdev, fmt);
  if (fmt != fmt2)
    {
      cmn_err (CE_WARN,
	       "Endianess problems when setting up the device (%x/%x)\n", fmt,
	       fmt2);
      return 0;
    }
  audio_engines[devc->masterdev]->hw_parms.fmt = fmt;
  devc->bits = 16;
  devc->hw_format = fmt;
  DDB (cmn_err (CE_CONT, "Will use 16 bit target sample size\n"));
  return 1;
}

static int
setup_32bit_sampling (void)
{
  int fmt, fmt2;

  if ((audio_engines[devc->masterdev]->oformat_mask & AFMT_S32_NE))
    {
      devc->chendian = 0;	/* No byte swapping needed */
      fmt2 = fmt = AFMT_S32_NE;
    }
  else if ((audio_engines[devc->masterdev]->oformat_mask & AFMT_S32_OE))
    {
      devc->chendian = 1;	/* Swap bytes during output */
      fmt2 = fmt = AFMT_S32_OE;
    }
  else
    return 0;			/* 32 bits not supported */

  fmt =
    audio_engines[devc->masterdev]->d->adrv_set_format (devc->masterdev, fmt);
  if (fmt != fmt2)
    {
      cmn_err (CE_CONT,
	       "Endianess problems when setting up the device (%x/%x)\n", fmt,
	       fmt2);
      return 0;
    }
  audio_engines[devc->masterdev]->hw_parms.fmt = fmt;
  devc->bits = 32;
  devc->hw_format = fmt;
  return 1;
}

static int
setup_24bit_packed_sampling (void)
{
  int fmt, fmt2;

  fmt2 = fmt = AFMT_S24_PACKED;

  fmt =
    audio_engines[devc->masterdev]->d->adrv_set_format (devc->masterdev, fmt);
  if (fmt != fmt2)
    {
      cmn_err (CE_WARN,
	       "Endianess problems when setting up the device (%x/%x)\n", fmt,
	       fmt2);
      return 0;
    }
  audio_engines[devc->masterdev]->hw_parms.fmt = fmt;
  devc->bits = 24;
  devc->hw_format = fmt;
  return 1;
}

int
softoss_open_audiodev (void)
{
  extern int softoss_channels;
  int err;
  int frags = 0x7fff0007;	/* fragment size of 128 bytes */

  if (devc->masterdev_opened)
    {
      return 0;
    }

  softoss_failed = 0;
  devc->finfo.mode = OPEN_WRITE;
  if (devc->duplex_mode)
    devc->finfo.mode |= OPEN_READ;
  devc->finfo.acc_flags = 0;

  /* Turn ADEV_FIXEDRATE off at the master device */
  devc->saved_flags = audio_engines[devc->masterdev]->flags;
  devc->saved_srate = audio_engines[devc->masterdev]->fixed_rate;
  audio_engines[devc->masterdev]->flags &= ~ADEV_FIXEDRATE;
  audio_engines[devc->masterdev]->cooked_enable = 0;
  audio_engines[devc->masterdev]->fixed_rate = 0;
  if ((err =
       oss_audio_open_engine (devc->masterdev, OSS_DEV_DSP, &devc->finfo, 1,
			      0, NULL)) < 0)
    {
      audio_engines[devc->masterdev]->flags = devc->saved_flags;
      audio_engines[devc->masterdev]->fixed_rate = devc->saved_srate;
      return err;
    }

  strcpy (audio_engines[devc->masterdev]->cmd, "VMIX");
  audio_engines[devc->masterdev]->pid = 0;
  audio_engines[devc->masterdev]->cooked_enable = 0;
  audio_engines[devc->masterdev]->redirect_out = devc->first_virtdev;

/*
 * Sample format (endianess) detection
 */
  if (!setup_32bit_sampling ())
    if (!setup_16bit_sampling ())
      if (!setup_24bit_packed_sampling ())
	{
	  cmn_err (CE_WARN, "Fatal error in setting up the sample format\n");
	  oss_audio_release (devc->masterdev, &devc->finfo);
	  stop_record_engine (devc);
	  return -ENXIO;
	}

  devc->masterdev_opened = 1;

#ifdef OSS_BIG_ENDIAN
  softoss_channels == 2;
#endif
  if (softoss_channels == 2 || softoss_channels == 4 || softoss_channels == 5)
    devc->hw_channels = softoss_channels;

  /* Force the device to stereo before trying with (possibly) 4 channels */
  audio_engines[devc->masterdev]->d->adrv_set_channels (devc->masterdev, 2);

  devc->hw_channels = devc->channels =
    audio_engines[devc->masterdev]->d->adrv_set_channels (devc->masterdev,
							  devc->hw_channels);
  DDB (cmn_err
       (CE_CONT, "Using audio dev %d, speed %d, bits %d, channels %d\n",
	devc->masterdev, devc->speed, devc->bits, devc->channels));

  if (audio_engines[devc->masterdev]->flags & ADEV_FIXEDRATE)	/* GRC in action */
    devc->speed = audio_engines[devc->masterdev]->fixed_rate;

  devc->speed = oss_audio_set_rate (devc->masterdev, devc->speed);

  if (devc->speed <= 22050)
    frags = 0x7fff0004;

  audio_engines[devc->masterdev]->hw_parms.channels = devc->channels;
  audio_engines[devc->masterdev]->hw_parms.rate = devc->speed;
  audio_engines[devc->masterdev]->dmap_out->data_rate =
    devc->speed * devc->channels * devc->bits / 8;
  audio_engines[devc->masterdev]->dmap_out->frame_size =
    devc->channels * devc->bits / 8;

  oss_audio_ioctl (devc->masterdev, NULL, SNDCTL_DSP_SETFRAGMENT,
		   (ioctl_arg) & frags);
  oss_audio_ioctl (devc->masterdev, NULL, SNDCTL_DSP_GETBLKSIZE,
		   (ioctl_arg) & devc->fragsize);

  devc->multich = 0;
  devc->fivechan = 0;
  if (devc->channels > 2)
    {
      DDB (cmn_err
	   (CE_CONT, "Enabling multi channel mode, %d hw channels\n",
	    devc->hw_channels));
      devc->multich = 1;
      devc->channels = 4;
    }
  else if (devc->channels != 2)
    {
      oss_audio_release (devc->masterdev, &devc->finfo);
      stop_record_engine (devc);
      cmn_err (CE_NOTE, "A stereo soundcard is required\n");
      return 0;
    }

  if (devc->max_playahead >= audio_engines[devc->masterdev]->dmap_out->nfrags)
    devc->max_playahead = audio_engines[devc->masterdev]->dmap_out->nfrags;

  DDB (cmn_err (CE_CONT, "Using %d Hz sampling rate.\n", devc->speed));
  DDB (cmn_err (CE_CONT, "Using %d fragments of %d bytes\n",
		devc->max_playahead, devc->fragsize));
  init_engine (devc);
  return 0;
}

void
softoss_close_audiodev (void)
{
  devc->engine_state = ES_STOPPED;

  if (devc->masterdev_opened)
    {
      oss_audio_ioctl (devc->masterdev, NULL, SNDCTL_DSP_HALT, 0);
      oss_audio_release (devc->masterdev, &devc->finfo);
      stop_record_engine (devc);
      audio_engines[devc->masterdev]->flags = devc->saved_flags;
      audio_engines[devc->masterdev]->fixed_rate = devc->saved_srate;
    }
  devc->masterdev_opened = 0;
}

/*ARGSUSED*/
static int
softsyn_open (int synthdev, int mode)
{
  int err, voice;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (devc->softoss_opened)	/* Already opened */
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return 0;
    }
  devc->softoss_opened = 1;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

#ifdef DO_TIMINGS
  oss_do_timing ("****** Opening SoftOSS ********");
#endif
  if ((err = softoss_open_audiodev ()) < 0)
    {
      devc->softoss_opened = 0;
      return err;
    }

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  devc->sequencer_mode = mode;
  for (voice = 0; voice < MAX_VOICE; voice++)
    voice_alloc->map[voice] = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
#ifdef DO_TIMINGS
  oss_do_timing ("****** Opening SoftOSS complete ********");
#endif
  return 0;
}

/*ARGSUSED*/
static void
softsyn_close (int synthdev)
{
#ifdef DO_TIMINGS
  oss_do_timing ("****** Closing SoftOSS ********");
#endif
  if (devc->nr_opened_audio_engines == 0)
    softoss_close_audiodev ();
  devc->softoss_opened = 0;
#ifdef DO_TIMINGS
  oss_do_timing ("****** Closing SoftOSS complete ********");
#endif
}

/*ARGSUSED*/
static void
softsyn_hw_control (int dev, unsigned char *event_rec)
{
}

/*ARGSUSED*/
static int
softsyn_load_patch (int dev, int format, uio_t * addr,
		    int offs, int count, int pmgr_flag)
{
  struct patch_info *patch = NULL;

  int i, instr;
  long sizeof_patch;
  int memlen, adj;
  unsigned short data;
  short *wave = NULL;

  sizeof_patch = (long) &patch->data[0] - (long) patch;	/* Header size */

  if (format != GUS_PATCH)
    {
      cmn_err (CE_WARN, "Invalid patch format (key) 0x%x\n", format);
      return -EINVAL;
    }

  if (count < sizeof_patch)
    {
      cmn_err (CE_WARN, "Patch header too short\n");
      return -EINVAL;
    }

  count -= sizeof_patch;

  if (devc->nrsamples >= MAX_SAMPLE)
    {
      cmn_err (CE_WARN, "Sample table full\n");
      return -ENOSPC;
    }

  /*
   * Copy the header from user space but ignore the first bytes which have
   * been transferred already.
   */

  patch = KERNEL_MALLOC (sizeof (*patch));

  if (patch == NULL)
    {
      cmn_err (CE_WARN, "Out of memory\n");
      return -ENOSPC;
    }

  uiomove ((caddr_t) & patch[offs], sizeof_patch - offs, UIO_WRITE, addr);

  if (patch->mode & WAVE_ROM)
    {
      KERNEL_FREE (patch);
      cmn_err (CE_WARN, "Unsupported ROM patch\n");
      return -EINVAL;
    }

  instr = patch->instr_no;

  if (instr < 0 || instr > MAX_PATCH)
    {
      cmn_err (CE_WARN, "Invalid program number %d\n", instr);
      KERNEL_FREE (patch);
      return -EINVAL;
    }

  if (count < patch->len)
    {
      cmn_err (CE_WARN, "Patch record too short (%d<%d)\n",
	       count, (int) patch->len);
      patch->len = count;
    }

  if (patch->len <= 0 || patch->len > (devc->ram_size - devc->ram_used))
    {
      cmn_err (CE_WARN, "Invalid sample length %d\n", (int) patch->len);
      cmn_err (CE_WARN, "Possibly out of memory\n");
      KERNEL_FREE (patch);
      return -EINVAL;
    }

  if (patch->mode & WAVE_LOOPING)
    {
      if (patch->loop_start < 0 || patch->loop_start >= patch->len)
	{
	  cmn_err (CE_WARN, "Invalid loop start %d\n", patch->loop_start);
	  KERNEL_FREE (patch);
	  return -EINVAL;
	}

      if (patch->loop_end < patch->loop_start || patch->loop_end > patch->len)
	{
	  cmn_err (CE_WARN, "Invalid loop start or end point (%d, %d)\n",
		   patch->loop_start, patch->loop_end);
	  KERNEL_FREE (patch);
	  return -EINVAL;
	}
    }

/* 
 * Next load the wave data to memory
 */

  memlen = patch->len;
  adj = 1;

  if (!(patch->mode & WAVE_16_BITS))
    memlen *= 2;
  else
    adj = 2;

  wave = KERNEL_MALLOC (memlen);

  if (wave == NULL)
    {
      cmn_err (CE_WARN, "Can't allocate %d bytes of mem for a sample\n",
	       memlen);
      KERNEL_FREE (patch);
      return -ENOSPC;
    }

  for (i = 0; i < memlen / 2; i++)	/* Handle words */
    {
      unsigned char tmp;

      data = 0;

      if (patch->mode & WAVE_16_BITS)
	{
	  uiomove (&tmp, 1, UIO_WRITE, addr);	/* Get lsb */
	  data = tmp;
	  uiomove (&tmp, 1, UIO_WRITE, addr);	/* Get msb */
	  if (patch->mode & WAVE_UNSIGNED)
	    tmp ^= 0x80;	/* Convert to signed */
	  data |= (tmp << 8);
	}
      else
	{
	  uiomove (&tmp, 1, UIO_WRITE, addr);
	  if (patch->mode & WAVE_UNSIGNED)
	    tmp ^= 0x80;	/* Convert to signed */
	  data = (tmp << 8);	/* Convert to 16 bits */
	}

      wave[i] = (short) data;
    }

  devc->ram_used += patch->len;
/*
 * Convert pointers to 16 bit indexes
 */
  patch->len /= adj;
  patch->loop_start /= adj;
  patch->loop_end /= adj;

/*
 * Finally link the loaded patch to the chain
 */

  patch->key = devc->programs[instr];
  devc->programs[instr] = devc->nrsamples;
  devc->wave[devc->nrsamples] = (short *) wave;
  devc->samples[devc->nrsamples++] = patch;

  return 0;
}

/*ARGSUSED*/
static void
softsyn_panning (int dev, int voice, int pan)
{
  if (voice < 0 || voice > devc->maxvoice)
    return;

  if (pan < -128)
    pan = -128;
  if (pan > 127)
    pan = 127;

  softoss_voices[voice].panning = pan;
  if (voice_active[voice])
    update_volume (voice);
}

/*ARGSUSED*/
static void
softsyn_frontrear (int dev, int voice, int pan)
{
  if (voice < 0 || voice > devc->maxvoice)
    return;

  if (pan < -128)
    pan = -128;
  if (pan > 127)
    pan = 127;

  softoss_voices[voice].frontrear = pan;
  if (voice_active[voice])
    update_volume (voice);
}

/*ARGSUSED*/
static void
softsyn_volume_method (int dev, int mode)
{
}

/*ARGSUSED*/
static void
softsyn_aftertouch (int dev, int voice, int pressure)
{
  if (voice < 0 || voice > devc->maxvoice)
    return;

  if (voice_active[voice])
    update_volume (voice);
}

static void
softsyn_controller (int dev, int voice, int ctrl_num, int value)
{
  oss_native_word flags;
  if (voice < 0 || voice > devc->maxvoice)
    return;
  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  switch (ctrl_num)
    {
    case CTRL_PITCH_BENDER:
      softoss_voices[voice].bender = value;

      if (voice_active[voice])
	compute_step (voice);	/* Update pitch */
      break;

    case CTL_SUSTAIN:
      if (value >= 64)		/* Sustain on */
	{
	  if (!softoss_voices[voice].releasing)
	    softoss_voices[voice].sustain_mode |= 1;	/* Sustain on */
	}
      else
	{
	  if (softoss_voices[voice].sustain_mode & 2)	/* Note off pending */
	    {
	      softoss_voices[voice].sustain_mode = 0;	/* Sustain off */
	      softsyn_kill_note (dev, voice, 0, 60);
	    }

	  softoss_voices[voice].sustain_mode = 0;	/* Sustain off */
	}
      break;

    case CTRL_PITCH_BENDER_RANGE:
      softoss_voices[voice].bender_range = value;
      break;
    case CTL_EXPRESSION:
      value /= 128;
      softoss_voices[voice].expression_vol = value;
      if (voice_active[voice])
	update_volume (voice);
      break;

    case CTRL_EXPRESSION:
      softoss_voices[voice].expression_vol = value;
      if (voice_active[voice])
	update_volume (voice);
      break;

    case CTL_PAN:
      softsyn_panning (dev, voice, (value * 2) - 128);
      break;

    case CTL_GENERAL_PURPOSE4:
      softsyn_frontrear (dev, voice, (value * 2) - 128);
      break;

    case CTL_MAIN_VOLUME:
      value = (value * 100) / 16383;
      softoss_voices[voice].main_vol = value;
      if (voice_active[voice])
	update_volume (voice);
      break;

    case CTRL_MAIN_VOLUME:
      softoss_voices[voice].main_vol = value;
      if (voice_active[voice])
	update_volume (voice);
      break;

    default:
      break;
    }

  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static void
softsyn_bender (int dev, int voice, int value)
{
  if (voice < 0 || voice > devc->maxvoice)
    return;

  softoss_voices[voice].bender = value - 8192;
  if (voice_active[voice])
    compute_step (voice);	/* Update pitch */
}

/*ARGSUSED*/
static int
softsyn_alloc_voice (int dev, int chn, int note,
		     struct voice_alloc_info *alloc)
{
  int i, p, best = -1, best_time = 0x7fffffff;
  int key = (chn << 8) | (note + 1);

  p = alloc->ptr;

/*
 * Check for voice(s) already playing this channel and note
 */
  for (i = 0; i < alloc->max_voice; i++)
    {
      if (alloc->map[p] == key)
	{
	  alloc->ptr = p;
	  voice_active[p] = 0;
	  alloc->ptr = p;
	  init_voice (devc, p);
	  return p;
	}
    }
  /*
   * Then look for a completely stopped voice
   */

  for (i = 0; i < alloc->max_voice; i++)
    {
      if (alloc->map[p] == 0)
	{
	  alloc->ptr = p;
	  voice_active[p] = 0;
	  alloc->ptr = p;
	  return p;
	}
      if (alloc->alloc_times[p] < best_time)
	{
	  best = p;
	  best_time = alloc->alloc_times[p];
	}
      p = (p + 1) % alloc->max_voice;
    }

  /*
   * Then look for a releasing voice
   */

  for (i = 0; i < alloc->max_voice; i++)
    {
      if (softoss_voices[p].releasing)
	{
	  alloc->ptr = p;
	  voice_active[p] = 0;
	  init_voice (devc, p);
	  return p;
	}
      p = (p + 1) % alloc->max_voice;
    }

  if (best >= 0)
    p = best;

  alloc->ptr = p;
  voice_active[p] = 0;
  return p;
}

/*ARGSUSED*/
static void
softsyn_setup_voice (int dev, int voice, int chn)
{
#ifdef CONFIG_SEQUENCER
  struct channel_info *info = &synth_devs[dev]->chn_info[chn];
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  /* init_voice(devc, voice); */
  softsyn_set_instr (dev, voice, info->pgm_num);

  softoss_voices[voice].expression_vol = info->controllers[CTL_EXPRESSION];	/* Just MSB */
  softoss_voices[voice].main_vol =
    (info->controllers[CTL_MAIN_VOLUME] * 100) / (unsigned) 128;
  softsyn_panning (dev, voice, (info->controllers[CTL_PAN] * 2) - 128);
  softsyn_frontrear (dev, voice,
		     (info->controllers[CTL_GENERAL_PURPOSE4] * 2) - 128);
  softoss_voices[voice].bender = 0;	/* info->bender_value; */
  softoss_voices[voice].bender_range = info->bender_range;
  if (info->controllers[CTL_SUSTAIN] >= 64)
    softoss_voices[voice].sustain_mode = 1;

  if (chn == 9)
    softoss_voices[voice].percussive_voice = 1;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
#endif
}

/*ARGSUSED*/
static void
softsyn_reset (int devno)
{
  int i;

  for (i = 0; i < devc->maxvoice; i++)
    init_voice (devc, i);
}

static struct synth_operations softsyn_operations = {
  "SoftOSS",
  &softsyn_info,
  0,
  SYNTH_TYPE_SAMPLE,
  0,
  softsyn_open,
  softsyn_close,
  softsyn_ioctl,
  softsyn_kill_note,
  softsyn_start_note,
  softsyn_set_instr,
  softsyn_reset,
  softsyn_hw_control,
  softsyn_load_patch,
  softsyn_aftertouch,
  softsyn_controller,
  softsyn_panning,
  softsyn_volume_method,
  softsyn_bender,
  softsyn_alloc_voice,
  softsyn_setup_voice
};

/***********************************
 * Audio routines 
 ***********************************/

typedef struct
{
  int open_mode;
  int speed, bits, channels;
  int avoice_nr;
  int fixed_rate;

  int duplex;
}
vmix_portc;

static int
vmix_set_rate (int dev, int arg)
{
  vmix_portc *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->speed;

  if (portc->fixed_rate || portc->duplex)
    {
      audio_engines[dev]->fixed_rate = arg = devc->speed;
      audio_engines[dev]->min_rate = devc->speed;
      audio_engines[dev]->max_rate = devc->speed;
      audio_engines[dev]->flags |= ADEV_FIXEDRATE;
    }
  else
    {
      audio_engines[dev]->fixed_rate = 0;
      audio_engines[dev]->flags &= ~ADEV_FIXEDRATE;
      audio_engines[dev]->min_rate = 5000;
      audio_engines[dev]->max_rate = devc->speed;
    }

  if (arg > devc->speed)
    arg = devc->speed;
  if (arg < 5000)
    arg = 5000;
  portc->speed = arg;
  return portc->speed;
}

static short
vmix_set_channels (int dev, short arg)
{
  vmix_portc *portc = audio_engines[dev]->portc;

  if (devc->duplex_mode)
    return portc->channels = 2;

  if ((arg != 1) && (arg != 2))
    return portc->channels;
  portc->channels = arg;

  return portc->channels;
}

static unsigned int
vmix_set_format (int dev, unsigned int arg)
{
  vmix_portc *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->bits;

  if (!(arg & AFMT_S16_NE))
    return portc->bits;
  portc->bits = arg;

  return portc->bits;
}

static int
vmix_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  int val;

  switch (cmd)
    {
    case SNDCTL_DSP_SETPLAYVOL:
      val = *arg;
      return *arg = (softoss_avol (audio_engines[dev]->mixer_dev,
				   portc->avoice_nr, SNDCTL_MIX_WRITE, val));
      break;

    case SNDCTL_DSP_GETPLAYVOL:
      return *arg = (softoss_avol (audio_engines[dev]->mixer_dev,
				   portc->avoice_nr, SNDCTL_MIX_READ, 0));
      break;

    case SNDCTL_DSP_COOKEDMODE:
      val = *arg;
      if (val)
	{
	  portc->fixed_rate = softoss_use_src;
	}
      else
	portc->fixed_rate = 0;

      if (!portc->fixed_rate)
	{
	  audio_engines[dev]->flags &= ~ADEV_FIXEDRATE;
	  audio_engines[dev]->fixed_rate = 0;
	}

      return 0;
      break;
    }

  return -EINVAL;
}

static void
vmix_reset_input (int dev)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  int voice = portc->avoice_nr;

  if (!(portc->open_mode & OPEN_READ))
    return;
  recvoice_active[voice] = 0;
}

static void
vmix_reset_output (int dev)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  int voice = portc->avoice_nr;

  if (!(portc->open_mode & OPEN_WRITE))
    return;

  avoice_active[voice] = 0;
}

static void
vmix_reset (int dev)
{
  vmix_reset_input (dev);
  vmix_reset_output (dev);
}

static int
vmix_open (int dev, int mode, int open_flags)
{
  int err;
  vmix_portc *portc = audio_engines[dev]->portc;
  int voice = portc->avoice_nr;
  avoice_info *v;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (portc->open_mode != 0)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return -EBUSY;
    }
  portc->open_mode = mode;
  if (mode & OPEN_READ)
    devc->opened_inputs++;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  if ((err = softoss_open_audiodev ()) < 0)
    {
      if (mode & OPEN_READ)
	devc->opened_inputs--;
      portc->open_mode = 0;
      return err;
    }

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  devc->nr_opened_audio_engines++;
  v = &softoss_avoices[voice];
  if (devc->autoreset)
    {
      v->left_vol = 100;
      v->right_vol = 100;
      v->active_left_vol = devc->pcm_volume_left;
      v->active_right_vol = devc->pcm_volume_right;
    }
  if (softoss_use_src)
    {
      audio_engines[dev]->flags |= ADEV_FIXEDRATE;
      portc->fixed_rate = 1;
    }
  else
    {
      audio_engines[dev]->flags &= ~ADEV_FIXEDRATE;
      portc->fixed_rate = 0;
    }

  portc->duplex = 0;

  if (devc->duplex_mode && mode & OPEN_READ)
    {
      portc->duplex = 1;
      audio_engines[dev]->flags |= ADEV_FIXEDRATE;
      portc->fixed_rate = 1;
    }

  if (open_flags & OF_MMAP)
    {
      audio_engines[dev]->flags &= ~ADEV_FIXEDRATE;
      portc->fixed_rate = 0;
    }

  mixer_devs[devc->mixer_dev]->modify_counter++;	/* Force update of mixer */

  audio_engines[dev]->rate_source =
    audio_engines[devc->masterdev]->rate_source;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  softoss_start_engine (devc);

  return 0;
}

static void
vmix_close (int dev, int mode)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  oss_native_word flags;
  int do_close = 0;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  vmix_reset (dev);
  portc->open_mode = 0;
  if (mode & OPEN_READ)
    devc->opened_inputs--;
  devc->nr_opened_audio_engines--;
  if (devc->nr_opened_audio_engines == 0 && devc->softoss_opened == 0)
    do_close = 1;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  if (do_close)
    softoss_close_audiodev ();
  mixer_devs[devc->mixer_dev]->modify_counter++;	/* Force update of mixer */
}

/*ARGSUSED*/
static void
vmix_output_block (int dev, oss_native_word buf, int count,
		   int fragsize, int intrflag)
{
}

/*ARGSUSED*/
static void
vmix_start_input (int dev, oss_native_word buf, int count, int fragsize,
		  int intrflag)
{
}

static void
vmix_trigger (int dev, int state)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  int voice = portc->avoice_nr;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  if (portc->open_mode & OPEN_WRITE)
    {
      avoice_active[voice] = !!(state & PCM_ENABLE_OUTPUT);
    }

  if (portc->open_mode & OPEN_READ)
    {
      recvoice_active[voice] = !!(state & PCM_ENABLE_INPUT);
    }
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static int
vmix_prepare_for_input (int dev, int bsize, int bcount)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  adev_t *adev;
  int voice = portc->avoice_nr;
  recvoice_info *v;
  v = &softoss_recvoices[voice];

  adev = audio_engines[dev];

  if (!devc->duplex_mode)
    {
      cmn_err (CE_WARN, "Audio device %d is playback only\n", dev);
      return -ENOTSUP;
    }

  v->channels = adev->hw_parms.channels;

  switch (portc->bits + portc->channels)
    {
    case AFMT_S16_NE + 1:
      v->mixer = recmix_16_mono_ne;
      break;
    case AFMT_S16_NE + 2:
      v->mixer = recmix_16_stereo_ne;
      break;
    }

  return 0;
}

/*ARGSUSED*/
static int
vmix_prepare_for_output (int dev, int bsize, int bcount)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  int voice = portc->avoice_nr;
  int samplesize;
  dmap_t *dmap = audio_engines[dev]->dmap_out;
  avoice_info *v;

  vmix_reset_output (dev);

  if (dmap->dmabuf == NULL)
    return -ENOSPC;
  /* dmap->applic_profile = APF_CPUINTENS; */

  v = &softoss_avoices[voice];

  samplesize = 1;
  if (portc->bits == AFMT_S16_NE)
    samplesize *= 2;
  samplesize *= portc->channels;

#if 1
  /*
   * Disable GRC if the device has been mmapped
   */

  if ((dmap->mapping_flags & DMA_MAP_MAPPED)
      && (audio_engines[dev]->flags & ADEV_FIXEDRATE))
    {
      audio_engines[dev]->flags &= ~ADEV_FIXEDRATE;
      portc->fixed_rate = 0;
    }
#endif
/*
 * Compute step based on the requested (virtual) sampling rate and the
 * actual (hardware) sampling rate.
 */
  v->step = (((portc->speed << 9) + (devc->speed >> 1)) / devc->speed);

  v->wave = (short *) dmap->dmabuf;
  v->ptr = 0;
  v->samplesize = samplesize;
  v->endloop = (dmap->bytes_in_use / samplesize) << 9;
  v->fragsize = dmap->fragment_size / samplesize;
  v->fragmodulo = dmap->bytes_in_use;
  if (portc->bits == AFMT_S16_NE)
    v->fragmodulo /= 2;

  if (portc->bits == AFMT_S16_NE)
    {				/* 16 bit data */
      if (portc->channels == 1)
	v->mixer = mix_avoice_mono16;
      else
	v->mixer = mix_avoice_stereo16;
    }
  else
    {				/* 8 bit data */
      if (portc->channels == 1)
	v->mixer = mix_avoice_mono8;
      else
	v->mixer = mix_avoice_stereo8;
    }

#ifdef USE_DCKILL
  memset (v->DCfeedbackBuf, 0, sizeof (DCfeedbackBuf_t));
  memset (v->DCfeedbackVal, 0, sizeof (DCfeedbackVal_t));
  v->DCptr = 0;
#endif

  return 0;
}

/*ARGSUSED*/
static int
vmix_alloc_buffer (int dev, dmap_t * dmap, int direction)
{
#define MY_BUFFSIZE (64*1024)
  if (dmap->dmabuf != NULL)
    return 0;
  dmap->dmabuf_phys = 0;
  dmap->dmabuf = KERNEL_MALLOC (MY_BUFFSIZE);
  if (dmap->dmabuf == NULL)
    return -ENOSPC;
  dmap->buffsize = MY_BUFFSIZE;

  return 0;
}

/*ARGSUSED*/
static int
vmix_free_buffer (int dev, dmap_t * dmap, int direction)
{
  if (dmap->dmabuf == NULL)
    return 0;
  KERNEL_FREE (dmap->dmabuf);

  dmap->dmabuf = NULL;
  return 0;
}

/*ARGSUSED*/
static int
vmix_get_output_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
  vmix_portc *portc = audio_engines[dev]->portc;
  int voice = portc->avoice_nr;
  avoice_info *v;
  int pos;

  v = &softoss_avoices[voice];

  pos = v->ptr >> 9;

  return pos * v->samplesize;
}

static int
vmix_local_qlen (int dev)
{
  vmix_portc *portc;
  int samplesize;
  int len = 0;

  portc = audio_engines[dev]->portc;

  samplesize = 1;
  if (portc->bits == AFMT_S16_NE)
    samplesize *= 2;
  samplesize *= portc->channels;

  /* Compute the number of samples in the physical device */
  oss_audio_ioctl (devc->masterdev, NULL, SNDCTL_DSP_GETODELAY,
		   (ioctl_arg) & len);

  if (devc->bits == 16)
    len /= 2;			/* 16 bit samples */
  else
    len /= 4;			/* 32 bit samples */
  len /= devc->hw_channels;

  /* Convert # of samples to local bytes */

  len *= portc->channels;

  if (portc->bits == AFMT_S16_NE)
    len *= 2;

  return len;
}

static audiodrv_t vmix_driver = {
  vmix_open,
  vmix_close,
  vmix_output_block,
  vmix_start_input,
  vmix_ioctl,
  vmix_prepare_for_input,
  vmix_prepare_for_output,
  vmix_reset,
  vmix_local_qlen,
  NULL,
  vmix_reset_input,
  vmix_reset_output,
  vmix_trigger,
  vmix_set_rate,
  vmix_set_format,
  vmix_set_channels,
  NULL,
  NULL,
  NULL,
  NULL,
  vmix_alloc_buffer,
  vmix_free_buffer,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  vmix_get_output_buffer_pointer
};

static void
attach_avoice (void)
{
  int adev;
  int opts, n = devc->nr_avoices;
  vmix_portc *portc;
  char tmp[64];

  portc = PMALLOC (devc->osdev, sizeof (*portc));
  if (portc == 0)
    {
      cmn_err (CE_WARN, "Can't allocate memory for avoice\n");
      return;
    }
  opts = ADEV_VIRTUAL | ADEV_DEFAULT;

  if (!devc->duplex_mode)
    opts |= ADEV_NOINPUT;
#ifndef NO_QSRC
  if (softoss_use_src)
    {
      opts |= ADEV_FIXEDRATE;
    }
#endif
  if (n > 0)
    opts |= ADEV_SHADOW;
  else
    devc->first_virtdev = num_audio_engines;

  if (devc->duplex_mode)
    strcpy (tmp, AUDIO_DRIVER_NAME " Rec/Play");
  else
    strcpy (tmp, AUDIO_DRIVER_NAME " Playback");

  if ((adev = oss_install_audiodev (OSS_AUDIO_DRIVER_VERSION,
				    devc->osdev,
				    devc->master_osdev,
				    tmp,
				    &vmix_driver,
				    sizeof (audiodrv_t),
				    opts, AFMT_S16_NE, devc, -1)) < 0)
    {
      return;
    }

  avoice_active[n] = 0;
  recvoice_active[n] = 0;
  audio_engines[adev]->mixer_dev = devc->mixer_dev;
  audio_engines[adev]->portc = portc;
  audio_engines[adev]->min_rate = 5000;
  audio_engines[adev]->max_rate = 48000;
  audio_engines[adev]->caps |= PCM_CAP_FREERATE;
  portc->open_mode = 0;
  portc->avoice_nr = n;
  portc->speed = 8000;
  portc->bits = 8;
  portc->channels = 1;
  devc->nr_avoices++;

  softoss_avoices[n].audiodev = adev;
  softoss_avoices[n].wave = NULL;
  softoss_avoices[n].ptr = 0;
  softoss_avoices[n].step = 0;
  softoss_avoices[n].left_vol = 100;
  softoss_avoices[n].right_vol = 100;
  softoss_avoices[n].active_left_vol = 100;
  softoss_avoices[n].active_right_vol = 100;
  softoss_avoices[n].mixer = NULL;

  softoss_recvoices[n].audiodev = adev;
}

/* 
 * Mixer support
 */
static int default_mixer_levels[32] = {
  0x0000,			/* Master Volume */
  0x0000,			/* Bass */
  0x0000,			/* Treble */
  0x6464,			/* Synth */
  0x6464,			/* PCM */
  0x0000,			/* PC Speaker */
  0x0000,			/* Ext Line */
  0x0000,			/* Mic */
  0x0000,			/* CD */
  0x0000,			/* Recording monitor */
  0x0000,			/* Second PCM */
  0x0000,			/* Recording level */
  0x0000,			/* Input gain */
  0x0000,			/* Output gain */
  0x0000,			/* Line1 */
  0x0000,			/* Line2 */
  0x0000			/* Line3 (usually line in) */
};

#define MIXER_DEVS (SOUND_MASK_PCM | SOUND_MASK_SYNTH)

static int
softoss_mixer_get (softoss_devc * devc, int audiodev, int dev)
{
  if (!((1 << dev) & MIXER_DEVS))
    return -EINVAL;

  switch (dev)
    {
    case SOUND_MIXER_PCM:
      if (audiodev >= 0 && audiodev < num_audio_engines)
	{
	  int voice;

	  for (voice = 0; voice < devc->nr_avoices; voice++)
	    if (softoss_avoices[voice].audiodev == audiodev)
	      {
		int left, right;

		left = softoss_avoices[voice].left_vol;
		right = softoss_avoices[voice].right_vol;
		return left | (right << 8);
	      }
	}

      return (devc->pcm_volume_left & 0x00ff) |
	((devc->pcm_volume_right & 0x00ff) << 8);
      break;

    case SOUND_MIXER_SYNTH:
      return (devc->synth_volume_left & 0x00ff) |
	((devc->synth_volume_right & 0x00ff) << 8);
      break;

    default:
      return -EINVAL;
    }
}

static int
softoss_mixer_set (softoss_devc * devc, int audiodev, int dev, int value)
{
  int left, right, voice;

  if (!((1 << dev) & MIXER_DEVS))
    return -EINVAL;

  left = value & 0x00ff;
  right = (value >> 8) & 0x00ff;

  if (left < 0)
    left = 0;
  if (left > 100)
    left = 100;
  if (right < 0)
    right = 0;
  if (right > 100)
    right = 100;
  value = (left & 0x00ff) | ((right & 0x00ff) << 8);

  switch (dev)
    {
    case SOUND_MIXER_PCM:

      if (audiodev >= 0 && audiodev < num_audio_engines)
	{
	  for (voice = 0; voice < devc->nr_avoices; voice++)
	    if (softoss_avoices[voice].audiodev == audiodev)
	      {
		softoss_avoices[voice].left_vol = left;
		softoss_avoices[voice].right_vol = right;

		softoss_avoices[voice].active_left_vol =
		  (softoss_avoices[voice].left_vol * devc->pcm_volume_left) /
		  100;
		softoss_avoices[voice].active_right_vol =
		  (softoss_avoices[voice].right_vol *
		   devc->pcm_volume_right) / 100;
		return value;
		break;
	      }
	}

      devc->pcm_volume_left = left;
      devc->pcm_volume_right = right;
      devc->levels[dev] = value;

      for (voice = 0; voice < devc->nr_avoices; voice++)
	{
	  /* Update the voices */
	  softoss_avoices[voice].active_left_vol =
	    (softoss_avoices[voice].left_vol * devc->pcm_volume_left) / 100;
	  softoss_avoices[voice].active_right_vol =
	    (softoss_avoices[voice].right_vol * devc->pcm_volume_right) / 100;
	}
      break;

    case SOUND_MIXER_SYNTH:
      devc->synth_volume_left = left;
      devc->synth_volume_right = right;
      devc->levels[dev] = value;

      if (devc->softoss_opened)
	for (voice = 0; voice < devc->maxvoice; voice++)
	  if (voice_active[voice])
	    update_volume (voice);
      break;

    default:
      return -EINVAL;
    }

  return value;
}

static int
softoss_mixer_ioctl (int dev, int audiodev, unsigned int cmd, ioctl_arg arg)
{
  softoss_devc *devc = mixer_devs[dev]->devc;

  if (((cmd >> 8) & 0xff) == 'M')
    {
      int val;

      if (IOC_IS_OUTPUT (cmd))
	switch (cmd & 0xff)
	  {
	  case SOUND_MIXER_RECSRC:
	    return *arg = (0);
	    break;

	  default:
	    val = *arg;
	    return *arg = softoss_mixer_set (devc, audiodev, cmd & 0xff, val);
	  }
      else
	switch (cmd & 0xff)	/*
				 * Return parameters
				 */
	  {

	  case SOUND_MIXER_RECSRC:
	    return *arg = (0);
	    break;

	  case SOUND_MIXER_DEVMASK:
	    return *arg = (MIXER_DEVS);
	    break;

	  case SOUND_MIXER_STEREODEVS:
	    return *arg = (MIXER_DEVS);
	    break;

	  case SOUND_MIXER_RECMASK:
	    return *arg = (0);
	    break;

	  case SOUND_MIXER_CAPS:
	    return *arg = (SOUND_CAP_EXCL_INPUT);
	    break;

	  default:
	    return *arg = softoss_mixer_get (devc, audiodev, cmd & 0xff);
	  }
    }
  else
    return -EINVAL;
}

static mixer_driver_t softoss_mixer_driver = {
  softoss_mixer_ioctl
};

static int softoss_mix_init (int dev);

static void
softsyn_attach_audio (void)
{
  int i;
  int my_mixer;

  if (num_audio_engines < 1)
    return;

  if ((my_mixer = oss_install_mixer (OSS_MIXER_DRIVER_VERSION,
				     devc->osdev,
				     devc->master_osdev,
				     "Virtual Mixer",
				     &softoss_mixer_driver,
				     sizeof (mixer_driver_t), devc)) >= 0)
    {
      mixer_devs[my_mixer]->devc = devc;
      mixer_devs[my_mixer]->caps = MIXER_CAP_VIRTUAL;
      devc->levels = load_mixer_volumes ("SOFTOSS", default_mixer_levels, 1);
      softoss_mixer_set (devc, SOUND_MIXER_PCM, -1,
			 devc->levels[SOUND_MIXER_PCM]);
      softoss_mixer_set (devc, SOUND_MIXER_SYNTH, -1,
			 devc->levels[SOUND_MIXER_SYNTH]);
      devc->mixer_dev = my_mixer;
      mixer_devs[my_mixer]->priority = 1;	/* Low priority default mixer candidate */
      mixer_ext_set_init_fn (my_mixer, softoss_mix_init,
			     2 * softoss_devices + 50);
    }

  devc->autoreset = 1;
  for (i = 0; i < softoss_devices; i++)
    attach_avoice ();

  if (softoss_loopdevs > 0)
    softoss_install_loop (devc);
}

static int
softoss_avol (int dev, int ctrl, unsigned int cmd, int value)
{
/*
 * Access function for software mixing channels
 */
  softoss_devc *devc = mixer_devs[dev]->devc;
  int left, right;

  if (ctrl < 0 || ctrl >= softoss_devices)
    return -EINVAL;

  if (cmd == SNDCTL_MIX_READ)
    {
      return (softoss_avoices[ctrl].left_vol & 0x00ff) |
	((softoss_avoices[ctrl].right_vol & 0x00ff) << 8);
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
      left = value & 0x00ff;
      right = (value >> 8) & 0x00ff;
      if (left < 0)
	left = 0;
      if (left > 100)
	left = 100;
      if (right < 0)
	right = 0;
      if (right > 100)
	right = 100;

      softoss_avoices[ctrl].left_vol = left;
      softoss_avoices[ctrl].right_vol = right;
      softoss_avoices[ctrl].active_left_vol =
	(softoss_avoices[ctrl].left_vol * devc->pcm_volume_left) / 100;
      softoss_avoices[ctrl].active_right_vol =
	(softoss_avoices[ctrl].right_vol * devc->pcm_volume_right) / 100;

      return left | (right << 8);
    }

  return -EINVAL;
}

static const unsigned char peak_cnv[256] = {
  0, 18, 29, 36, 42, 47, 51, 54, 57, 60, 62, 65, 67, 69, 71, 72,
  74, 75, 77, 78, 79, 81, 82, 83, 84, 85, 86, 87, 88, 89, 89, 90,
  91, 92, 93, 93, 94, 95, 95, 96, 97, 97, 98, 99, 99, 100, 100, 101,
  101, 102, 102, 103, 103, 104, 104, 105, 105, 106, 106, 107, 107, 108, 108,
  108,
  109, 109, 110, 110, 110, 111, 111, 111, 112, 112, 113, 113, 113, 114, 114,
  114,
  115, 115, 115, 115, 116, 116, 116, 117, 117, 117, 118, 118, 118, 118, 119,
  119,
  119, 119, 120, 120, 120, 121, 121, 121, 121, 122, 122, 122, 122, 122, 123,
  123,
  123, 123, 124, 124, 124, 124, 125, 125, 125, 125, 125, 126, 126, 126, 126,
  126,
  127, 127, 127, 127, 127, 128, 128, 128, 128, 128, 129, 129, 129, 129, 129,
  130,
  130, 130, 130, 130, 130, 131, 131, 131, 131, 131, 131, 132, 132, 132, 132,
  132,
  132, 133, 133, 133, 133, 133, 133, 134, 134, 134, 134, 134, 134, 134, 135,
  135,
  135, 135, 135, 135, 135, 136, 136, 136, 136, 136, 136, 136, 137, 137, 137,
  137,
  137, 137, 137, 138, 138, 138, 138, 138, 138, 138, 138, 139, 139, 139, 139,
  139,
  139, 139, 139, 140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 141,
  141,
  141, 141, 141, 141, 142, 142, 142, 142, 142, 142, 142, 142, 142, 143, 143,
  143,
  143, 143, 143, 143, 143, 143, 144, 144, 144, 144, 144, 144, 144, 144, 144,
  144,
};

/*ARGSUSED*/
static int
softoss_vu (int dev, int ctrl, unsigned int cmd, int value)
{
/*
 * Access function for PCM VU meters
 */
  int left, right;
  avoice_info *v;

  if (ctrl < 0 || ctrl >= softoss_devices)
    return -EINVAL;

  if (cmd == SNDCTL_MIX_READ)
    {
      v = &softoss_avoices[ctrl];
      left = peak_cnv[v->left_vu];
      right = peak_cnv[v->right_vu];
      v->left_vu = v->right_vu = 0;
      return left | (right << 8);
    }

  return -EINVAL;
}

/*ARGSUSED*/
static int
softoss_control (int dev, int ctrl, unsigned int cmd, int value)
{
/*
 * Access function for VirtualMixer global controls
 */
  int left, right;

  if (ctrl < 0 || ctrl >= softoss_devices)
    return -EINVAL;

  if (cmd == SNDCTL_MIX_READ)
    switch (ctrl)
      {
      case 1:
	left = peak_cnv[devc->left_vu];
	right = peak_cnv[devc->right_vu];
	devc->left_vu = devc->right_vu = 0;
	return left | (right << 8);
	break;

      case 2:
	return devc->autoreset;
	break;
      }

  if (cmd == SNDCTL_MIX_WRITE)
    switch (ctrl)
      {
      case 2:
	return devc->autoreset = !!value;
	break;
      }

  return -EINVAL;
}

#ifdef USE_EQ
/*ARGSUSED*/
static int
softoss_eq (int dev, int ctrl, unsigned int cmd, int value)
{
  if (ctrl < 0 || ctrl > 5)
    return -EINVAL;

  if (cmd == SNDCTL_MIX_READ)
    {
      if (ctrl == 4)
	return devc->eq_bypass;
      if (ctrl == 5)
	return devc->eq_prescale;

      return devc->eq_bands[ctrl];
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
      if (ctrl == 4)
	return devc->eq_bypass = !!value;
      if (ctrl == 4)
	{
	  if (value < 0)
	    value = 0;
	  if (value > 255)
	    value = 255;
	  return devc->eq_prescale = value;
	}

      if (value < 0)
	value = 0;
      if (value > 255)
	value = 255;
      return devc->eq_bands[ctrl] = value;
    }
  return -EINVAL;
}
#endif

#ifdef USE_ALGORITHMS
static int init_effect_mixer (int dev, int parent);
#endif

static int
softoss_mix_init (int dev)
{
  int group, eqgroup, err, i;
  char tmp[16];

  group = 0;
  if ((err = mixer_ext_create_control (dev, group, 1, softoss_control,
				       MIXT_STEREOPEAK,
				       "-", 144, MIXF_READABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group, 2, softoss_control,
				       MIXT_ONOFF,
				       "VMIX_AUTORESET", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;


#ifdef USE_EQ
  if ((group = mixer_ext_create_group (dev, 0, "EFFECTS")) < 0)
    return group;
  if ((eqgroup = mixer_ext_create_group (dev, group, "SOFTOSS_EQ")) < 0)
    return group;
  if ((err = mixer_ext_create_control (dev, eqgroup, 5, softoss_eq,
				       MIXT_SLIDER,
				       "EQ_PRESCALE", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;
  if ((err = mixer_ext_create_control (dev, eqgroup, 0, softoss_eq,
				       MIXT_SLIDER,
				       "EW_LO", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, eqgroup, 1, softoss_eq,
				       MIXT_SLIDER,
				       "EW_MID", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, eqgroup, 2, softoss_eq,
				       MIXT_SLIDER,
				       "EW_HI", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, eqgroup, 3, softoss_eq,
				       MIXT_SLIDER,
				       "EW_XHI", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, eqgroup, 4, softoss_eq,
				       MIXT_ONOFF,
				       "EW_BYPASS", 1,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;
#endif

#ifdef USE_ALGORITHMS
  if ((err = init_effect_mixer (dev, group)) < 0)
    return err;
#endif

  for (i = 0; i < softoss_devices; i++)
    {
      if (!(i % 4))
	if ((group = mixer_ext_create_group (dev, 0, "SOFTOSS_VOICES")) < 0)
	  return group;

      sprintf (tmp, "@pcm%d", softoss_avoices[i].audiodev);
      if ((err = mixer_ext_create_control (dev, group, i, softoss_avol,
					   MIXT_STEREOSLIDER,
					   tmp, 100,
					   MIXF_READABLE | MIXF_WRITEABLE)) <
	  0)
	return err;

      if ((err = mixer_ext_create_control (dev, group, i, softoss_vu,
					   MIXT_STEREOPEAK,
					   "-", 144, MIXF_READABLE)) < 0)
	return err;

    }
  return 0;
}

#ifdef USE_ALGORITHMS
#include "kernel/nonfree/ossverb_int/ossverb.c"
#include "kernel/nonfree/ossfid_int/ossfid_int.c"
#include "kernel/nonfree/3dsur_int/3dsur_int.c"

typedef struct
{
  /* Reverb */
  int ossverb_enabled;
  int ossverb_scale;
  ossverb_t ossverb_struct;

  /* Ossfid */
  int ossfid_enabled;
  int ossfid_level;
  ossfid_t ossfid_struct1;
  ossfid_t ossfid_struct2;

  /* 3Dsurround */
  int p3d_enabled;
  int p3d_level;
  p3dstat_t p3d_struct;
} effects_info_t;

void
init_effects (softoss_devc * devc)
{
  int err;
  int sr = devc->speed / 1000;
  effects_info_t *efc;
  static effects_info_t global_efc;

  if (devc->effects_info != NULL)	/* Already done */
    return;

  efc = &global_efc;

  memset (efc, 0, sizeof (*efc));
  devc->effects_info = efc;

  if ((err = ossverb_init (&efc->ossverb_struct, devc->speed)) <= 0)
    {
      cmn_err (CE_WARN, "reverb init failed %d\n", err);
      efc->ossverb_enabled = 0;
    }
  else
    {
      efc->ossverb_struct.dry = 0;
      efc->ossverb_struct.wet = OSSVERB_SAT256_UNITY / 4;
      efc->ossverb_struct.scale0 = (OSSVERB_SAT256_UNITY * 48) / sr;
      efc->ossverb_struct.scale1 = (OSSVERB_SAT256_UNITY * 48) / sr;
      efc->ossverb_scale = 1;
      efc->ossverb_struct._clr_factor = 0;
      ossverb_updatedly (&efc->ossverb_struct);
    }

  if ((err = ossfid_init (&efc->ossfid_struct1, devc->speed)) <= 0 ||
      (err = ossfid_init (&efc->ossfid_struct2, devc->speed)) <= 0)
    {
      cmn_err (CE_WARN, "ossfid init failed %d\n", err);
      efc->ossfid_enabled = 0;
    }
  else
    {
      efc->ossfid_level = 128;
    }
}

static int
softoss_ossverb (int dev, int ctrl, unsigned int cmd, int value)
{
  softoss_devc *devc = mixer_devs[dev]->devc;
  int sr = devc->speed / 1000;
  int v;

  effects_info_t *efc;

  if ((efc = devc->effects_info) == NULL)
    return -EIO;

  if (cmd == SNDCTL_MIX_READ)
    switch (ctrl)
      {
      case 0:			/* Enable */
	return !efc->ossverb_enabled;
	break;

      case 1:			/* Dry */
	return (efc->ossverb_struct.dry * 255 / OSSVERB_SAT256_UNITY) * 4;
	break;

      case 2:			/* Wet */
	return efc->ossverb_struct.wet * 255 / OSSVERB_SAT256_UNITY;
	break;

      case 3:			/* Scale0 */
	return efc->ossverb_scale;
	break;

      case 100:		/* ossfid_enabled */
	return !efc->ossfid_enabled;
	break;

      case 101:		/* ossfid->xscale */
	return efc->ossfid_level;
	break;

      case 200:		/* 3Dsurround enable */
	return !efc->p3d_enabled;
	break;

      case 201:		/* 3Dsurround intensity */
	return efc->p3d_level;
	break;

      default:
	return -EINVAL;
      }

  if (cmd == SNDCTL_MIX_WRITE)
    switch (ctrl)
      {
      case 0:			/* Enable */
	return efc->ossverb_enabled = !value;
	break;

      case 1:			/* Dry */
	if (value < 0)
	  value = 0;
	if (value > 255)
	  value = 255;
	efc->ossverb_struct.dry = value * (OSSVERB_SAT256_UNITY / 255) / 4;
	return value;
	break;

      case 2:			/* Wet */
	if (value < 0)
	  value = 0;
	if (value > 255)
	  value = 255;
	efc->ossverb_struct.wet = value * (OSSVERB_SAT256_UNITY / 255);
	return value;
	break;

      case 3:			/* Scale */
	if (value < 0)
	  value = 0;
	if (value > 4)
	  value = 4;
	efc->ossverb_scale = value;

	v = OSSVERB_SAT256_UNITY / sr * 48;

	switch (value)
	  {
	  case 0:		/* Small room */
	    v = v / 2;
	    break;
	  case 1:		/* Medium room */
	    v = v;
	    break;
	  case 2:		/* Large room */
	    v = 13 * v / 10;
	    break;
	  case 3:		/* Small hall */
	    v = 15 * v / 10;
	    break;
	  case 4:		/* Large hall */
	    v = v * 2;
	    break;
	  }

	efc->ossverb_struct.scale0 = v;
	efc->ossverb_struct.scale1 = v;
	ossverb_updatedly (&efc->ossverb_struct);
	return value;
	break;

      case 100:		/* ossfid enable */
	efc->ossfid_enabled = !value;
	return value;
	break;

      case 101:		/* ossfid level */
	if (value < 0)
	  value = 0;
	if (value > 255)
	  value = 255;
	efc->ossfid_level = value;
	efc->ossfid_struct1.xvalue = 0x00ffffff / 128 * value;
	efc->ossfid_struct2.xvalue = 0x00ffffff / 128 * value;
	return value;
	break;

      case 200:		/* 3Dsurround enable */
	efc->p3d_enabled = !value;
	return value;
	break;

      case 201:		/* 3Dsurround level */
	if (value < 0)
	  value = 0;
	if (value > 255)
	  value = 255;
	efc->p3d_level = value;
	efc->p3d_struct.value = 0x1000000 / 255 * value;
	return value;
	break;

      default:
	return -EINVAL;
      }
  return -EINVAL;
}

static int
init_effect_mixer (int dev, int parent)
{
  int group, err;

  if (parent == 0)
    if ((parent = mixer_ext_create_group (dev, 0, "SOFTOSS_EFFECTS")) < 0)
      return parent;

#if 0
/*
 * OSSVERB
 */
  if ((group = mixer_ext_create_group (dev, parent, "REVERB")) < 0)
    return group;

  if ((err = mixer_ext_create_control (dev, group, 1, softoss_ossverb,
				       MIXT_SLIDER,
				       "DRY", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group, 2, softoss_ossverb,
				       MIXT_SLIDER,
				       "WET", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group, 3, softoss_ossverb,
				       MIXT_ENUM,
				       "PRESET", 5,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group, 0, softoss_ossverb,
				       MIXT_ONOFF,
				       "BYPASS", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;
#endif
/*
 * OSSFID
 */
  if ((group = mixer_ext_create_group (dev, parent, "FIDELITY")) < 0)
    return group;

  if ((err = mixer_ext_create_control (dev, group, 101, softoss_ossverb,
				       MIXT_SLIDER,
				       "LEVEL", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group, 100, softoss_ossverb,
				       MIXT_ONOFF,
				       "BYPASS", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

/*
 * 3DSurround
 */
  if ((group = mixer_ext_create_group (dev, parent, "3DSURROUND")) < 0)
    return group;

  if ((err = mixer_ext_create_control (dev, group, 201, softoss_ossverb,
				       MIXT_SLIDER,
				       "LEVEL", 255,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  if ((err = mixer_ext_create_control (dev, group, 200, softoss_ossverb,
				       MIXT_ONOFF,
				       "BYPASS", 2,
				       MIXF_READABLE | MIXF_WRITEABLE)) < 0)
    return err;

  return 0;
}

void
apply_effects (softoss_devc * devc, int *buf, int nsamples)
{
  static int aux_buf[MAX_ILOOP * 2], *bufin, *bufout, *tmp;
  int i;
  int channels = 2;		/* Always stereo at this moment */
  effects_info_t *efc;

  if ((efc = devc->effects_info) == NULL)
    return;

  bufin = buf;
  bufout = aux_buf;

  /*
   * Reverb
   */

  if (efc->ossverb_enabled)
    {
      ossverb_process (&efc->ossverb_struct, bufin, bufout, nsamples);
      tmp = bufin;
      bufin = bufout;
      bufout = tmp;		/* Swap buffers */
    }

  /*
   * OSSFID
   */

  if (efc->ossfid_enabled)
    {
      ossfid_process (&efc->ossfid_struct1, bufin, bufout, nsamples, 2);
      ossfid_process (&efc->ossfid_struct2, bufin + 1, bufout + 1, nsamples,
		      2);
      tmp = bufin;
      bufin = bufout;
      bufout = tmp;		/* Swap buffers */
    }

  /*
   * 3DSurround
   */

  if (efc->p3d_enabled)
    {
      process_3dsurround (&efc->p3d_struct, bufin, bufout, nsamples);
      tmp = bufin;
      bufin = bufout;
      bufout = tmp;		/* Swap buffers */
    }

  /* Finish operations */

  if (bufin != buf)
    {
      for (i = 0; i < nsamples * channels; i++)
	{
	  buf[i] = bufin[i];
	}
    }
}
#endif

static void
init_softoss (softoss_devc * devc)
{
  int i;
  extern int softoss_rate;

  MUTEX_INIT (devc->master_osdev, devc->mutex, MH_DRV + 4);
  devc->ram_size = 32 * 1024 * 1024;
  devc->ram_used = 0;
  devc->nrsamples = 0;
  devc->nr_loops = 0;
  devc->first_virtdev = 0;

  for (i = 0; i < MAX_PATCH; i++)
    {
      devc->programs[i] = NO_SAMPLE;
      devc->wave[i] = NULL;
    }

  devc->maxvoice = DEFAULT_VOICES;

  devc->softoss_opened = 0;
  devc->masterdev_opened = 0;
  devc->nr_opened_audio_engines = 0;
  devc->nr_avoices = 0;
  devc->channels = 2;
  devc->hw_channels = 2;
  devc->bits = 16;

  if (audio_engines[devc->masterdev]->vmix_flags & VMIX_MULTIFRAG)
    devc->max_playahead = 32;
  else
    devc->max_playahead = 4;

  devc->synth_volume_left = devc->synth_volume_right = 100;
  devc->pcm_volume_left = devc->pcm_volume_right = 100;

  if (softoss_rate < 8000)
    softoss_rate = 48000;
  devc->speed = softoss_rate;
#ifdef SOFTOSS_VOICES
  devc->default_max_voices = voice_limit = SOFTOSS_VOICES;
#else
  devc->default_max_voices = 32;
#endif

#if !defined(__hpux)
  softsyn_attach_audio ();
#endif

#ifdef USE_EQ
  devc->eq_bypass = 1;
  devc->eq_prescale = 255;
  for (i = 0; i < 4; i++)
    devc->eq_bands[i] = 128;
#endif

  /* Make a copy of original softsyn_operations to prevent crashes during/after detach */
  voice_alloc = &softsyn_operations.alloc;
#ifdef CONFIG_SEQUENCER
  softsynthp = softoss_hook;
#endif

}

static int
check_masterdev (softoss_devc * devc, int dev, int use_force)
{
  unsigned int mask;

  if (dev < 0 || dev >= num_audio_devfiles)
    return 0;

  dev = audio_devfiles[dev]->engine_num;	/* Convert to engine number */

  if (!(audio_engines[dev]->
	oformat_mask & (AFMT_S16_NE | AFMT_S16_OE | AFMT_S32_NE |
			AFMT_S32_OE | AFMT_S24_PACKED)))
    {
      DDB (cmn_err
	   (CE_NOTE,
	    "Audio engine %d doesn't support 16, 24 or 32 bit formats\n",
	    dev));
      return 0;
    }

  mask = ADEV_NOOUTPUT | ADEV_VIRTUAL | ADEV_DISABLE_VIRTUAL;
  if (!use_force)
    mask |= ADEV_NOVIRTUAL;

  if (audio_engines[dev]->flags & mask)
    {
      DDB (cmn_err
	   (CE_NOTE,
	    "Audio engine %d doesn't support 16, 24 or 32 bit formats\n",
	    dev));
      return 0;
    }

  devc->masterdev = devc->input_master = dev;

  DDB (cmn_err
       (CE_CONT, "Will use audio engine %d/%s as the master\n",
	devc->masterdev, audio_engines[devc->masterdev]->name));

#if 0
  /*
   * The input stuff doesn't work and will cause random crashes. For this
   * reason this code is disabled. Recording support is included in the
   * new vmix driver that has replaced softoss in most processor
   * architectures.
   */
  if (!(audio_engines[dev]->flags & ADEV_NOINPUT))
    {
      devc->duplex_mode = 1;
    }
#endif

  return 1;
}

static int
find_masterdev (softoss_devc * devc)
{
  extern int softoss_masterdev;
  int dev;

  if (num_audio_devfiles < 1)	/* No devices yet */
    return 0;

  devc->duplex_mode = 0;

  if (softoss_masterdev >= num_audio_devfiles)	/* The requested device is not there yet */
    return 0;

  if (softoss_masterdev >= 0 && softoss_masterdev < num_audio_devfiles)
    {
      softoss_masterdev = audio_devfiles[softoss_masterdev]->engine_num;

      if (check_masterdev (devc, softoss_masterdev, 1))
	return 1;

      cmn_err (CE_WARN,
	       "The selected master device (softoss_masterdev=%d) is not suitable.\n",
	       softoss_masterdev);
      return 0;
    }

  for (dev = 0; dev < num_audio_devfiles; dev++)
    {
      if (check_masterdev (devc, dev, 0))
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * The try_to_start routine checks if it can initialize the configured master device.
 * Otherwise it returns false which makes the attach routine to register a startup
 * callback. In this way softoss will hopefully get properly initialized after the
 * selected device shows up.
 */

static int
try_to_start (void *dc)
{
  softoss_devc *devc = (softoss_devc *) dc;

  if (already_attached)
    return 1;

  if (!find_masterdev (devc))
    {
      DDB (cmn_err (CE_NOTE,
		    "Cannot find suitable master device - delaying startup.\n"));
      return 0;
    }

  already_attached = 1;

  devc->master_osdev = audio_engines[devc->masterdev]->osdev;	/* Use the osdev structure of the master device */

  init_softoss (devc);
#ifdef USE_ALGORITHMS
  init_effects (devc);
#endif

  return 1;
}

int
softoss_attach (oss_device_t * osdev)
{

  if (softoss_disable)
    return 0;

  if (softoss_loaded)
    {
      return 0;
    }
  softoss_loaded = 1;

  devc->osdev = osdev;
  osdev->devc = devc;

  if (softoss_devices < 4)
    softoss_devices = 4;
  if (softoss_devices > MAX_AVOICE)
    softoss_devices = MAX_AVOICE;

  oss_register_device (osdev, AUDIO_DRIVER_NAME);

  if (!try_to_start (devc))
    {
      oss_audio_register_client (try_to_start, devc, devc->osdev);
    }

  return 1;
}

int
softoss_detach (oss_device_t * osdev)
{
  softoss_devc *devc = osdev->devc;

  if (!softoss_loaded || devc->osdev == NULL)
    return 1;

  if (oss_disable_device (devc->osdev) < 0)
    return 0;

#ifdef CONFIG_SEQUENCER
  softsynthp = NULL;
#endif
  reset_samples (devc);

  if (already_attached)
    MUTEX_CLEANUP (devc->mutex);
  oss_unregister_device (devc->osdev);

  return 1;
}
