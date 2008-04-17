/*
 * Purpose: Hook to enable internal speaker on Acer TravelMate 4060.
 */

#define COPYING Copyright (C) 2008 Paulo Matias <matias@dotbsd.org>. Licensed to 4Front Technologies.

/* Codec index is 0 */
/* Codec vendor 10ec:0260 */
/* HD codec revision 1.0 (4.0) (0x00100400) */
/* Subsystem ID 1025160d */
#include "hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "hdaudio_dedicated.h"
#include "hdaudio_mixers.h"

int
hdaudio_travelmate4060_mixer_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
	DDB(cmn_err(CE_CONT, "hdaudio_travelmate4060_mixer_init got called.\n"));

	/* Acer TravelMate 4060 and similar Aspire series, with ALC260 codec, need
	 * that we init GPIO to get internal speaker and headphone jack working. */
	corb_write(mixer, cad, 0x01, 0, SET_GPIO_ENABLE, 1);
	corb_write(mixer, cad, 0x01, 0, SET_GPIO_DIR, 1);
	corb_write(mixer, cad, 0x01, 0, SET_GPIO_DATA, 1);
  
	return hdaudio_generic_mixer_init(dev, mixer, cad, top_group);
}
