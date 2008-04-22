/*
 * Purpose: Definitions for the mixer of cs4231.
 */

#define COPYING2 Copyright (C) Hannu Savolainen and Dev Mazumdar 1997-2007. All rights reserved.

/*
 * The AD1848 codec has generic input lines called Line, Aux1 and Aux2.
 * Soundcard manufacturers have connected actual inputs (CD, synth, line,
 * etc) to these inputs in different order. Therefore it's difficult
 * to assign mixer channels to to these inputs correctly. The following
 * contains two alternative mappings. The first one is for GUS MAX and
 * the second is just a generic one (line1, line2 and line3).
 * (Actually this is not a mapping but rather some kind of interleaving
 * solution).
 */
#define MODE1_REC_DEVICES		(SOUND_MASK_LINE3 | SOUND_MASK_MIC | \
					 SOUND_MASK_LINE1|SOUND_MASK_IMIX)

#define MODE1_MIXER_DEVICES		(SOUND_MASK_LINE1 | SOUND_MASK_MIC | \
					 SOUND_MASK_LINE2 | \
					 SOUND_MASK_IGAIN | \
					 SOUND_MASK_PCM | SOUND_MASK_IMIX)

#define MODE2_MIXER_DEVICES		(SOUND_MASK_LINE1 | SOUND_MASK_LINE2 | \
					 SOUND_MASK_MIC | \
					 SOUND_MASK_LINE3 | SOUND_MASK_SPEAKER | \
					 SOUND_MASK_IGAIN | \
					 SOUND_MASK_PCM | SOUND_MASK_IMIX)

#define MODE3_MIXER_DEVICES		(MODE2_MIXER_DEVICES | SOUND_MASK_VOLUME)

#define CMI8330_MIXER_DEVICES		(SOUND_MASK_LINE | SOUND_MASK_CD | \
					 SOUND_MASK_MIC | \
					 SOUND_MASK_SYNTH | SOUND_MASK_SPEAKER | \
					 SOUND_MASK_LINE1 | SOUND_MASK_VOLUME | \
					 SOUND_MASK_PCM | SOUND_MASK_IMIX)
#define CMI8330_REC_DEVICES		(SOUND_MASK_MIC|SOUND_MASK_LINE|SOUND_MASK_CD)


/* OPTi 82C930 has no IMIX level control, but it can still be selected as an
 * input
 */
#define C930_MIXER_DEVICES	(SOUND_MASK_LINE1 | SOUND_MASK_LINE2 | \
				 SOUND_MASK_MIC | SOUND_MASK_VOLUME | \
				 SOUND_MASK_LINE3 | \
				 SOUND_MASK_IGAIN | SOUND_MASK_PCM)

struct mixer_def
{
  unsigned int regno;
  unsigned int polarity;	/* 0=normal, 1=reversed */
  unsigned int bitpos;
  unsigned int nbits;
  unsigned int mutepos;
};

typedef struct mixer_def mixer_ent;
typedef mixer_ent mixer_ents[2];

/*
 * Most of the mixer entries work in backwards. Setting the polarity field
 * makes them to work correctly.
 *
 * The channel numbering used by individual soundcards is not fixed. Some
 * cards have assigned different meanings for the AUX1, AUX2 and LINE inputs.
 * The current version doesn't try to compensate this.
 */

#define MIX_ENT(name, reg_l, pola_l, pos_l, len_l, reg_r, pola_r, pos_r, len_r, mute_bit)	\
	{{reg_l, pola_l, pos_l, len_l}, {reg_r, pola_r, pos_r, len_r, mute_bit}}

static mixer_ents cs4231_mix_devices[32] = {
  MIX_ENT (SOUND_MIXER_VOLUME, 27, 1, 0, 4, 29, 1, 0, 4, 7),
  MIX_ENT (SOUND_MIXER_BASS, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_TREBLE, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_SYNTH, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_PCM, 6, 1, 0, 6, 7, 1, 0, 6, 7),
  MIX_ENT (SOUND_MIXER_SPEAKER, 26, 1, 0, 4, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE, 18, 1, 0, 5, 19, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_MIC, 0, 0, 5, 1, 1, 0, 5, 1, 8),
  MIX_ENT (SOUND_MIXER_CD, 2, 1, 0, 5, 3, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_IMIX, 13, 1, 2, 6, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_ALTPCM, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_RECLEV, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_IGAIN, 0, 0, 0, 4, 1, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_OGAIN, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE1, 2, 1, 0, 5, 3, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_LINE2, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_LINE3, 18, 1, 0, 5, 19, 1, 0, 5, 7)
};

