/*
 * Purpose: init handler for Asus m9.
 */

#define COPYING Copyright (C) 2008 Paulo Matias <matias@dotbsd.org>. Licensed to 4Front Technologies.

/* Codec index is 0 */
/* Codec vendor 10ec:0260 */
/* HD codec revision 1.0 (4.0) (0x00100400) */
/* Subsystem ID 1025160d */
#include "oss_hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "hdaudio_dedicated.h"
#include "hdaudio_mixers.h"

int
hdaudio_asus_m9_mixer_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
	DDB(cmn_err(CE_CONT, "hdaudio_asus_m9_mixer_init got called.\n"));

	corb_write (mixer, cad, 0x1b, 0, SET_EAPD, 0);

	return hdaudio_generic_mixer_init(dev, mixer, cad, top_group);
}

