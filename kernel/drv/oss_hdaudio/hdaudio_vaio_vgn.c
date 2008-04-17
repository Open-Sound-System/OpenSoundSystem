#define COPYING Copyright (C) 4Front Technologies 2007. All rights reserved.
/* Codec index is 0 */
/* Codec vendor 0804:73dc */
/* HD codec revision 1.0 (2.1) (0x00100201) */
/* Subsystem ID 104d2200 */
/* Default amplifier caps: in=80050f00, out=80027f7f */
#include "hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "hdaudio_dedicated.h"

int
hdaudio_vaio_vgn_mixer_init (int dev, hdaudio_mixer_t * mixer, int cad,
			     int top_group)
{
  int wid;
  int ctl = 0;
  codec_t *codec = mixer->codecs[cad];

  DDB (cmn_err (CE_CONT, "hdaudio_vaio_vgn_mixer_init got called.\n"));

  HDA_OUTAMP (0x05, top_group, "speaker", 90);
  HDA_OUTAMP (0x02, top_group, "headphone", 100);

  HDA_SETSELECT (0x0f, 0);	/* Int speaker mode */
  HDA_SETSELECT (0x14, 1);	/* Int mic mode */

  /* Handle PIN widgets */
  {
    int n, group, pin_group;

    n = 0;

    HDA_GROUP (pin_group, top_group, "jack");

    if (HDA_PIN_GROUP (0x0a, group, pin_group, "headphone", n, "jack", 4))	/* Pin widget 0x0a */
      {
	/* Src 0x2=pcm2 */
	if (HDA_PINSELECT (0x0a, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "pcm2-out input");
      }

    if (HDA_PIN_GROUP (0x0b, group, pin_group, "black", n, "jack", 4))	/* Pin widget 0x0b */
      {
	/* Src 0x4=pcm */
	if (HDA_PINSELECT (0x0b, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "pcm-out input");

	/* Widget 0x04 (pcm) */
	HDA_OUTAMP (0x04, group, "-", 90);
      }

    if (HDA_PIN_GROUP (0x0c, group, pin_group, "black", n, "jack", 4))	/* Pin widget 0x0c */
      {
	/* Src 0x3=pcm */
	if (HDA_PINSELECT (0x0c, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "pcm-out input");

	/* Widget 0x03 (pcm) */
	HDA_OUTAMP (0x03, group, "-", 90);
      }

    if (HDA_PIN_GROUP (0x0d, group, pin_group, "red", n, "jack", 4))	/* Pin widget 0x0d */
      {
	/* Src 0x2=pcm2 */
	if (HDA_PINSELECT (0x0d, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "pcm2-out input");
      }

    if (HDA_PIN_GROUP (0x0e, group, pin_group, "black", n, "jack", 4))	/* Pin widget 0x0e */
      {
	if (HDA_PINSELECT (0x0e, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "input");
      }
  }
  /* Handle ADC widgets */
  {
    int n, group, rec_group;

    n = 0;

    HDA_GROUP (rec_group, top_group, "record");

    if (HDA_ADC_GROUP (0x06, group, rec_group, "rec", n, "record", 4))	/* ADC widget 0x06 */
      {
	/* Src 0x7=rec */

	/* Widget 0x07 (rec) */
	/* Src 0xe=black */
	HDA_INAMP (0x07, 0, group, "black", 90);	/* From widget 0x0e */
      }

    if (HDA_ADC_GROUP (0x08, group, rec_group, "rec", n, "record", 4))	/* ADC widget 0x08 */
      {
	/* Src 0x9=rec */
      }

    if (HDA_ADC_GROUP (0x12, group, rec_group, "spdifin", n, "record", 4))	/* ADC widget 0x12 */
      {
	/* Src 0x13=speaker */
      }
  }
  /* Handle misc widgets */
  {
    int n, group, misc_group;

    n = 0;

    HDA_GROUP (misc_group, top_group, "misc");

    if (HDA_MISC_GROUP (0x09, group, misc_group, "rec", n, "misc", 8))	/* Misc widget 0x09 */
      {
	/* Src 0x15=rec */
	HDA_INAMP (0x09, 0, group, "rec", 90);	/* From widget 0x15 */

	/* Widget 0x15 (rec) */
	/* Src 0xa=black */
	/* Src 0xd=red */
	/* Src 0x14=int-mic */
	/* Src 0x2=pcm2 */
	if (HDA_SELECT (0x15, "src", ctl, group, -1))
	  {
	    HDA_CHOICES (ctl, "headphone mic int-mic pcm2");
	  }
	HDA_OUTAMP (0x15, group, "-", 90);
      }

#if 0
    if (HDA_MISC_GROUP (0x16, group, misc_group, "beep", n, "misc", 8))	/* Misc widget 0x16 */
      {
	HDA_OUTAMP (0x16, group, "-", 90);
      }
#endif
  }
  return 0;
}