static mixer_ents iwave_mix_devices[32] = {
  MIX_ENT (SOUND_MIXER_VOLUME, 25, 1, 0, 5, 27, 1, 0, 5, 8),
  MIX_ENT (SOUND_MIXER_BASS, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_TREBLE, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_SYNTH, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_PCM, 6, 1, 0, 6, 7, 1, 0, 6, 7),
  MIX_ENT (SOUND_MIXER_SPEAKER, 26, 1, 0, 4, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE, 18, 1, 0, 5, 19, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_MIC, 0, 0, 5, 1, 1, 0, 5, 1, 8),
  MIX_ENT (SOUND_MIXER_CD, 2, 1, 0, 5, 3, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_IMIX, 16, 1, 0, 5, 17, 1, 0, 5, 8),
  MIX_ENT (SOUND_MIXER_ALTPCM, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_RECLEV, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_IGAIN, 0, 0, 0, 4, 1, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_OGAIN, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE1, 2, 1, 0, 5, 3, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_LINE2, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_LINE3, 18, 1, 0, 5, 19, 1, 0, 5, 7)
};

/* OPTi 82C930 has somewhat different port addresses.
 * Note: VOLUME == SPEAKER, SYNTH == LINE2, LINE == LINE3, CD == LINE1
 * VOLUME, SYNTH, LINE, CD are not enabled above.
 * MIC is level of mic monitoring direct to output. Same for CD, LINE, etc.
 */
static mixer_ents c930_mix_devices[32] = {
  MIX_ENT (SOUND_MIXER_VOLUME, 22, 1, 0, 5, 23, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_BASS, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_TREBLE, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_SYNTH, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_PCM, 6, 1, 0, 6, 7, 1, 0, 6, 7),
  MIX_ENT (SOUND_MIXER_SPEAKER, 22, 1, 0, 5, 23, 1, 0, 5, 8),
  MIX_ENT (SOUND_MIXER_LINE, 18, 1, 1, 4, 19, 1, 1, 4, 7),
  MIX_ENT (SOUND_MIXER_MIC, 20, 1, 0, 4, 21, 1, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_CD, 2, 1, 1, 4, 3, 1, 1, 4, 7),
  MIX_ENT (SOUND_MIXER_IMIX, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_ALTPCM, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_RECLEV, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_IGAIN, 0, 0, 0, 4, 1, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_OGAIN, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE1, 2, 1, 1, 4, 3, 1, 1, 4, 7),
  MIX_ENT (SOUND_MIXER_LINE2, 4, 1, 1, 4, 5, 1, 1, 4, 7),
  MIX_ENT (SOUND_MIXER_LINE3, 18, 1, 1, 4, 19, 1, 1, 4, 7)
};

static mixer_ents cmi8330_mix_devices[32] = {
  MIX_ENT (SOUND_MIXER_VOLUME, 0x13, 0, 4, 4, 0x13, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_BASS, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_TREBLE, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_SYNTH, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_PCM, 6, 1, 0, 6, 7, 1, 0, 6, 7),
  MIX_ENT (SOUND_MIXER_SPEAKER, 0x26, 0, 3, 2, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE, 0x14, 0, 4, 4, 0x14, 0, 0, 4, 8),
#if 1
  MIX_ENT (SOUND_MIXER_MIC, 0, 0, 5, 1, 1, 0, 5, 1, 8),
#else
  MIX_ENT (SOUND_MIXER_MIC, 0x12, 0, 0, 3, 1, 0, 5, 1, 8),
#endif
  MIX_ENT (SOUND_MIXER_CD, 0x15, 0, 4, 4, 0x15, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_IMIX, 13, 1, 2, 6, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_ALTPCM, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_RECLEV, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_IGAIN, 0, 0, 0, 4, 1, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_OGAIN, 0, 0, 0, 0, 0, 0, 0, 0, 8),
  MIX_ENT (SOUND_MIXER_LINE1, 0x16, 0, 4, 4, 0x14, 0, 0, 4, 8),
  MIX_ENT (SOUND_MIXER_LINE2, 4, 1, 0, 5, 5, 1, 0, 5, 7),
  MIX_ENT (SOUND_MIXER_LINE3, 18, 1, 0, 5, 19, 1, 0, 5, 7)
};

static int default_mixer_levels[32] = {
  0x3232,			/* Master Volume */
  0x3232,			/* Bass */
  0x3232,			/* Treble */
  0x4b4b,			/* FM */
  0x3232,			/* PCM */
  0x1515,			/* PC Speaker */
  0x2020,			/* Ext Line */
  0x2020,			/* Mic */
  0x4b4b,			/* CD */
  0x0000,			/* Recording monitor */
  0x4b4b,			/* Second PCM */
  0x4b4b,			/* Recording level */
  0x0000,			/* Input gain */
  0x4b4b,			/* Output gain */
  0x2020,			/* Line1 */
  0x2020,			/* Line2 */
  0x1515			/* Line3 (usually line in) */
};

#define LEFT_CHN	0
#define RIGHT_CHN	1

/*
 * Channel enable bits for ioctl(SOUND_MIXER_PRIVATE1)
 */

#ifndef AUDIO_SPEAKER
#define	AUDIO_SPEAKER		0x01	/* Enable mono output */
#define	AUDIO_HEADPHONE		0x02	/* Sparc only */
#define	AUDIO_LINE_OUT		0x04	/* Sparc only */
#endif
