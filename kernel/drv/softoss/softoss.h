/*
 * Purpose: Definitions for the ols SoftOSS based virtual mixer.
 */
#define COPYING2 Copyright (C) Hannu Savolainen and Dev Mazumdar 1997. All rights reserved.

#define SYNTH_DRIVER_NAME "OSS Virtual Synth v2.5"
#define AUDIO_DRIVER_NAME "OSS Virtual Mixer v3.0"

#ifdef __hpux
#  define OSS_VOLATILE
#else
#  define OSS_VOLATILE volatile
#endif

#if !defined(_AIX) && !defined(__VXWORKS__) && !defined(__OPENOSS__)
#undef  USE_DCKILL
#define USE_ALGORITHMS
#endif

#if !defined(_AIX) && !defined(__VXWORKS__)
#define USE_EQ
#endif

#ifdef USE_DCKILL
#define DCPOINTS_FRAC (13)
#define DCPOINTS      (1<<DCPOINTS_FRAC)
#define DCPOINTS_MASK (DCPOINTS - 1)
typedef int DCfeedbackBuf_t[DCPOINTS][2];
typedef int DCfeedbackVal_t[2];
#endif

#if defined(linux) && defined(i386)
/* #define USE_FLOAT */
#endif

#define MAX_SOFTLOOP_DEV	4

extern int softoss_loopdevs;

/*
 * Sequencer mode1 timer calls made by sequencer.c
 */
extern int (*softsynthp) (int cmd, int parm1, int parm2,
			  oss_native_word parm3);

#define SSYN_START	1
#define SSYN_REQUEST	2	/* parm1 = time */
#define SSYN_STOP	3
#define SSYN_GETTIME	4	/* Returns number of ticks since reset */

#define MAX_PATCH 256
#define MAX_SAMPLE 512
#define MAX_VOICE 32
#define MAX_AVOICE 32
#define DEFAULT_VOICES 16

#define MAX_ILOOP 256

typedef struct voice_info
{
/*
 * Don't change anything in the beginning of this struct. These fields are used
 * by the resampling loop which may have been written in assembly for some
 * architectures. Any change may make the resampling code incompatible
 */
  int instr;
  short *wave;
  struct patch_info *sample;

  unsigned int ptr;
  int step;			/* Pointer to the wave data and pointer increment */

  int mode;
  int startloop, startbackloop, endloop, looplen;
  int releasing;

  int leftvol, rightvol;
  int rearleftvol, rearrightvol;
/***** Don't change anything above this */

  OSS_VOLATILE oss_native_word orig_freq, current_freq;
  OSS_VOLATILE int bender, bender_range, panning, frontrear;
  OSS_VOLATILE int main_vol, expression_vol, patch_vol, velocity;

/* Envelope parameters */

  int envelope_phase;
  OSS_VOLATILE int envelope_vol;
  OSS_VOLATILE int envelope_volstep;
  int envelope_time;		/* Number of remaining envelope steps */
  unsigned int envelope_target;
  int percussive_voice;
  int sustain_mode;		/* 0=off, 1=sustain on, 2=sustain on+key released */
  int note;

/*	Vibrato	*/
  int vibrato_rate;
  int vibrato_depth;
  int vibrato_phase;
  int vibrato_step;
  int vibrato_level;

/*	Tremolo	*/
  int tremolo_rate;
  int tremolo_depth;
  int tremolo_phase;
  int tremolo_step;
  int tremolo_level;

}
voice_info;

typedef struct avoice_info
{
  void (*mixer) (int, int *, int);
  short *wave;
  unsigned int ptr;
  int step;			/* Pointer to the wave data and pointer increment */
  int samplesize;

  unsigned int endloop;
  unsigned int fragsize;
  unsigned int fragmodulo;
  int audiodev;

  int left_vol;
  int right_vol;
  int active_left_vol;
  int active_right_vol;
  unsigned char left_vu, right_vu;

#ifdef USE_DCKILL
  /* DC killer tables */
  DCfeedbackBuf_t DCfeedbackBuf;
  DCfeedbackVal_t DCfeedbackVal;
  int DCptr;
#endif
}
avoice_info;

struct softoss_devc;

typedef struct recvoice_info
{
  int audiodev;
  int channels;

  void (*mixer) (struct softoss_devc * devc, struct recvoice_info * v,
		 int *devbuf, int devsize, dmap_t * dmap);

}
recvoice_info;

extern char voice_active[MAX_VOICE];
extern voice_info softoss_voices[MAX_VOICE];	/* Voice spesific info */
extern char avoice_active[MAX_AVOICE];
extern avoice_info softoss_avoices[MAX_AVOICE];	/* Voice spesific info */

extern voice_info softoss_voices[MAX_VOICE];	/* Voice spesific info */
extern recvoice_info softoss_recvoices[MAX_AVOICE];	/* Audio voice spesific info */

