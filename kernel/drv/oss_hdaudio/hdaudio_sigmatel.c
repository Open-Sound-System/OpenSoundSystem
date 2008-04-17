/*
 * Purpose: Mixer/control panel drivers for selected HDAudio codecs by Sigmatel
 *
 * This driver is under construction and it doesn't do anything (yet).
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2006. All rights reserved.

#include "hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"

typedef struct
{
  unsigned int vendorid;
  char *name;
  unsigned int flags;
} stac_codec_def_t;

static const stac_codec_def_t codec_defs[] = {
  /* Sigmatel HDA codecs */
  {0x83847690, "STAC9200"},
  {0x83847882, "STAC9220 A1"},
  {0x83847680, "STAC9221 A1"},
  {0x83847880, "STAC9220 A2"},
  {0x83847681, "STAC9220D/9223D A2"},
  {0x83847682, "STAC9221 A2"},
  {0x83847683, "STAC9221D A2"},
  {0x83847620, "STAC9274"},
  {0x83847621, "STAC9274D"},
  {0x83847622, "STAC9273X"},
  {0x83847623, "STAC9273D"},
  {0x83847624, "STAC9272X"},
  {0x83847625, "STAC9272D"},
  {0x83847626, "STAC9271X"},
  {0x83847627, "STAC9271D"},
  {0x83847628, "STAC9274X5NH"},
  {0x83847629, "STAC9274D5NH"},
  {0, NULL}
};

/*ARGSUSED*/
int
hdaudio_stac922x_mixer_init (int dev, hdaudio_mixer_t * mixer, int cad,
			     int group)
{
  unsigned int vendorid, b;
  int wid, i;
  codec_t *codec = mixer->codecs[cad];
  const stac_codec_def_t *cdef;

  cmn_err (CE_CONT, "hdaudio_stac922x_mixer_init(dev=%d, cad=%d)\n", dev,
	   cad);

  if (corb_read (mixer, cad, 0, 0, GET_PARAMETER, HDA_VENDOR, &vendorid, &b))
    {
      cmn_err (CE_CONT, "Vendor ID = 0x%08x\n", vendorid);
    }
  else
    cmn_err (CE_WARN, "Cannot get codec ID\n");

  for (wid = 0; wid < codec->nwidgets; wid++)
    cmn_err (CE_CONT, "Widget %d = '%s'\n", wid, codec->widgets[wid].name);

  cdef = NULL;
  for (i = 0; codec_defs[i].vendorid != 0; i++)
    if (codec_defs[i].vendorid == vendorid)
      {
	cmn_err (CE_CONT, "Codec type = %s\n", codec_defs[i].name);
	cdef = &codec_defs[i];
	break;
      }

  if (cdef == NULL)
    {
      cmn_err (CE_WARN, "Unknown sigmatel codec ID 0x%08x\n", vendorid);
    }

  return -EAGAIN;
}
