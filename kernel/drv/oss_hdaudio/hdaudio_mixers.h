/*
 * Purpose: Declarations of some functions and structures for HD audio mixers
 */
#define COPYING100 Copyright (C) Hannu Savolainen and Dev Mazumdar 1996-2005. All rights reserved.
/*
 * Prototype definitions for dedicated HDAudio codec/mixer drivers.
 */

extern int hdaudio_generic_mixer_init (int dev, hdaudio_mixer_t * mixer,
				       int cad, int group);
