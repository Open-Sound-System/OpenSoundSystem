/*
 * Purpose: GPIO initialization handlers for some High Definition Audio systems
 *
 * This file contains codec initialization functions for some HDaudio
 * systems that require initialization of GPIO bits. All functions should
 * return OSS_EAGAIN so that hdaudio_codec.c knows to call the generic
 * codec/mixer initialization routine for the codec. Alternatively the
 * GPIO init function may call the codec/mixer init function for the
 * given system directly (return my_mixer_init_func()).
 *
 * Note that if the system has a dedicated mixer initialization function
 * then also GPIO initialization needs to be performed in the mixer init
 * function (since the same mixer_init function pointers in hdaudio_codecds.h
 * are shared for both purposes).
 *
 * For example:
 *
 * int
 * hdaudio_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
 * {
 *	codec_t *codec = mixer->codecs[cad];
 *	int afg = codec->afg;	// Audio function group root widget
 *
 *	// Now use the corb_read() and corb_write() functions to set the
 *	// GPIO related verbs (registers) to the required values.
 *
 * 	return OSS_EAGAIN;	// Fallback
 * }
 *
 * To write the GPIO registers you can use:
 *
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_DIR, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_ENABLE, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_DATA, 0xNNNNNNNN);
 *
 * Also (if necessary) you can use the following calls. However they will probably
 * need changes to hdaudio_codec.c so that the unsolicited responses are handled peoperly:
 *
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_WKEN, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_UNSOL, 0xNNNNNNNN);
 *	  corb_write (mixer, cad, afg, 0, SET_GPIO_STICKY, 0xNNNNNNNN);
 *
 * Next the function prototype should be added to hdaudio_codecids.h. Finally
 * edit the subdevices[] array in hdaudio_codecids.h so that the function
 * gets called when given codec (subsystem vendor+device) is detected in the 
 * system. It is not recommended to use the codecs[] table to
 * detect systems that need GPIO handling. The same codec may be used
 * in many different systems and most of them don't require GPIO init.
 * However this is possible if the handler uses the subvendor+subdevice ID to detect the system.
 */
/*
 *
 * This file is part of Open Sound System.
 *
 * Copyright (C) 4Front Technologies 1996-2008.
 *
 * This this source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 */

#include "oss_hdaudio_cfg.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "hdaudio_codecids.h"

int
hdaudio_mac_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
 	codec_t *codec = mixer->codecs[cad];
 	int afg = codec->afg;	// Audio function group root widget
	unsigned int subdevice = codec->subvendor_id;
	unsigned int codec_id = codec->vendor_id;

	// TODO: Populate this function with the real stuff
	
cmn_err(CE_CONT, "hdaudio_mac_GPIO_init() entered, afg=%d, subdevice=0x%08x, codec=0x%08x\n", afg, subdevice, codec_id);

	return OSS_EAGAIN; /* Continue with the default mixer init */
}

int
hdaudio_mac_sigmatel_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
cmn_err(CE_CONT, "iMac Sigmatel hdaudio initialization\n");
	return hdaudio_mac_GPIO_init(dev, mixer, cad, top_group);
}

/*
 * ALC298 on Lenovo boards matching subsystem 17aa:222d (Thinkpad T470-family
 * audio, per ALSA's ALC298_FIXUP_TPT470_DOCK quirk chain -- verified against
 * this exact codec's own GET_SUBSYSTEM_ID readback, "Codec 0: ALC298
 * (0x10ec0298/0x17aa222d)" in ossinfo -d output, not just the controller's
 * PCI subsystem ID).
 *
 *  - alc_fixup_tpt470_dacs(): pin 0x14 (front) should select DAC 0x03 and
 *    pins 0x17/0x21 should select DAC 0x02 ("otherwise the speaker output
 *    becomes too low by some reason on Thinkpads with ALC298 codec", per
 *    ALSA's comment). On this exact hardware none of the three pins
 *    actually have DAC 0x02/0x03 in their connection list -- ALSA's
 *    preferred_dacs mechanism operates on its own path-building graph
 *    search and reaches those DACs through an intermediate node a direct
 *    single-hop connection search doesn't see. Confirmed dead end; #if 0'd
 *    below rather than deleted, in case the multi-hop search is ever worth
 *    doing.
 *
 *  - AA-loopback (NID 0x0b, "mix" in alc298remap[]) is the mic's own signal
 *    path, not just an unwanted extra: selector 0x22 (feeds the ADC behind
 *    the working "record.rec2-sel" mic control) defaults to 0x0b, and 0x0b
 *    itself draws from 0x18 (pin_type PIN_MIC, VREF80 bias enabled) among
 *    its 4 inputs. Two attempts to route 0x22 directly at a mic pin (0x12,
 *    then 0x18) both produced measured silence instead of the working
 *    mic-through-loopback signal, for reasons not understood -- reverted.
 *
 *    The actual fix instead targets where the leak reaches the speaker,
 *    found via 0x0b's own reference list (every widget tracks who lists it
 *    as a connection): 0x0c ("front", a real NT_MIXER) sums DAC 0x02 with
 *    0x0b before the signal reaches the speaker pin. Muting just 0x0c's
 *    input from 0x0b (loopback-mute/-vol, below, defaulted to muted at
 *    attach) cuts the leak with zero effect on the record path, since it's
 *    downstream of the fork -- 0x0b's own amp (mic-src-mute/-vol) and
 *    everything on the record side are untouched.
 */