typedef struct
{
  int audio_dev;
  int is_opened, is_prepared, is_triggered;
  int speed, channels, fmt;
}
softloop_portc;

typedef struct softoss_devc
{
  oss_device_t *osdev;		/* The local osdev structure */
  oss_device_t *master_osdev;	/* The master device */
  oss_mutex_t mutex;
  int maxvoice;			/* # of voices to be processed */
  int afterscale;
  int delay_size;
  int control_rate, control_counter;
  int softoss_opened;
  int masterdev, input_master;
  int duplex_mode;
  int opened_inputs;
  int input_ready;
  int masterdev_opened;
/***** Don't change anything above this */

  int ram_size;
  int ram_used;

  int synthdev;
  int sequencer_mode;
  int subtype;			/* hw_config->subtype, 0=SoftOSS+MIX, 1=MIX only */
/*
 *	Audio parameters
 */

  int first_virtdev;
  int speed;
  int channels, hw_channels, multich, fivechan;
  int rec_channels;
  int bits;
  int default_max_voices;
  int max_playahead;
  struct fileinfo finfo, rec_finfo;
  int fragsize;
  int samples_per_fragment;
  int chendian;			/* Swap endianess before writing to the device */
  int autoreset;
  unsigned char left_vu, right_vu;

/*
 * 	Sample storage
 */
  int nrsamples;
  struct patch_info *samples[MAX_SAMPLE];
  short *wave[MAX_SAMPLE];

/*
 * 	Programs
 */
  int programs[MAX_PATCH];

/*
 *	Timer parameters
 */
  OSS_VOLATILE oss_native_word usecs;
  OSS_VOLATILE oss_native_word usecs_per_frag;
  OSS_VOLATILE oss_native_word next_event_usecs;

/*
 * 	Engine state
 */

  OSS_VOLATILE int engine_state;
#define ES_STOPPED			0
#define ES_STARTED			1

  /* Voice spesific bitmaps */
  OSS_VOLATILE int tremolomap;
  OSS_VOLATILE int vibratomap;

/*     Software mixing audio parameters */
  int nr_opened_audio_engines;
  int nr_avoices;

/* Mixer parameters */
  int synth_volume_right, synth_volume_left;
  int pcm_volume_right, pcm_volume_left;
  int *levels;
  int mixer_dev;

  oss_native_word saved_flags;
  int saved_srate;

/* Loop devices */
  softloop_portc loop_portc[MAX_SOFTLOOP_DEV];
  int nr_loops;

#ifdef USE_EQ
  int eq_bands[4];
  int eq_bypass;
  int eq_prescale;
#endif

#ifdef USE_ALGORITHMS
  int effects_initialized;
  void *effects_info;
#endif

  int dev_ptr;
  dmap_p dmap;
  int hw_format;

  int *tmp_recbuf;
  int rec_nsamples;
}
softoss_devc;

void softoss_resample_loop (softoss_devc * devc, int loops);
void mix_avoice_mono8 (int voice, int *buf, int loops);
void mix_avoice_stereo8 (int voice, int *buf, int loops);
void mix_avoice_mono16 (int voice, int *buf, int loops);
void mix_avoice_stereo16 (int voice, int *buf, int loops);
extern void softoss_start_engine (softoss_devc * devc);
extern int softoss_open_audiodev (void);
extern void softoss_close_audiodev (void);
extern void softsyn_control_loop (softoss_devc * devc);
extern void start_resampling (softoss_devc * devc);
extern void softoss_install_loop (softoss_devc * devc);
extern void softloop_callback (softoss_devc * devc);
extern void init_effects (softoss_devc * devc);
extern void apply_effects (softoss_devc * devc, int *buf, int nsamples);
extern void start_record_engine (softoss_devc * devc);
extern void stop_record_engine (softoss_devc * devc);

#define DELAY_SIZE	4096

extern char recvoice_active[MAX_AVOICE];

extern void recmix_8_mono (softoss_devc * devc, recvoice_info * v,
			   int *devbuf, int devsize, dmap_t * dmap);
extern void recmix_8_stereo (softoss_devc * devc, recvoice_info * v,
			     int *devbuf, int devsize, dmap_t * dmap);
extern void recmix_16_mono_ne (softoss_devc * devc, recvoice_info * v,
			       int *devbuf, int devsize, dmap_t * dmap);
extern void recmix_16_stereo_ne (softoss_devc * devc, recvoice_info * v,
				 int *devbuf, int devsize, dmap_t * dmap);
extern void recmix_16_mono_oe (softoss_devc * devc, recvoice_info * v,
			       int *devbuf, int devsize, dmap_t * dmap);
extern void recmix_16_stereo_oe (softoss_devc * devc, recvoice_info * v,
				 int *devbuf, int devsize, dmap_t * dmap);
