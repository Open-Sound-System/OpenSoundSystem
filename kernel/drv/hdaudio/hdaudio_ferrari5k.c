#define COPYING Copyright (C) 4Front Technologies 2007. All rights reserved.
/* Codec index is 1 */
/* Codec vendor 0804:74ac */
/* HD codec revision 1.0 (0.2) (0x00100002) */
/* Subsystem ID 10250000 */
/* Default amplifier caps: in=00000000, out=00000000 */
#include "hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "hdaudio_dedicated.h"

int
hdaudio_ferrari5k_mixer_init (int dev, hdaudio_mixer_t * mixer, int cad,
			      int top_group)
{
  int wid;
  int ctl = 0;
  codec_t *codec = mixer->codecs[cad];

  DDB (cmn_err (CE_CONT, "hdaudio_ferrari5k_mixer_init got called.\n"));

  /*
   * Selected volume controls that have been manually moved to the top group.
   */
  HDA_OUTAMP_F (0x0c, top_group, "front", 90, MIXF_PCMVOL);
  HDA_OUTAMP_F (0x0d, top_group, "rear", 90, MIXF_PCMVOL);
  HDA_OUTAMP_F (0x0e, top_group, "center/LFE", 90, MIXF_PCMVOL);
  HDA_OUTAMP_F (0x0f, top_group, "side", 90, MIXF_PCMVOL);
  HDA_OUTAMP_F (0x26, top_group, "pcm4", 90, MIXF_PCMVOL);

/*
 * Permanently unmute the input mute controls for the above controls.
 */
  HDA_SET_INMUTE (0x0c, 0, UNMUTE);	/* From widget 0x02 */
  HDA_SET_INMUTE (0x0c, 1, UNMUTE);	/* From widget 0x0b */
  HDA_SET_INMUTE (0x0d, 0, UNMUTE);	/* From widget 0x03 */
  HDA_SET_INMUTE (0x0d, 1, UNMUTE);	/* From widget 0x0b */
  HDA_SET_INMUTE (0x0e, 0, UNMUTE);	/* From widget 0x04 */
  HDA_SET_INMUTE (0x0e, 1, UNMUTE);	/* From widget 0x0b */
  HDA_SET_INMUTE (0x0f, 0, UNMUTE);	/* From widget 0x05 */
  HDA_SET_INMUTE (0x0f, 1, UNMUTE);	/* From widget 0x0b */
  HDA_SET_INMUTE (0x26, 0, UNMUTE);	/* From widget 0x25 */
  HDA_SET_INMUTE (0x26, 1, UNMUTE);	/* From widget 0x0b */

  /* Handle ADC widgets */
  {
    int n, group, rec_group;

    n = 0;

    HDA_GROUP (rec_group, top_group, "record");

    if (HDA_ADC_GROUP (0x08, group, rec_group, "rec1", n, "record", 4))	/* ADC widget 0x08 */
      {
	/* Src 0x23=mix */
	HDA_INAMP (0x08, 0, group, "reclev", 90);	/* From widget 0x23 */

	/* Widget 0x23 (mix) */
	/* Src 0x18=mic */
	/* Src 0x19=int-mic */
	/* Src 0x1a=linein */
	/* Src 0x1b=speaker */
	/* Src 0x1c=speaker */
	/* Src 0x1d=speaker */
	/* Src 0x14=headphone */
	/* Src 0x15=int-speaker */
	/* Src 0x16=speaker */
	/* Src 0x17=speaker */
	/* Src 0xb=input */
	if (HDA_INSRC_SELECT (0x23, group, ctl, "recsrc", 10))
	  HDA_CHOICES (ctl,
		       "mic int-mic linein speaker speaker speaker headphone-jack int-speaker speaker speaker input-mix");
      }

    if (HDA_ADC_GROUP (0x09, group, rec_group, "rec2", n, "record", 4))	/* ADC widget 0x09 */
      {
	/* Src 0x22=mix */
	HDA_INAMP (0x09, 0, group, "reclev", 90);	/* From widget 0x22 */

	/* Widget 0x22 (mix) */
	/* Src 0x18=mic */
	/* Src 0x19=int-mic */
	/* Src 0x1a=linein */
	/* Src 0x1b=speaker */
	/* Src 0x1c=speaker */
	/* Src 0x1d=speaker */
	/* Src 0x14=headphone */
	/* Src 0x15=int-speaker */
	/* Src 0x16=speaker */
	/* Src 0x17=speaker */
	/* Src 0xb=input */
	if (HDA_INSRC_SELECT (0x22, group, ctl, "recsrc", 10))
	  HDA_CHOICES (ctl,
		       "mic int-mic linein speaker speaker speaker headphone-jack int-speaker speaker speaker input-mix");
      }

#if 0
    if (HDA_ADC_GROUP (0x0a, group, rec_group, "spdif-in", n, "record", 4))	/* ADC widget 0x0a */
      {
	/* Src 0x1f=speaker */
      }
#endif
  }
  /* Handle misc widgets */
  {
    int n, group, misc_group, mute_group;
    int n2 = 0;

    n = 0;

    HDA_GROUP (group, top_group, "input-mix");

    //if (HDA_MISC_GROUP(0x0b, group, misc_group, "input-mix", n, "misc", 8))       /* Misc widget 0x0b */
    {
      /* Src 0x18=mic */
      /* Src 0x19=int-mic */
      /* Src 0x1a=linein */
      /* Src 0x1b=speaker */
      /* Src 0x1c=speaker */
      /* Src 0x1d=speaker */
      /* Src 0x14=headphone */
      /* Src 0x15=int-speaker */
      /* Src 0x16=speaker */
      /* Src 0x17=speaker */

      HDA_INAMP (0x0b, 0, group, "mic", 90);	/* From widget 0x18 */
      HDA_INAMP (0x0b, 1, group, "int-mic", 20);	/* From widget 0x19 */
      HDA_INAMP (0x0b, 2, group, "linein", 90);	/* From widget 0x1a */
      HDA_INAMP (0x0b, 3, group, "speaker", 90);	/* From widget 0x1b */
      HDA_INAMP (0x0b, 4, group, "speaker", 90);	/* From widget 0x1c */
      HDA_INAMP (0x0b, 5, group, "speaker", 90);	/* From widget 0x1d */
      HDA_INAMP (0x0b, 6, group, "headphone-jack", 90);	/* From widget 0x14 */
      HDA_INAMP (0x0b, 7, group, "int-speaker", 90);	/* From widget 0x15 */
      HDA_INAMP (0x0b, 8, group, "speaker", 90);	/* From widget 0x16 */
      HDA_INAMP (0x0b, 9, group, "speaker", 90);	/* From widget 0x17 */
    }

  }
  /* Handle PIN widgets */
  {
    int n, group, pin_group;

    n = 0;

    HDA_GROUP (pin_group, top_group, "jack");

    if (HDA_PIN_GROUP (0x15, group, pin_group, "int-speaker", n, "jack", 5))	/* Pin widget 0x15 */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_SELECT (0x15, "mode", ctl, group, 0))
	  {
	    HDA_CHOICES (ctl, "front rear center/LFE side pcm4");
	  }
	HDA_OUTMUTE (0x15, group, "inmute", UNMUTE);
	HDA_INAMP (0x15, 0, group, "out", 90);	/* From widget 0x0c */
      }

    if (HDA_PIN_GROUP (0x14, group, pin_group, "headphone-jack", n, "jack", 5))	/* Pin widget 0x14 */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_PINSELECT (0x14, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "front rear center/LFE side pcm4 input");
	HDA_OUTMUTE (0x14, group, "inmute", UNMUTE);
	HDA_INAMP (0x14, 0, group, "out", 90);
      }