/*
 * Gain for widget 0x0b's input amp on the mic connection (ix 0, "0x18"),
 * captured fresh at the moment of each mute so the toggle below can
 * restore it exactly on unmute instead of leaving the amp at gain=0.
 * hdaudio_set_control()'s generic CT_INMUTE path (reused for the mute
 * control's first cut) writes gain-and-mute as a single byte and always
 * zeroes the gain bits on both mute and unmute -- fine for mute-only amps,
 * but this one has real adjustable gain (now also exposed as its own
 * slider, see hdaudio_lenovo_alc298_init()), so that silently lowered
 * capture volume every time it got toggled. Capturing on every mute
 * (rather than once at attach) also means an adjustment made via the
 * slider while unmuted survives a subsequent mute/unmute cycle instead of
 * being silently discarded in favor of a stale attach-time value.
 *
 * Named mic-src, not mic-loopback: this is 0x0b's own input gain, upstream
 * of where capture and the speaker leak fork apart -- it's the shared mic
 * source for both, not the loopback itself. That's loopback-mute/-vol,
 * further down, which targets the actual leak-only path at 0x0c.
 */
static unsigned int hda_lenovo_mic_src_gain = 0;

static int
hda_lenovo_mic_src_mute (int dev, int ctrl, unsigned int cmd, int value)
{
  hdaudio_mixer_t *mixer = mixer_devs[dev]->devc;
  unsigned int cad, wid, ix, a, b;

  ix = ctrl & 0xff;
  wid = (ctrl >> 16) & 0xff;
  cad = (ctrl >> 24) & 0xff;

  if (cmd == SNDCTL_MIX_READ)
    {
      if (!corb_read (mixer, cad, wid, 0, GET_GAIN (0, 0), ix, &a, &b))
	return OSS_EIO;
      return (a >> 7) & 0x01;
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
      unsigned int v;

      if (value)
	{
	  if (corb_read (mixer, cad, wid, 0, GET_GAIN (0, 0), ix, &a, &b))
	    hda_lenovo_mic_src_gain = a & 0x7f;
	  v = 0x80 | hda_lenovo_mic_src_gain;
	}
      else
	v = hda_lenovo_mic_src_gain;

      corb_write (mixer, cad, wid, 0, SET_GAIN (0, 1, 1, 1, ix), v);
      return value;
    }

  return OSS_EINVAL;
}

/*
 * The actual, surgical fix for the speaker leak: widget 0x0c ("front",
 * NT_MIXER) sums DAC 0x02 with the AA-loopback bus 0x0b before the signal
 * ever reaches the speaker pin. Unlike 0x0b/0x22 above, 0x0c is a real
 * 2-input mixer with independent per-connection amps, so muting its input
 * from 0x0b (ix 1) cuts only the leak -- 0x0b's own amp, and everything
 * downstream of it on the record side (0x22/0x23 -> the ADCs), is
 * untouched. Same gain-preserving read/write shape as
 * hda_lenovo_mic_src_mute() above, just against a different widget/index --
 * defaulted to muted at attach (see hdaudio_lenovo_alc298_init()) since
 * there's no real downside to leaving it off by default, unlike the mic
 * mute which trades away capture.
 */
static unsigned int hda_lenovo_loopback_gain = 0;

static int
hda_lenovo_loopback_mute (int dev, int ctrl, unsigned int cmd, int value)
{
  hdaudio_mixer_t *mixer = mixer_devs[dev]->devc;
  unsigned int cad, wid, ix, a, b;

  ix = ctrl & 0xff;
  wid = (ctrl >> 16) & 0xff;
  cad = (ctrl >> 24) & 0xff;

  if (cmd == SNDCTL_MIX_READ)
    {
      if (!corb_read (mixer, cad, wid, 0, GET_GAIN (0, 0), ix, &a, &b))
	return OSS_EIO;
      return (a >> 7) & 0x01;
    }

  if (cmd == SNDCTL_MIX_WRITE)
    {
      unsigned int v;

      if (value)
	{
	  if (corb_read (mixer, cad, wid, 0, GET_GAIN (0, 0), ix, &a, &b))
	    hda_lenovo_loopback_gain = a & 0x7f;
	  v = 0x80 | hda_lenovo_loopback_gain;
	}
      else
	v = hda_lenovo_loopback_gain;

      corb_write (mixer, cad, wid, 0, SET_GAIN (0, 1, 1, 1, ix), v);
      return value;
    }

  return OSS_EINVAL;
}

int
hdaudio_lenovo_alc298_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
  codec_t *codec = mixer->codecs[cad];
#if 0
  /*
   * Confirmed dead end on this hardware (see the header comment above):
   * none of these pins have the target DAC in their single-hop connection
   * list. Left here, disabled, in case the multi-hop search ALSA's
   * preferred_dacs effectively does is ever worth implementing.
   */
  {
    static const struct
    {
      unsigned char widget, target;
    } selector_fixups[] = {
      { 0x14, 0x03 },	/* speaker pin -> DAC */
      { 0x17, 0x02 },
      { 0x21, 0x02 },
    };
    int i, j;

    for (i = 0; i < sizeof (selector_fixups) / sizeof (selector_fixups[0]); i++)
      {
	widget_t *w = &codec->widgets[selector_fixups[i].widget];
	int idx = -1;

	for (j = 0; j < w->nconn; j++)
	  if (w->connections[j] == selector_fixups[i].target)
	    {
	      idx = j;
	      break;
	    }

	if (idx >= 0)
	  corb_write (mixer, cad, selector_fixups[i].widget, 0, SET_SELECTOR,
		      idx);
	else
	  cmn_err (CE_WARN,
		   "hdaudio_lenovo_alc298_init: widget 0x%02x has no connection to 0x%02x, leaving default routing\n",
		   selector_fixups[i].widget, selector_fixups[i].target);
      }
  }