#if 0
    if (HDA_PIN_GROUP (0x16, group, pin_group, "unused1", n, "jack", 5))	/* Pin widget 0x16 */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_PINSELECT (0x16, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "front rear center/LFE side pcm4 input");
	HDA_OUTMUTE (0x16, group, "mute", UNMUTE);
	{
	  int amp_group;

	  HDA_GROUP (amp_group, group, "vol");
	  HDA_INAMP (0x16, 0, amp_group, "front", 90);	/* From widget 0x0c */
	  HDA_INAMP (0x16, 1, amp_group, "rear", 90);	/* From widget 0x0d */
	  HDA_INAMP (0x16, 2, amp_group, "center/LFE", 90);	/* From widget 0x0e */
	  HDA_INAMP (0x16, 3, amp_group, "side", 90);	/* From widget 0x0f */
	  HDA_INAMP (0x16, 4, amp_group, "pcm4", 90);	/* From widget 0x26 */
	}
      }

    if (HDA_PIN_GROUP (0x17, group, pin_group, "unused2", n, "jack", 5))	/* Pin widget 0x17 */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_PINSELECT (0x17, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "front rear center/LFE side pcm4 input");
	HDA_OUTMUTE (0x17, group, "mute", UNMUTE);
	{
	  int amp_group;

	  HDA_GROUP (amp_group, group, "vol");
	  HDA_INAMP (0x17, 0, amp_group, "front", 90);	/* From widget 0x0c */
	  HDA_INAMP (0x17, 1, amp_group, "rear", 90);	/* From widget 0x0d */
	  HDA_INAMP (0x17, 2, amp_group, "center/LFE", 90);	/* From widget 0x0e */
	  HDA_INAMP (0x17, 3, amp_group, "side", 90);	/* From widget 0x0f */
	  HDA_INAMP (0x17, 4, amp_group, "pcm4", 90);	/* From widget 0x26 */
	}
      }