#endif

  /*
   * Manual mute toggle for the mic's contribution to the AA-loopback bus:
   * widget 0x0b's own input amp, at the connection index for 0x18 ("mic"),
   * the same one feeding both the speaker bleed and the working capture
   * path. The two routing fixes above that tried to separate them (see
   * revert notes) both broke capture instead of just the leak. This
   * doesn't separate them either -- muting it kills capture too -- but it
   * turns an always-on leak into something you can switch off between
   * uses.
   */
  if (codec->widgets[0x0b].widget_caps & WCAP_INPUT_AMP_PRESENT)
    {
      widget_t *mix_widget = &codec->widgets[0x0b];
      unsigned int a, b;
      int ctl;

      hda_lenovo_mic_src_gain = 0;
      if (corb_read (mixer, cad, 0x0b, 0, GET_GAIN (0, 0), 0, &a, &b))
	hda_lenovo_mic_src_gain = a & 0x7f;

      if ((ctl = mixer_ext_create_control (mixer->mixer_dev, top_group,
					   MIXNUM (mix_widget, CT_INMUTE, 0),
					   hda_lenovo_mic_src_mute,
					   MIXT_MUTE, "mic-src-mute", 2,
					   MIXF_READABLE |
					   MIXF_WRITEABLE)) < 0)
	cmn_err (CE_WARN,
		 "hdaudio_lenovo_alc298_init: failed to create mic-src-mute control (%d)\n",
		 ctl);

      /*
       * Gain slider for the same connection, same widget the auto-walker
       * would have created had it ever reached 0x0b -- identical
       * type/handler/flag choices to attach_amplifiers()'s own version of
       * this in hdaudio_generic.c. Deliberately does NOT force an initial
       * value (that code's own 80%-of-max default) so it doesn't disturb
       * whatever gain is already live, muted or not.
       */
      if (mix_widget->inamp_caps & ~AMPCAP_MUTE)
	{
	  int maxval = hdaudio_amp_maxval (mix_widget->inamp_caps);
	  int typ = (mix_widget->widget_caps & WCAP_STEREO) ?
	    MIXT_STEREOSLIDER16 : MIXT_MONOSLIDER16;
	  int num = (mix_widget->widget_caps & WCAP_STEREO) ?
	    MIXNUM (mix_widget, CT_INSTEREO, 0) :
	    MIXNUM (mix_widget, CT_INMONO, 0);

	  if ((ctl = mixer_ext_create_control (mixer->mixer_dev, top_group,
					       num, hdaudio_set_control, typ,
					       "mic-src-vol", maxval,
					       MIXF_READABLE |
					       MIXF_WRITEABLE |
					       MIXF_CENTIBEL)) < 0)
	    cmn_err (CE_WARN,
		     "hdaudio_lenovo_alc298_init: failed to create mic-src-vol control (%d)\n",
		     ctl);
	}
    }
  else
    cmn_err (CE_WARN,
	     "hdaudio_lenovo_alc298_init: widget 0x0b has no input amp, can't add mic-src-mute\n");

  /*
   * The real fix: widget 0x0c ("front", NT_MIXER) sums DAC 0x02 with the
   * AA-loopback bus 0x0b before the speaker pin -- found via 0x0b's own
   * reference list, not guessed. Mute just its input from 0x0b (found by
   * connection-list search, same as the DAC-pairing fixups above) and
   * default it to muted immediately: unlike mic-src-mute, there's no
   * capture tradeoff here, so there's no reason to leave the leak on by
   * default and require a manual toggle every boot.
   */
  {
    widget_t *front_mix = &codec->widgets[0x0c];
    int idx = -1, j2;

    for (j2 = 0; j2 < front_mix->nconn; j2++)
      if (front_mix->connections[j2] == 0x0b)
	{
	  idx = j2;
	  break;
	}

    if (idx < 0)
      cmn_err (CE_WARN,
	       "hdaudio_lenovo_alc298_init: widget 0x0c has no connection to 0x0b, can't fix speaker leak\n");
    else if (!(front_mix->widget_caps & WCAP_INPUT_AMP_PRESENT))
      cmn_err (CE_WARN,
	       "hdaudio_lenovo_alc298_init: widget 0x0c has no input amp, can't fix speaker leak\n");
    else
      {
	int ctl;
	int mixnum = MIXNUM (front_mix, CT_INMUTE, idx);

	if ((ctl = mixer_ext_create_control (mixer->mixer_dev, top_group,
					     mixnum,
					     hda_lenovo_loopback_mute,
					     MIXT_MUTE, "loopback-mute",
					     2,
					     MIXF_READABLE |
					     MIXF_WRITEABLE)) < 0)
	  cmn_err (CE_WARN,
		   "hdaudio_lenovo_alc298_init: failed to create loopback-mute control (%d)\n",
		   ctl);
	else
	  hda_lenovo_loopback_mute (mixer->mixer_dev, mixnum,
					    SNDCTL_MIX_WRITE, 1);

	if (front_mix->inamp_caps & ~AMPCAP_MUTE)
	  {
	    int maxval = hdaudio_amp_maxval (front_mix->inamp_caps);
	    int typ = (front_mix->widget_caps & WCAP_STEREO) ?
	      MIXT_STEREOSLIDER16 : MIXT_MONOSLIDER16;
	    int num = (front_mix->widget_caps & WCAP_STEREO) ?
	      MIXNUM (front_mix, CT_INSTEREO, idx) :
	      MIXNUM (front_mix, CT_INMONO, idx);

	    if ((ctl = mixer_ext_create_control (mixer->mixer_dev, top_group,
						 num, hdaudio_set_control,
						 typ, "loopback-vol",
						 maxval,
						 MIXF_READABLE |
						 MIXF_WRITEABLE |
						 MIXF_CENTIBEL)) < 0)
	      cmn_err (CE_WARN,
		       "hdaudio_lenovo_alc298_init: failed to create loopback-vol control (%d)\n",
		       ctl);
	  }
      }
  }

  /*
   * DACs 0x02 ("front") and 0x03 ("rear") each have a real, adjustable
   * output amp (127 steps, offset==numsteps -- the top of the range is
   * this codec's 0dB/nominal level, so driving it there is not an
   * out-of-spec boost, just removing an attenuation nobody asked for).
   * Neither is ever visited by the generic widget walker on this board:
   * they're only reachable as connections of mixer widgets 0x0c/0x0d
   * (which themselves have no adjustable gain, mute-only), so
   * attach_amplifiers() never runs on 0x02/0x03 and they're left at
   * whatever the codec/BIOS defaults to -- measured at ~80% of max on
   * this hardware, i.e. speaker and headphone output are already a few
   * dB down before any software volume control even applies. 0x02 feeds
   * every "front"-mode output pin (speaker 0x14, headphone 0x17/0x21 when
   * in front mode); 0x03 feeds the same pins in "rear" mode. Same
   * hdaudio_set_control()-backed slider shape as loopback-vol above, just
   * against these two DACs directly, initialized to their true max
   * instead of the usual 80% default.
   */
  {
    static const struct
    {
      unsigned char wid;
      const char *name;
    } dacs[] = {
      { 0x02, "front" },
      { 0x03, "rear" },
    };
    int i;

    for (i = 0; i < sizeof (dacs) / sizeof (dacs[0]); i++)
      {
	widget_t *dac = &codec->widgets[dacs[i].wid];

	if (!(dac->widget_caps & WCAP_OUTPUT_AMP_PRESENT) ||
	    !(dac->outamp_caps & ~AMPCAP_MUTE))
	  continue;

	{
	  int maxval = hdaudio_amp_maxval (dac->outamp_caps);
	  int typ = (dac->widget_caps & WCAP_STEREO) ?
	    MIXT_STEREOSLIDER16 : MIXT_MONOSLIDER16;
	  int num = (dac->widget_caps & WCAP_STEREO) ?
	    MIXNUM (dac, CT_OUTSTEREO, 0) : MIXNUM (dac, CT_OUTMONO, 0);
	  int ctl;

	  if ((ctl = mixer_ext_create_control (mixer->mixer_dev, top_group,
					       num, hdaudio_set_control, typ,
					       dacs[i].name, maxval,
					       MIXF_READABLE |
					       MIXF_WRITEABLE |
					       MIXF_CENTIBEL)) < 0)
	    cmn_err (CE_WARN,
		     "hdaudio_lenovo_alc298_init: failed to create %s control (%d)\n",
		     dacs[i].name, ctl);
	  else
	    {
	      int val = maxval | (maxval << 16);

	      hdaudio_set_control (mixer->mixer_dev, num, SNDCTL_MIX_WRITE,
				   val);
	    }
	}
      }
  }

  return OSS_EAGAIN;	/* Continue with the default (generic) mixer init */
}

int
hdaudio_mac_realtek_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
 	codec_t *codec = mixer->codecs[cad];
 	int afg = codec->afg;	// Audio function group root widget

cmn_err(CE_CONT, "iMac Realtek hdaudio initialization\n");

	corb_write (mixer, cad, afg, 0, SET_GPIO_DIR, 0xffffffff);
 	corb_write (mixer, cad, afg, 0, SET_GPIO_ENABLE, 0xffffffff);
	corb_write (mixer, cad, afg, 0, SET_GPIO_DATA, 0xffffffff);
	return hdaudio_mac_GPIO_init(dev, mixer, cad, top_group);
}

int
hdaudio_asus_a7k_GPIO_init (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
        DDB(cmn_err(CE_CONT, "hdaudio_asus_a7k_GPIO_init got called.\n"));

        corb_write (mixer, cad, 0x01, 0, SET_GPIO_ENABLE, 3);
        corb_write (mixer, cad, 0x01, 0, SET_GPIO_DIR, 1);
        corb_write (mixer, cad, 0x01, 0, SET_GPIO_DATA, 1);

        return hdaudio_generic_mixer_init(dev, mixer, cad, top_group);
}

int
hdaudio_GPIO_init_1 (int dev, hdaudio_mixer_t * mixer, int cad, int top_group)
{
	DDB(cmn_err(CE_CONT, "hdaudio_GPIO_init_1 got called.\n"));

	/* Acer TravelMate 4060 and similar Aspire series, with ALC260 codec, need
	 * that we init GPIO to get internal speaker and headphone jack working. */
	corb_write(mixer, cad, 0x01, 0, SET_GPIO_ENABLE, 1);
	corb_write(mixer, cad, 0x01, 0, SET_GPIO_DIR, 1);
	corb_write(mixer, cad, 0x01, 0, SET_GPIO_DATA, 1);
  
	return hdaudio_generic_mixer_init(dev, mixer, cad, top_group);
}