#endif

    if (HDA_PIN_GROUP (0x18, group, pin_group, "ext-mic", n, "jack", 5))	/* Pin widget 0x18 */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_PINSELECT (0x18, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "front rear center/LFE side pcm4 input");
	HDA_OUTMUTE (0x18, group, "inmute", UNMUTE);
	HDA_INAMP (0x18, 0, group, "out", 90);	/* From widget 0x0c */
      }

/*
 * Hide controls for PIN widget 0x19 which is the internal microphone.
 */
    HDA_SETSELECT (0x19, 5);	/* Force input mode */
    if (HDA_PIN_GROUP (0x19, group, pin_group, "int-mic", n, "jack", 5))	/* Pin widget 0x19 */
      {
	HDA_OUTMUTE (0x19, group, "inmute", UNMUTE);
      }

    if (HDA_PIN_GROUP (0x1a, group, pin_group, "line-out", n, "jack", 5))	/* Pin widget 0x1a */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_PINSELECT (0x1a, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "front rear center/LFE side pcm4 input");
	HDA_OUTMUTE (0x1a, group, "inmute", UNMUTE);
	HDA_INAMP (0x1a, 0, group, "out", 90);	/* From widget 0x0c */
      }

#if 0
    if (HDA_PIN_GROUP (0x1b, group, pin_group, "unused3", n, "jack", 4))	/* Pin widget 0x1b */
      {
	/* Src 0xc=front */
	/* Src 0xd=rear */
	/* Src 0xe=center/LFE */
	/* Src 0xf=side */
	/* Src 0x26=pcm4 */
	if (HDA_PINSELECT (0x1b, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "front rear center/LFE side pcm4 input");
	HDA_OUTMUTE (0x1b, group, "mute", UNMUTE);
	{
	  int amp_group;

	  HDA_GROUP (amp_group, group, "vol");
	  HDA_INAMP (0x1b, 0, amp_group, "front", 90);	/* From widget 0x0c */
	  HDA_INAMP (0x1b, 1, amp_group, "rear", 90);	/* From widget 0x0d */
	  HDA_INAMP (0x1b, 2, amp_group, "center/LFE", 90);	/* From widget 0x0e */
	  HDA_INAMP (0x1b, 3, amp_group, "side", 90);	/* From widget 0x0f */
	  HDA_INAMP (0x1b, 4, amp_group, "pcm4", 90);	/* From widget 0x26 */
	}
      }

    if (HDA_PIN_GROUP (0x1c, group, pin_group, "unused4", n, "jack", 4))	/* Pin widget 0x1c */
      {
	if (HDA_PINSELECT (0x1c, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "input");
      }

    if (HDA_PIN_GROUP (0x1d, group, pin_group, "unused5", n, "jack", 4))	/* Pin widget 0x1d */
      {
	if (HDA_PINSELECT (0x1d, ctl, group, "mode", -1))
	  HDA_CHOICES (ctl, "input");
      }
#endif
  }
  return 0;
}
