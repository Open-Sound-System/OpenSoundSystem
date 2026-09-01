/*
 * Purpose: The High Definition Audio (HDA/Azalia) driver.
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

#ifdef __linux__
/*
 * Needed for the i915 display-power-well binding further down (see
 * the big comment above hda_i915_init()) -- pulled in here, before
 * oss_hdaudio_cfg.h's own compat-macro setup (printk/HZ/etc
 * redefinitions), because including them afterwards collides with
 * that setup instead of layering cleanly on top of it.
 */
#include <linux/component.h>
#include <linux/device.h>
#include <drm/intel/i915_component.h>
#endif

#include "oss_hdaudio_cfg.h"
#include "oss_pci.h"
#include "hdaudio.h"
#include "hdaudio_codec.h"
#include "spdif.h"

#define CORB_DELAY 10
#define CORB_LOOPS 1000

#define INTEL_VENDOR_ID         0x8086
#define INTEL_DEVICE_ICH6       0x2668
#define INTEL_DEVICE_ICH7       0x27d8
#define INTEL_DEVICE_ESB2       0x269a
#define INTEL_DEVICE_ICH8       0x284b
#define INTEL_DEVICE_ICH9       0x293e
#define INTEL_DEVICE_ICH9_B     0x293f
#define INTEL_DEVICE_ICH10      0x3a3e
#define INTEL_DEVICE_ICH10_B    0x3a6e
#define INTEL_DEVICE_PCH        0x3b56
#define INTEL_DEVICE_PCH_B      0x3b57
#define INTEL_DEVICE_CPT        0x1c20
#define INTEL_DEVICE_PBG        0x1d20
#define INTEL_DEVICE_PPT        0x1e20
#define INTEL_DEVICE_SCH        0x811b
#define INTEL_DEVICE_PCH_C      0x8c20
#define INTEL_DEVICE_HSW        0x0a0c
#define INTEL_DEVICE_BDW        0x160c
#define INTEL_DEVICE_BYT        0x0f04
#define INTEL_DEVICE_BSW        0x2284
#define INTEL_DEVICE_SKL_LP     0x9d70
#define INTEL_DEVICE_SPT_LP     0x9d71
#define INTEL_DEVICE_CNL_LP     0x9dc8
#define INTEL_DEVICE_WPT_LP     0x9ca0
#define INTEL_DEVICE_LPT_LP_0   0x9c20
#define INTEL_DEVICE_LPT_LP_1   0x9c21
#define INTEL_DEVICE_9_SERIES   0x8ca0
#define INTEL_DEVICE_SKL        0xa170
#define INTEL_DEVICE_KBL        0xa171
#define INTEL_DEVICE_KBL_H      0xa2f0
#define INTEL_DEVICE_CNL_H      0xa348
#define INTEL_DEVICE_CML_S      0xa3f0
#define INTEL_DEVICE_CML_LP     0x02c8
#define INTEL_DEVICE_CML_H      0x06c8
#define INTEL_DEVICE_ICL_LP     0x34c8
#define INTEL_DEVICE_ICL_N      0x38c8
#define INTEL_DEVICE_ICL_H      0x3dc8
#define INTEL_DEVICE_TGL_LP     0xa0c8
#define INTEL_DEVICE_TGL_H      0x43c8
#define INTEL_DEVICE_ADL_P      0x51c8
#define INTEL_DEVICE_ADL_PX     0x51cd
#define INTEL_DEVICE_ADL_M      0x51cc
#define INTEL_DEVICE_ADL_N      0x54c8
#define INTEL_DEVICE_ADL_S      0x7ad0
#define INTEL_DEVICE_RPL_S      0x7a50
#define INTEL_DEVICE_APL        0x5a98
#define INTEL_DEVICE_GML        0x3198

/*
 * PCI config offset/bit ALSA's hda_intel.c clears before, and restores
 * after, every controller reset on Skylake-and-later Intel controllers
 * (AZX_DRIVER_SKL in hda_intel_init_chip()) -- disables a specific bit
 * clock-domain clock gate that otherwise keeps the reset from actually
 * reaching (and waking) the codec. See init_HDA()/oss_hdaudio_resume().
 */
#define INTEL_HDA_CGCTL			0x48
#define INTEL_HDA_CGCTL_MISCBDCGE	(0x1 << 6)

#define NVIDIA_VENDOR_ID        0x10de
#define NVIDIA_DEVICE_MCP51     0x026c
#define NVIDIA_DEVICE_MCP55     0x0371
#define NVIDIA_DEVICE_MCP61     0x03e4
#define NVIDIA_DEVICE_MCP61A    0x03f0
#define NVIDIA_DEVICE_MCP65     0x044a
#define NVIDIA_DEVICE_MCP67     0x055c
#define NVIDIA_DEVICE_MCP73     0x07fc
#define NVIDIA_DEVICE_MCP78S    0x0774
#define NVIDIA_DEVICE_MCP79     0x0ac0

#define ATI_VENDOR_ID           0x1002
#define ATI_DEVICE_SB450        0x437b
#define ATI_DEVICE_SB600        0x4383

#define AMD_VENDOR_ID           0x1022
#define AMD_DEVICE_HUDSON       0x780d

#define VIA_VENDOR_ID           0x1106
#define VIA_DEVICE_HDA          0x3288

#define SIS_VENDOR_ID           0x1039
#define SIS_DEVICE_HDA          0x7502

#define ULI_VENDOR_ID           0x10b9
#define ULI_DEVICE_HDA          0x5461

#define CREATIVE_ID		0x1102
#define CREATIVE_XFI_HDA	0x0009

#define BDL_SIZE	32
/*
 * GCAP's input/output stream-engine-count fields are 4 bits wide (see
 * setup_engines()), so a controller can report up to 15 of each. Size for
 * the full field range rather than an arbitrary smaller number, or engines
 * a controller actually has get silently dropped (seen in practice: a
 * 9-output-engine controller capped down to 8 by this being too small).
 */
#define HDA_MAX_ENGINES	16
#define MAX_OUTPUTS 	8
#define MAX_INPUTS	4

typedef struct
{
  oss_uint64_t addr;
  unsigned int len;
  unsigned int ioc;
} bdl_t;

typedef struct hda_portc_t hda_portc_t;

typedef struct
{
  int num;
  int busy;
  bdl_t *bdl;
  oss_uint64_t bdl_phys;
  oss_dma_handle_t bdl_dma_handle;
  int bdl_size, bdl_max;
  unsigned char *base;
  unsigned int intrmask;
  hda_portc_t *portc;
} hda_engine_t;

struct hda_portc_t
{
  int num;
  int open_mode;
  int speed, bits, channels;
  int audio_enabled;
  int trigger_bits;
  int audiodev;
  int port_type;
#define PT_OUTPUT	0
#define PT_INPUT	1
  hdaudio_endpointinfo_t *endpoint;
  hda_engine_t *engine;
};

typedef struct
{
  unsigned int response, resp_ex;
} rirb_entry_t;

/*
 * Suspend/resume support (see oss_hdaudio_suspend()/oss_hdaudio_resume()).
 *
 * The HD Audio controller and codec have no suspend/resume support of
 * their own in the ACPI S3 sense -- they simply lose all of their
 * internal state (CORB/RIRB, every verb-programmed widget setting)
 * across a suspend, coming back at D0 in their power-on-default state,
 * exactly like a cold boot. To restore working audio without tearing
 * down (and thus needing to recreate) any device file, mixer control
 * or DMA buffer -- which would require first evicting every process
 * with the device open -- every "persistent" verb (one that programs
 * lasting widget state, as opposed to a one-shot command or a GET
 * query) sent through do_corb_write() is cached here, keyed by
 * (cad, nid, direct, verb). On resume the controller is reset and the
 * entire cache is simply replayed, same idea as ALSA's regmap cache
 * sync on codec resume.
 */
#define HDA_VERB_CACHE_SIZE	2048

typedef struct
{
  unsigned short cad, nid;
  unsigned char direct;
  unsigned short verb;
  unsigned int parm;
} hda_verb_cache_ent_t;

typedef struct hda_devc_t
{
  oss_device_t *osdev;
  oss_native_word base;
  unsigned int membar_addr;
  unsigned char *azbar;

  char *chip_name;
  unsigned int vendor_id, subvendor_id;

  int irq;
  oss_mutex_t mutex;
  oss_mutex_t low_mutex;

  /* CORB and RIRB */
  int corbsize, rirbsize;
  unsigned int *corb;
  rirb_entry_t *rirb;
  oss_uint64_t corb_phys, rirb_phys;
  int rirb_rp;
  unsigned int rirb_upper, rirb_lower;
  volatile int rirb_empty;

  oss_dma_handle_t corb_dma_handle;

  /* Mixer */
  unsigned short codecmask;
  hdaudio_mixer_t *mixer;
  int mixer_dev;
  spdif_devc spdc;

  /* Audio */
  int first_dev;
  hda_engine_t inengines[HDA_MAX_ENGINES];
  hda_engine_t outengines[HDA_MAX_ENGINES];
  int num_outengines, num_inengines;
  hdaudio_endpointinfo_t *spdifout_endpoint;

  hda_portc_t output_portc[MAX_OUTPUTS];
  hda_portc_t input_portc[MAX_INPUTS];
  int num_outputs, num_inputs;

  int num_spdin, num_spdout;
  int num_mdmin, num_mdmout;

  /* Suspend/resume: see hda_verb_cache_ent_t above */
  hda_verb_cache_ent_t verb_cache[HDA_VERB_CACHE_SIZE];
  int verb_cache_n;
  int verb_cache_full;		/* Logged the overflow warning already? */
}
hda_devc_t;

static int
rirb_intr (hda_devc_t * devc)
{
  int serviced = 0;
  unsigned char rirbsts;
  oss_native_word flags;

  rirbsts = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBSTS);
  if (rirbsts != 0)
    {
      serviced = 1;

      if (rirbsts & 0x01)
	{
	  /* RIRB response interrupt */
	  int wp, rp;
	  unsigned int upper = 0, lower = 0;

	  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
	  wp = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBWP) & 0x00ff;
	  while (devc->rirb_rp != wp)
	    {
	      devc->rirb_rp++;
	      devc->rirb_rp %= devc->rirbsize;
	      rp = devc->rirb_rp;
	      upper = devc->rirb[rp].response;
	      lower = devc->rirb[rp].resp_ex;

	      if (lower & 0x10)	/* Unsolicited response */
		{
		  hda_codec_unsol (devc->mixer, upper, lower);
		}
	      else if (devc->rirb_empty)
		{
		  devc->rirb_upper = upper;
		  devc->rirb_lower = lower;
		  devc->rirb_empty--;
#if 0
		  cmn_err (CE_NOTE,
			   "oss_hdaudio: RIRB entry %d: upper=%08x lower=%08x\n",
			   rp, upper, lower);
#endif
		}
	    }
	  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
	}
      PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSTS, rirbsts);
    }

  return serviced;
}

static int
hdaintr (oss_device_t * osdev)
{
  hda_devc_t *devc = (hda_devc_t *) osdev->devc;
  unsigned int status;
  int serviced = 0;
  int i;

  if (devc == NULL)		/* Too bad */
    return 1;

  status = PCI_READL (devc->osdev, devc->azbar + HDA_INTSTS);
  if (status != 0)
    {
      serviced = 1;

      for (i = 0; i < devc->num_outengines; i++)
	{
	  hda_engine_t *engine = &devc->outengines[i];

	  hda_portc_t *portc;
	  if (status & engine->intrmask)
	    {
	      PCI_WRITEB (devc->osdev, engine->base + 0x03, 0x1e);

	      portc = engine->portc;

	      if (portc != NULL && (portc->trigger_bits & PCM_ENABLE_OUTPUT))
		oss_audio_outputintr (portc->audiodev, 1);
	    }
	}

      for (i = 0; i < devc->num_inengines; i++)
	{
	  hda_engine_t *engine = &devc->inengines[i];

	  hda_portc_t *portc;
	  if (status & engine->intrmask)
	    {
	      PCI_WRITEB (devc->osdev, engine->base + 0x03, 0x1e);

	      portc = engine->portc;

	      if (portc != NULL && (portc->trigger_bits & PCM_ENABLE_INPUT))
		oss_audio_inputintr (portc->audiodev, 0);
	    }
	}
      PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTSTS, status);	/* ACK */

      if (status & (1 << 30))	/* Controller interrupt (RIRB) */
	{
	  if (rirb_intr (devc))
	    serviced = 1;
	}
    }
  return serviced;
}

/*
 * Is this verb worth caching for suspend/resume replay? Only verbs that
 * program lasting widget state belong here -- one-shot commands
 * (SET_CODEC_RESET) and GET_* queries (which never reach do_corb_write()
 * as a "verb" in the first place, but let's not assume) must not be
 * replayed blindly.
 */
static int
hda_verb_is_persistent (unsigned int verb)
{
  if ((verb & 0xf00) == 0x300)	/* SET_GAIN, any side/index/type */
    return 1;

  switch (verb)
    {
    case SET_SELECTOR:
    case SET_PINCTL:
    case SET_EAPD:
    case SET_POWER_STATE:
    case SET_CONVERTER:
    case SET_CONVERTER_FORMAT:
    case SET_GPIO_DIR:
    case SET_GPIO_ENABLE:
    case SET_GPIO_DATA:
    case SET_GPIO_WKEN:
    case SET_GPIO_UNSOL:
    case SET_GPIO_STICKY:
      return 1;
    }

  return 0;
}

static void
hda_verb_cache_record (hda_devc_t * devc, unsigned int cad, unsigned int nid,
			unsigned int d, unsigned int verb, unsigned int parm)
{
  int i;

  if (!hda_verb_is_persistent (verb))
    return;

  for (i = 0; i < devc->verb_cache_n; i++)
    if (devc->verb_cache[i].cad == cad && devc->verb_cache[i].nid == nid &&
	devc->verb_cache[i].direct == d && devc->verb_cache[i].verb == verb)
      {
	devc->verb_cache[i].parm = parm;
	return;
      }

  if (devc->verb_cache_n >= HDA_VERB_CACHE_SIZE)
    {
      if (!devc->verb_cache_full)
	{
	  devc->verb_cache_full = 1;
	  cmn_err (CE_WARN,
		   "oss_hdaudio: verb cache full -- some codec state may "
		   "not survive suspend/resume\n");
	}
      return;
    }

  devc->verb_cache[devc->verb_cache_n].cad = cad;
  devc->verb_cache[devc->verb_cache_n].nid = nid;
  devc->verb_cache[devc->verb_cache_n].direct = d;
  devc->verb_cache[devc->verb_cache_n].verb = verb;
  devc->verb_cache[devc->verb_cache_n].parm = parm;
  devc->verb_cache_n++;
}

static int
do_corb_write (void *dc, unsigned int cad, unsigned int nid, unsigned int d,
	       unsigned int verb, unsigned int parm)
{
  unsigned int wp;
  unsigned int tmp;
  oss_native_word flags;
  hda_devc_t *devc = (hda_devc_t *) dc;

  hda_verb_cache_record (devc, cad, nid, d, verb, parm);

  tmp = (cad << 28) | (d << 27) | (nid << 20) | (verb << 8) | (parm & 0xffff);
  wp = PCI_READB (devc->osdev, devc->azbar + HDA_CORBWP) & 0x00ff;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  wp = (wp + 1) % devc->corbsize;

  devc->corb[wp] = tmp;
  devc->rirb_empty++;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_CORBWP, wp);

  return 1;
}

static int
do_corb_write_nomutex (void *dc, unsigned int cad, unsigned int nid, unsigned int d,
	       unsigned int verb, unsigned int parm)
{
  unsigned int wp;
  unsigned int tmp;
  hda_devc_t *devc = (hda_devc_t *) dc;

  tmp = (cad << 28) | (d << 27) | (nid << 20) | (verb << 8) | (parm & 0xffff);
  wp = PCI_READB (devc->osdev, devc->azbar + HDA_CORBWP) & 0x00ff;

  wp = (wp + 1) % devc->corbsize;

  devc->corb[wp] = tmp;
  devc->rirb_empty++;
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_CORBWP, wp);

  return 1;
}

static int
do_corb_read (void *dc, unsigned int cad, unsigned int nid, unsigned int d,
	      unsigned int verb, unsigned int parm, unsigned int *upper,
	      unsigned int *lower)
{
  int tmout;
  hda_devc_t *devc = (hda_devc_t *) dc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  do_corb_write_nomutex (devc, cad, nid, d, verb, parm);

  tmout = CORB_LOOPS;
  while (devc->rirb_empty)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

      if (--tmout < 0)
	{
	  devc->rirb_rp = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBWP);
	  devc->rirb_empty = 0;
	  return 0;
	}
      oss_udelay (CORB_DELAY);
      MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
    }

  *upper = devc->rirb_upper;
  *lower = devc->rirb_lower;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 1;
}

static int
do_corb_read_poll (void *dc, unsigned int cad, unsigned int nid,
		   unsigned int d, unsigned int verb, unsigned int parm,
		   unsigned int *upper, unsigned int *lower)
{
  int tmout = 0;
  hda_devc_t *devc = (hda_devc_t *) dc;
  oss_native_word flags;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  do_corb_write_nomutex (devc, cad, nid, d, verb, parm);

  tmout = CORB_LOOPS;
  while (devc->rirb_empty)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

      rirb_intr (devc);

      if (--tmout < 0)
	{
	  devc->rirb_rp = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBWP);
	  devc->rirb_empty = 0;
	  return 0;
	}
      oss_udelay (CORB_DELAY);
      MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
    }

  *upper = devc->rirb_upper;
  *lower = devc->rirb_lower;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return 1;
}

#undef corb_read

static int
corb_read (void *dc, unsigned int cad, unsigned int nid, unsigned int d,
	   unsigned int verb, unsigned int parm, unsigned int *upper,
	   unsigned int *lower)
{
  hda_devc_t *devc = (hda_devc_t *) dc;

/*
 * Do three retries using different access methods
 */
  if (do_corb_read (dc, cad, nid, d, verb, parm, upper, lower))
    return 1;

  PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0x02);	/* Intr disable */

  if (do_corb_read_poll (dc, cad, nid, d, verb, parm, upper, lower))
    {
      PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0x03);	/* Intr re-enable */
      return 1;
    }

  if (do_corb_read_poll (dc, cad, nid, d, verb, parm, upper, lower))
    {
      PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0x03);	/* Intr re-enable */
      return 1;
    }

#if 0
  // TODO: Implement this
  if (do_corb_read_single (dc, cad, nid, d, verb, parm, upper, lower))
    {
      PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0x03);	/* Intr re-enable */
      return 1;
    }
#endif

  PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0x03);	/* Intr re-enable */

  cmn_err (CE_WARN,
	   "RIRB timeout (cad=%d, nid=%d, d=%d, verb=%03x, parm=%x)\n",
	   cad, nid, d, verb, parm);

  return 0;
}

/*
 * Audio routines
 */

static int
hda_audio_set_rate (int dev, int arg)
{
  hda_portc_t *portc = audio_engines[dev]->portc;
  adev_p adev = audio_engines[dev];

  int best = -1, bestdiff = 10000000;
  int i;

  if (arg == 0)
    return portc->speed;

  for (i = 0; i < adev->nrates; i++)
    {
      int diff = arg - adev->rates[i];
      if (diff < 0)
	diff = -diff;

      if (diff < bestdiff)
	{
	  best = i;
	  bestdiff = diff;
	}
    }

  if (best == -1)
    {
      cmn_err (CE_WARN, "No suitable rate found!\n");
      return portc->speed = 48000;	/* Some default */
    }
  portc->speed = adev->rates[best];
  return portc->speed;
}

static short
hda_audio_set_channels (int dev, short arg)
{
  hda_portc_t *portc = audio_engines[dev]->portc;
  hda_devc_t *devc = audio_engines[dev]->devc;
  adev_p adev = audio_engines[dev];
  int i, n1, n2;

  if (arg == 0)
    {
      return portc->channels;
    }

  if (arg < 1)
    arg = 1;

  if (arg > adev->max_channels)
  {
    arg = adev->max_channels;
  }

  if (arg != 1 && arg != 2 && arg != 4 && arg != 6 && arg != 8)
    {
      cmn_err (CE_NOTE, "Ignored request for odd number (%d) of channels\n", arg);
      return portc->channels = arg & ~1;
    }

  if (arg < adev->min_channels)
    arg = adev->min_channels;

  /*
   * Check if more output endpoints need to be allocated
   */
  n2 = (arg + 1) / 2;
  n1 = (portc->channels + 1) / 2;
  if (n1 < 1)
    n1 = 1;

  if (n2 > n1)			/* Needs more stereo pairs */
    {
      oss_native_word flags;

      MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

      for (i = n1; i < n2; i++)
	{
	  if (portc->endpoint[i].busy)
	    {
	      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
	      return portc->channels;
	    }
	}

      for (i = n1; i < n2; i++)
	{
	  portc->endpoint[i].busy = 1;
	  portc->endpoint->borrow_count++;
	}

      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
    }
  else if (n2 < n1)		/* Some stereo pairs can be released */
    {
      for (i = n2; i < n1; i++)
	{
	  portc->endpoint[i].busy = 0;
	  portc->endpoint->borrow_count--;
	}
    }

  return portc->channels = arg;
}

static unsigned int
hda_audio_set_format (int dev, unsigned int arg)
{
  hda_portc_t *portc = audio_engines[dev]->portc;

  if (arg == 0)
    return portc->bits;

  if (!(arg & audio_engines[dev]->oformat_mask))
    return portc->bits;
  portc->bits = arg;

  return portc->bits;
}

static int
do_corb_write_simple (void *dc, unsigned int v)
{
  unsigned int wp;
  hda_devc_t *devc = (hda_devc_t *) dc;

  wp = PCI_READB (devc->osdev, devc->azbar + HDA_CORBWP) & 0x00ff;

  wp = (wp + 1) % devc->corbsize;

  devc->corb[wp] = v;
  devc->rirb_empty++;
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_CORBWP, wp);

  return 1;
}

static int
do_corb_read_simple (void *dc, unsigned int cmd, unsigned int *v)
{
  int tmout = 0;
  hda_devc_t *devc = (hda_devc_t *) dc;

  do_corb_write_simple (dc, cmd);

  tmout = CORB_LOOPS;
  while (devc->rirb_empty)
    {
      if (--tmout < 0)
	{
	  cmn_err (CE_WARN, "RIRB timeout (cmd=%08x)\n", cmd);
	  devc->rirb_rp = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBWP);
	  devc->rirb_empty = 0;
	  return 0;
	}
      oss_udelay (CORB_DELAY);
    }

  *v = devc->rirb_upper;

  return 1;
}

static int
hda_audio_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
  hda_devc_t *devc = audio_engines[dev]->devc;
  hda_portc_t *portc = audio_engines[dev]->portc;

  //if (hdaudio_snoopy)
    switch (cmd)
      {
      case HDA_IOCTL_WRITE:
#ifdef GET_PROCESS_UID
	if (GET_PROCESS_UID () != 0)	/* Not root */
	  return OSS_EINVAL;
#endif
	do_corb_write_simple (devc, *(unsigned int *) arg);
	return 0;
	break;

      case HDA_IOCTL_READ:
#ifdef GET_PROCESS_UID
	if (GET_PROCESS_UID () != 0)	/* Not root */
	  return OSS_EINVAL;
#endif
	if (!do_corb_read_simple
	    (devc, *(unsigned int *) arg, (unsigned int *) arg))
	  return OSS_EIO;

	return 0;
	break;

      case HDA_IOCTL_NAME:
#ifdef GET_PROCESS_UID
	if (GET_PROCESS_UID () != 0)	/* Not root */
	  return OSS_EINVAL;
#endif
	return hda_codec_getname (devc->mixer, (hda_name_t *) arg);
	break;

      case HDA_IOCTL_WIDGET:
#ifdef GET_PROCESS_UID
	if (GET_PROCESS_UID () != 0)	/* Not root */
	  return OSS_EINVAL;
#endif
	return hda_codec_getwidget (devc->mixer, (hda_widget_info_t *) arg);
	break;

      }
  return hdaudio_codec_audio_ioctl (devc->mixer, portc->endpoint, cmd, arg);
}

static void hda_audio_trigger (int dev, int state);

static void
hda_audio_reset (int dev)
{
  hda_audio_trigger (dev, 0);
}

/*ARGSUSED*/
static int
hda_audio_open (int dev, int mode, int openflags)
{
  hda_portc_t *portc = audio_engines[dev]->portc;
  hda_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
  hda_engine_t *engines, *engine = NULL;
  int i, n;
  unsigned int tmp;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  if (portc->open_mode || portc->endpoint->busy)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      return OSS_EBUSY;
    }

  if (portc->port_type == PT_INPUT)
    {
      engines = devc->inengines;
      n = devc->num_inengines;
    }
  else
    {
      engines = devc->outengines;
      n = devc->num_outengines;
    }

  for (i = 0; i < n && engine == NULL; i++)
    {
      if (!engines[i].busy)
	engine = &engines[i];
    }

  if (engine == NULL)
    {
      MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
      cmn_err (CE_WARN, "No free DMA engines.\nn");
      return OSS_EBUSY;
    }

  portc->open_mode = mode;
  portc->audio_enabled &= ~mode;
  portc->endpoint->busy = 1;
  portc->endpoint->borrow_count = 1;

  portc->engine = engine;
  engine->portc = portc;
  portc->engine->busy = 1;

  portc->endpoint->stream_number = portc->endpoint->default_stream_number;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

/*
 * Reset the DMA engine and wait for the reset to complete
 */
  tmp = PCI_READL (devc->osdev, engine->base);
  PCI_WRITEL (devc->osdev, engine->base, tmp | 0x01);	/* Stream reset */

  for (i = 0; i < 1000; i++)
    {
      if (PCI_READL (devc->osdev, engine->base) & 0x01)
	break;
      oss_udelay (1000);
    }

  /* Unreset and wait */

  tmp = PCI_READL (devc->osdev, engine->base);
  PCI_WRITEL (devc->osdev, engine->base, tmp & ~0x01);	/* Release reset */

  for (i = 0; i < 1000; i++)
    {
      if (!(PCI_READL (devc->osdev, engine->base) & 0x01))
	break;
      oss_udelay (1000);
    }

  return 0;
}

static void
hda_audio_close (int dev, int mode)
{
  hda_portc_t *portc = audio_engines[dev]->portc;
  hda_devc_t *devc = audio_engines[dev]->devc;
  oss_native_word flags;
  int i;

  if (!portc->open_mode)
    {
      cmn_err (CE_WARN, "Bad close %d\n", dev);
      return;
    }

  hda_audio_reset (dev);
  hdaudio_codec_reset_endpoint (devc->mixer, portc->endpoint,
				portc->channels);

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->audio_enabled &= ~mode;
  portc->engine->busy = 0;
  portc->engine->portc = NULL;
  portc->engine = NULL;
  portc->endpoint->busy = 0;
  portc->endpoint->stream_number = portc->endpoint->default_stream_number;

  for (i = 1; i < portc->endpoint->borrow_count; i++)
    {
      portc->endpoint[i].busy = 0;
    }

  portc->endpoint->borrow_count = 1;
  portc->open_mode = 0;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

/*ARGSUSED*/
static void
hda_audio_output_block (int dev, oss_native_word buf, int count,
			int fragsize, int intrflag)
{
  hda_portc_t *portc = audio_engines[dev]->portc;

  portc->audio_enabled |= PCM_ENABLE_OUTPUT;
  portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
}

/*ARGSUSED*/
static void
hda_audio_start_input (int dev, oss_native_word buf, int count,
		       int fragsize, int intrflag)
{
  hda_portc_t *portc = audio_engines[dev]->portc;

  portc->audio_enabled |= PCM_ENABLE_INPUT;
  portc->trigger_bits &= ~PCM_ENABLE_INPUT;
}

static void
hda_audio_trigger (int dev, int state)
{
  hda_devc_t *devc = audio_engines[dev]->devc;
  hda_portc_t *portc = audio_engines[dev]->portc;
  hda_engine_t *engine = portc->engine;
  oss_native_word flags;
  unsigned char tmp, intr;

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);

  tmp = PCI_READB (devc->osdev, engine->base + 0x00);
  intr = PCI_READB (devc->osdev, devc->azbar + HDA_INTCTL);
  if (portc->open_mode & OPEN_WRITE)
    {
      if (state & PCM_ENABLE_OUTPUT)
	{
	  if ((portc->audio_enabled & PCM_ENABLE_OUTPUT) &&
	      !(portc->trigger_bits & PCM_ENABLE_OUTPUT))
	    {
	      portc->trigger_bits |= PCM_ENABLE_OUTPUT;
	      tmp |= 0x1e;	/* DMA and Stream Interrupt enable */
	      intr |= engine->intrmask;
	      PCI_WRITEB (devc->osdev, engine->base + 0x00, tmp);
	      PCI_WRITEB (devc->osdev, devc->azbar + HDA_INTCTL, intr);
	    }
	}
      else
	{
	  if ((portc->audio_enabled & PCM_ENABLE_OUTPUT) &&
	      (portc->trigger_bits & PCM_ENABLE_OUTPUT))
	    {
	      portc->audio_enabled &= ~PCM_ENABLE_OUTPUT;
	      portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
	      tmp &= ~0x1e;	/* Run-off & intr disable */
	      intr &= ~engine->intrmask;
	      /* Stop DMA and Stream Int */
	      PCI_WRITEB (devc->osdev, engine->base + 0x00, tmp);
	      /* Ack pending ints */
	      PCI_WRITEB (devc->osdev, engine->base + 0x03, 0x1c);
	      /* Disable Interrupts */
	      PCI_WRITEB (devc->osdev, devc->azbar + HDA_INTCTL, intr);
	    }
	}
    }

  if (portc->open_mode & OPEN_READ)
    {
      if (state & PCM_ENABLE_INPUT)
	{
	  if ((portc->audio_enabled & PCM_ENABLE_INPUT) &&
	      !(portc->trigger_bits & PCM_ENABLE_INPUT))
	    {
	      portc->trigger_bits |= PCM_ENABLE_INPUT;
	      tmp |= 0x1e;	/* dma & intr enable */
	      intr |= engine->intrmask;
	      PCI_WRITEB (devc->osdev, engine->base + 0x00, tmp);
	      PCI_WRITEB (devc->osdev, devc->azbar + HDA_INTCTL, intr);
	    }
	}
      else
	{
	  if ((portc->audio_enabled & PCM_ENABLE_INPUT) &&
	      (portc->trigger_bits & PCM_ENABLE_INPUT))
	    {
	      portc->audio_enabled &= ~PCM_ENABLE_INPUT;
	      portc->trigger_bits &= ~PCM_ENABLE_INPUT;
	      tmp &= ~0x1e;	/* Run-off & intr disable */
	      intr &= ~engine->intrmask;
	      /* Stop DMA and Stream Int */
	      PCI_WRITEB (devc->osdev, engine->base + 0x00, tmp);
	      /* Ack pending ints */
	      PCI_WRITEB (devc->osdev, engine->base + 0x03, 0x1c);
	      /* Disable Interrupts */
	      PCI_WRITEB (devc->osdev, devc->azbar + HDA_INTCTL, intr);
	    }
	}
    }
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
}

static void
init_bdl (hda_devc_t * devc, hda_engine_t * engine, dmap_p dmap)
{
  int i;
  bdl_t *bdl = engine->bdl;

  PCI_WRITEL (devc->osdev, engine->base + HDA_SD_BDLPL, 0);
  PCI_WRITEL (devc->osdev, engine->base + HDA_SD_BDLPU, 0);
  for (i = 0; i < dmap->nfrags; i++)
    {
      bdl[i].addr = dmap->dmabuf_phys + (i * dmap->fragment_size);
      bdl[i].len = dmap->fragment_size;
      bdl[i].ioc = 0x01;
    }
}

static int
setup_audio_engine (hda_devc_t * devc, hda_engine_t * engine, hda_portc_t * portc,
		    dmap_t * dmap)
{
  unsigned int tmp, tmout;

/* make sure the run bit is zero for SD */
  PCI_WRITEB (devc->osdev, engine->base + HDA_SD_CTL,
	      PCI_READB (devc->osdev, engine->base + HDA_SD_CTL) & ~0x2);
/*
 * First reset the engine.
 */

  tmp = PCI_READB (devc->osdev, engine->base + HDA_SD_CTL);
  PCI_WRITEB (devc->osdev, engine->base + HDA_SD_CTL, tmp | 0x01);	/* Reset */
  oss_udelay (1000);

  tmout = 300;
  while (!(PCI_READB (devc->osdev, engine->base + HDA_SD_CTL) & 0x01)
	 && --tmout)
    oss_udelay (1000);

  PCI_WRITEB (devc->osdev, engine->base + HDA_SD_CTL, tmp & ~0x01);	/* Release reset */
  oss_udelay (1000);
  tmout = 300;
  while ((PCI_READB (devc->osdev, engine->base + HDA_SD_CTL) & 0x01)
	 && --tmout)
    oss_udelay (1000);

/*
 * Set the engine tag number field
 */
  tmp = PCI_READL (devc->osdev, engine->base + HDA_SD_CTL);
  tmp &= ~(0xf << 20);
  tmp |= portc->endpoint->stream_number << 20;
  PCI_WRITEL (devc->osdev, engine->base + HDA_SD_CTL, tmp);

/* program buffer size and fragments in the engine */

  PCI_WRITEL (devc->osdev, engine->base + HDA_SD_CBL, dmap->bytes_in_use);
  PCI_WRITEW (devc->osdev, engine->base + HDA_SD_LVI, dmap->nfrags - 1);

/* setup the BDL base address */
  tmp = engine->bdl_phys;
  PCI_WRITEL (devc->osdev, engine->base + HDA_SD_BDLPL, tmp & 0xffffffff);
  tmp = engine->bdl_phys >> 32;
  PCI_WRITEL (devc->osdev, engine->base + HDA_SD_BDLPU, tmp & 0xffffffff);
  PCI_WRITEB (devc->osdev, engine->base + HDA_SD_STS, 0x1c);	/* mask out ints */

/*
 * Next the sample rate and format setup
 */
  if (hdaudio_codec_setup_endpoint
      (devc->mixer, portc->endpoint, portc->speed, portc->channels,
       portc->bits, portc->endpoint->stream_number, &tmp) < 0)
    {
      cmn_err (CE_WARN, "Codec setup failed\n");
      return OSS_EIO;
    }

  PCI_WRITEW (devc->osdev, engine->base + HDA_SD_FORMAT, tmp);
  tmp = PCI_READW (devc->osdev, engine->base + HDA_SD_FORMAT);

  return 0;
}

/*ARGSUSED*/
static int
hda_audio_prepare_for_input (int dev, int bsize, int bcount)
{
  hda_devc_t *devc = audio_engines[dev]->devc;
  hda_portc_t *portc = audio_engines[dev]->portc;
  dmap_t *dmap = audio_engines[dev]->dmap_in;
  oss_native_word flags;
  hda_engine_t *engine;

  if (portc->port_type != PT_INPUT)
    return OSS_ENOTSUP;

  engine = portc->engine;

  if (dmap->nfrags > engine->bdl_max)	/* Overflow protection */
    {
      dmap->nfrags = engine->bdl_max;
      dmap->bytes_in_use = dmap->nfrags * dmap->fragment_size;
    }

  init_bdl (devc, engine, dmap);

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->audio_enabled &= ~PCM_ENABLE_INPUT;
  portc->trigger_bits &= ~PCM_ENABLE_INPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);
  return setup_audio_engine (devc, engine, portc, dmap);
}

/*ARGSUSED*/
static int
hda_audio_prepare_for_output (int dev, int bsize, int bcount)
{
  hda_devc_t *devc = audio_engines[dev]->devc;
  hda_portc_t *portc = audio_engines[dev]->portc;
  hda_engine_t *engine;

  dmap_t *dmap = audio_engines[dev]->dmap_out;
  oss_native_word flags;

  if (portc->port_type != PT_OUTPUT)
    return OSS_ENOTSUP;

  engine = portc->engine;

  if (dmap->nfrags > engine->bdl_max)	/* Overflow protection */
    {
      dmap->nfrags = engine->bdl_max;
      dmap->bytes_in_use = dmap->nfrags * dmap->fragment_size;
    }

  init_bdl (devc, engine, dmap);

  MUTEX_ENTER_IRQDISABLE (devc->mutex, flags);
  portc->audio_enabled &= ~PCM_ENABLE_OUTPUT;
  portc->trigger_bits &= ~PCM_ENABLE_OUTPUT;
  MUTEX_EXIT_IRQRESTORE (devc->mutex, flags);

  return setup_audio_engine (devc, engine, portc, dmap);
}

/*ARGSUSED*/
static int
hda_get_buffer_pointer (int dev, dmap_t * dmap, int direction)
{
  hda_portc_t *portc = audio_engines[dev]->portc;
#ifdef sun
  // PCI_READL under solaris needs devc->osdev
  hda_devc_t *devc = audio_engines[dev]->devc;
#endif
  hda_engine_t *engine;
  unsigned int ptr;

  engine = portc->engine;
  ptr =
    PCI_READL (devc->osdev, engine->base + HDA_SD_LPIB) % dmap->bytes_in_use;

  return ptr;
}

static const audiodrv_t hda_audio_driver = {
  hda_audio_open,
  hda_audio_close,
  hda_audio_output_block,
  hda_audio_start_input,
  hda_audio_ioctl,
  hda_audio_prepare_for_input,
  hda_audio_prepare_for_output,
  hda_audio_reset,
  NULL,
  NULL,
  NULL,
  NULL,
  hda_audio_trigger,
  hda_audio_set_rate,
  hda_audio_set_format,
  hda_audio_set_channels,
  NULL,
  NULL,
  NULL,				/* hda_check_input, */
  NULL,				/* hda_check_output, */
  NULL,				/* hda_alloc_buffer */
  NULL,				/* hda_free_buffer */
  NULL,
  NULL,
  hda_get_buffer_pointer
};

/*
 * Number of times to repeat the CRST low->high link reset pulse below
 * if STATESTS still shows no codec present at all after the first
 * attempt. Some Intel PCH controllers (seen in practice on this exact
 * chip, 8086:a170, after several back-to-back resets in one session --
 * e.g. repeated module reloads or several suspend/resume cycles) don't
 * reliably re-assert a codec's presence bit on the very first pulse; a
 * second or third full pulse recovers it without needing a cold boot.
 * ALSA's azx_reset() has the same double-pulse-on-empty-STATESTS quirk.
 */
#define CRST_RETRIES	4

/*
 * See the big comment above INTEL_HDA_CGCTL's #define. Intel-only;
 * harmless on chipsets that don't implement/use this bit, but not
 * worth risking on anything that isn't actually an Intel controller.
 */
static void
hda_intel_cgctl_set (hda_devc_t * devc, int enable)
{
  unsigned int val;

  if ((devc->vendor_id >> 16) != INTEL_VENDOR_ID)
    return;

  pci_read_config_dword (devc->osdev, INTEL_HDA_CGCTL, &val);
  if (enable)
    val |= INTEL_HDA_CGCTL_MISCBDCGE;
  else
    val &= ~INTEL_HDA_CGCTL_MISCBDCGE;
  pci_write_config_dword (devc->osdev, INTEL_HDA_CGCTL, val);
}

static int
reset_controller (hda_devc_t * devc)
{
  unsigned int tmp, tmout;
  int attempt;

  for (attempt = 0; attempt < CRST_RETRIES; attempt++)
    {
      /*reset the controller by writing a 0*/
      tmp = PCI_READL (devc->osdev, devc->azbar + HDA_GCTL);
      tmp &= ~CRST;
      PCI_WRITEL (devc->osdev, devc->azbar + HDA_GCTL, tmp);

      /*wait until the controller writes a 0 to indicate reset is done or until 50ms have passed*/
      tmout = 50;
      while ((PCI_READL (devc->osdev, devc->azbar + HDA_GCTL) & CRST) && --tmout)
	oss_udelay (1000);

      oss_udelay (1000);

      /*bring the controller out of reset  by writing a 1*/
      tmp = PCI_READL (devc->osdev, devc->azbar + HDA_GCTL);
      tmp |= CRST;
      PCI_WRITEL (devc->osdev, devc->azbar + HDA_GCTL, tmp);

      /*wait until the controller writes a 1 to indicate it is ready is or until 50ms have passed*/
      tmout = 50;
      while (!(PCI_READL (devc->osdev, devc->azbar + HDA_GCTL) & CRST) && --tmout)
	oss_udelay (1000);

      /* Give the link/codec extra time to settle and assert its
       * presence bit -- 1ms is enough on a clean boot, but not always
       * enough after several resets in a row. */
      oss_udelay (20000);

      /*if the controller is not ready now, abort*/
      if (!(PCI_READL (devc->osdev, devc->azbar + HDA_GCTL)))
	{
	  cmn_err (CE_WARN, "Controller not ready\n");
	  return 0;
	}

      if (devc->codecmask)
	return 1;		/* Already known from a previous reset (resume) */

      devc->codecmask = PCI_READW (devc->osdev, devc->azbar + HDA_STATESTS);
      DDB (cmn_err (CE_CONT, "Codec mask %x (attempt %d)\n",
		    devc->codecmask, attempt + 1));

      if (devc->codecmask)
	{
	  if (attempt > 0)
	    cmn_err (CE_CONT,
		     "oss_hdaudio: link reset needed %d attempts before a "
		     "codec responded\n", attempt + 1);
	  return 1;
	}
    }

  cmn_err (CE_WARN,
	   "oss_hdaudio: no codec responded after %d link reset attempts\n",
	   CRST_RETRIES);
  return 1;			/* Let the caller's own codec probe report the details */
}

/*
 * first_time=1 (normal attach): allocate a fresh CORB/RIRB buffer.
 * first_time=0 (resume): the existing buffer (devc->corb/corb_phys) is
 * still valid RAM -- suspend doesn't free it -- so just reuse it instead
 * of leaking a new 4K allocation on every suspend/resume cycle.
 */
static int
setup_controller (hda_devc_t * devc, int first_time)
{
  unsigned int tmp, tmout;
  oss_native_word phaddr;

/*
 * Allocate the CORB and RIRB buffers.
 */
  tmp = PCI_READB (devc->osdev, devc->azbar + HDA_CORBSIZE) & 0x03;

  switch (tmp)
    {
    case 0:
      devc->corbsize = 2;
      break;
    case 1:
      devc->corbsize = 16;
      break;
    case 2:
      devc->corbsize = 256;
      break;
    default:
      cmn_err (CE_WARN, "Bad CORB size\n");
      return 0;
    }

  tmp = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBSIZE) & 0x03;

  switch (tmp)
    {
    case 0:
      devc->rirbsize = 2;
      break;
    case 1:
      devc->rirbsize = 16;
      break;
    case 2:
      devc->rirbsize = 256;
      break;
    default:
      cmn_err (CE_WARN, "Bad CORB size\n");
      return 0;
    }

  if (first_time)
    {
      if ((devc->corb =
	   CONTIG_MALLOC (devc->osdev, 4096, MEMLIMIT_32BITS, &phaddr, devc->corb_dma_handle)) == NULL)
	{
	  cmn_err (CE_WARN, "Out of memory (CORB)\n");
	  return 0;
	}

      devc->corb_phys = phaddr;

      devc->rirb = (rirb_entry_t *) (devc->corb + 512);	/* 512 dwords = 2048 bytes */
      devc->rirb_phys = devc->corb_phys + 2048;
    }

/*
 * Initialize CORB registers
 */

  PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBCTL, 0);	/* Stop */

  tmout = 0;

  while (tmout++ < 500
	 && (PCI_READB (devc->osdev, devc->azbar + HDA_CORBCTL) & 0x02));

  if (PCI_READB (devc->osdev, devc->azbar + HDA_CORBCTL) & 0x02)
    {
      cmn_err (CE_WARN, "CORB didn't stop\n");
    }

  tmp = devc->corb_phys & 0xffffffff;
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_CORBLBASE, tmp);
  tmp = (devc->corb_phys >> 32) & 0xffffffff;
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_CORBUBASE, tmp);

  PCI_WRITEW (devc->osdev, devc->azbar + HDA_CORBWP, 0x0);	/* Reset to 0 */
  PCI_WRITEW (devc->osdev, devc->azbar + HDA_CORBRP, 0x8000);	/* Reset to 0 */
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBCTL, 0x02);	/* Start */

/*
 * Initialize RIRB registers
 */
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0);	/* Stop */

  tmout = 0;

  while (tmout++ < 500
	 && (PCI_READB (devc->osdev, devc->azbar + HDA_RIRBCTL) & 0x02));

  if (PCI_READB (devc->osdev, devc->azbar + HDA_RIRBCTL) & 0x02)
    {
      cmn_err (CE_WARN, "RIRB didn't stop\n");
    }

  tmp = devc->rirb_phys & 0xffffffff;
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_RIRBLBASE, tmp);
  tmp = (devc->rirb_phys >> 32) & 0xffffffff;
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_RIRBUBASE, tmp);

  /*
   * Clear any stale entries left in the buffers by a previous driver
   * (the DMA pages may be reused) and make sure the write/read pointers
   * are really reset before starting.
   */
  memset (devc->corb, 0, 4096);
  memset (devc->rirb, 0, 2048);

  PCI_WRITEW (devc->osdev, devc->azbar + HDA_RIRBWP, 0x8000);	/* Reset to 0 */
  PCI_WRITEW (devc->osdev, devc->azbar + HDA_RINTCNT, 1);	/* reset hw write ptr */
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0x03);	/* Start & Intr enable */

  /* Sync the software read pointer with the hardware write pointer */
  devc->rirb_rp = PCI_READB (devc->osdev, devc->azbar + HDA_RIRBWP) & 0x00ff;

#if 0
  cmn_err (CE_NOTE,
	   "oss_hdaudio: setup_controller: corb=%08x%08x rirb=%08x%08x corbwp=%02x corbrp=%02x rirbwp=%02x rirbsts=%02x\n",
	   (unsigned int) (devc->corb_phys >> 32), (unsigned int) devc->corb_phys,
	   (unsigned int) (devc->rirb_phys >> 32), (unsigned int) devc->rirb_phys,
	   PCI_READB (devc->osdev, devc->azbar + HDA_CORBWP),
	   PCI_READB (devc->osdev, devc->azbar + HDA_CORBRP),
	   PCI_READB (devc->osdev, devc->azbar + HDA_RIRBWP),
	   PCI_READB (devc->osdev, devc->azbar + HDA_RIRBSTS));
#endif

  return 1;
}

static int
setup_engines (hda_devc_t * devc)
{
  int i, p;
  unsigned int gcap;
  unsigned int num_inputs, num_outputs;
  oss_native_word phaddr;
  gcap = PCI_READW (devc->osdev, devc->azbar + HDA_GCAP);

  num_outputs = (gcap >> 12) & 0x0f;
  num_inputs = (gcap >> 8) & 0x0f;

  /* Init output engines */
  p = num_inputs;		/* Output engines are located after the input ones */

  if (num_outputs > HDA_MAX_ENGINES)
    {
      cmn_err (CE_WARN,
	       "Using only %d out of %d available output engines\n",
	       HDA_MAX_ENGINES, num_outputs);
      num_outputs = HDA_MAX_ENGINES;
    }

  if (num_inputs > HDA_MAX_ENGINES)
    {
      cmn_err (CE_WARN,
	       "Using only %d out of %d available input engines\n",
	       HDA_MAX_ENGINES, num_inputs);
      num_inputs = HDA_MAX_ENGINES;
    }

  for (i = 0; i < num_outputs; i++)
    {
      hda_engine_t *engine = &devc->outengines[i];

      engine->bdl =
	CONTIG_MALLOC (devc->osdev, 4096, MEMLIMIT_32BITS, &phaddr, engine->bdl_dma_handle);
      if (engine->bdl == NULL)
	{
	  cmn_err (CE_WARN, "Out of memory\n");
	  return 0;
	}
      engine->bdl_max = 4096 / sizeof (bdl_t);
      engine->bdl_phys = phaddr;
      engine->base = devc->azbar + 0x80 + (p * 0x20);
      engine->num = i;
      engine->intrmask = (1 << p);
      engine->portc = NULL;
      p++;
      devc->num_outengines = i + 1;
    }

  /* Init input engines */
  p = 0;
  for (i = 0; i < num_inputs; i++)
    {
      hda_engine_t *engine = &devc->inengines[i];

      engine->bdl =
	CONTIG_MALLOC (devc->osdev, 4096, MEMLIMIT_32BITS, &phaddr, engine->bdl_dma_handle);
      if (engine->bdl == NULL)
	{
	  cmn_err (CE_WARN, "Out of memory\n");
	  return 0;
	}
      engine->bdl_max = 4096 / sizeof (bdl_t);
      engine->bdl_phys = phaddr;
      engine->base = devc->azbar + 0x80 + (p * 0x20);
      engine->num = i;
      engine->intrmask = (1 << p);
      engine->portc = NULL;
      p++;
      devc->num_inengines = i + 1;
    }
  return 1;
}

/*ARGSUSED*/
static void
init_adev_caps (hda_devc_t * devc, adev_p adev, hdaudio_endpointinfo_t * ep)
{
  int i;

  adev->oformat_mask = ep->fmt_mask;
  adev->iformat_mask = ep->fmt_mask;

  adev->min_rate = 500000;
  adev->max_rate = 0;

  adev->nrates = ep->nrates;
  if (adev->nrates > OSS_MAX_SAMPLE_RATES)
  {
     cmn_err(CE_NOTE, "Too many supported sample rates (%d)\n", adev->nrates);
     adev->nrates = OSS_MAX_SAMPLE_RATES;
  }

  for (i = 0; i < adev->nrates; i++)
    {
      int r = ep->rates[i];

      adev->rates[i] = r;

      if (adev->min_rate > r)
	adev->min_rate = r;
      if (adev->max_rate < r)
	adev->max_rate = r;
    }
}

/*
 * S/PDIF lowlevel driver
 */
/*ARGSUSED*/
static int
hdaudio_reprogram_spdif (void *_devc, void *_portc,
			 oss_digital_control * ctl, unsigned int mask)
{
  unsigned short cbits = 0;
  hda_devc_t *devc = _devc;
  oss_native_word flags;
  hdaudio_endpointinfo_t *endpoint = devc->spdifout_endpoint;

  if (endpoint == NULL)
    return OSS_EIO;

  MUTEX_ENTER_IRQDISABLE (devc->low_mutex, flags);

  cbits = 0x01;			/* Digital interface enabled */

  cbits |= (1 << 2);		/* VCFG */

  if (ctl->out_vbit == VBIT_ON)
    cbits |= (1 << 1);		/* Turn on the V bit */

  if (ctl->cbitout[0] & 0x02)	/* Audio/Data */
    cbits |= (1 << 5);		/* /AUDIO */

  if (ctl->cbitout[0] & 0x01)	/* Pro */
    cbits |= (1 << 6);
  else
    {

      if (ctl->cbitout[0] & 0x04)	/* Copyright */
	cbits |= (1 << 4);	/* COPY */

      if (ctl->cbitout[1] & 0x80)	/* Generation level */
	cbits |= (1 << 7);	/* L */

      if (ctl->emphasis_type & 1)	/* Pre-emphasis */
	cbits |= (1 << 3);	/* PRE */
    }
  do_corb_write (devc, endpoint->cad, endpoint->base_wid, 0,
		 SET_SPDIF_CONTROL1, cbits);

  cbits = ctl->cbitout[1] & 0x7f;	/* Category code */
  do_corb_write (devc, endpoint->cad, endpoint->base_wid, 0,
		 SET_SPDIF_CONTROL2, cbits);

  MUTEX_EXIT_IRQRESTORE (devc->low_mutex, flags);
  return 0;
}

spdif_driver_t hdaudio_spdif_driver = {
/* reprogram_device: */ hdaudio_reprogram_spdif,
};

static int
spdif_mixer_init (int dev)
{
  hda_devc_t *devc = mixer_devs[dev]->hw_devc;
  int err;

  if ((err = oss_spdif_mix_init (&devc->spdc)) < 0)
    return err;

  return 0;
}

static void
install_outputdevs (hda_devc_t * devc)
{
  int i, n, pass, audio_dev, output_num=0;
  char tmp_name[64];
  hdaudio_endpointinfo_t *endpoints;

  if ((n =
       hdaudio_mixer_get_outendpoints (devc->mixer, &endpoints,
				       sizeof (*endpoints))) < 0)
    return;

#if 0
  // This check will be done later.
  if (n > MAX_OUTPUTS)
    {
      cmn_err (CE_WARN,
	       "Only %d out of %d output devices can be installed\n",
	       MAX_OUTPUTS, n);
      n = MAX_OUTPUTS;
    }
#endif

/*
 * Install the output devices in two passes. First install the analog
 * endpoints and then the digital one(s).
 */
  for (pass = 0; pass < 3; pass++)
    for (i = 0; i < n; i++)
      {
	adev_p adev;
	hda_portc_t *portc = &devc->output_portc[i];
	unsigned int formats = AFMT_S16_LE;
	int opts = ADEV_AUTOMODE | ADEV_NOINPUT;
	char *devfile_name = "";
	char devname[16];

/* Skip endpoints that are not physically connected on the motherboard. */
	if (endpoints[i].skip)
	  continue;

	if (output_num >= MAX_OUTPUTS)
	{
		cmn_err(CE_CONT, "Too many output endpoints. Endpoint %d ignored.\n", i);
		continue;
	}

	switch (pass)
	  {
	  case 0:		/* Pick analog and non-modem ones */
	    if (endpoints[i].is_digital || endpoints[i].is_modem)
	      continue;
	    break;

	  case 1:		/* Pick digital one(s) */
	    if (!endpoints[i].is_digital)
	      continue;
            break;

          case 2:               /* Pick modem one(s) */
            if (!endpoints[i].is_modem)
              continue;
	  }

	if (endpoints[i].is_digital)
	  {
	    opts |= ADEV_SPECIAL;
	    sprintf (devname, "spdout%d", devc->num_spdout++);
	    devfile_name = devname;
	  }
        if (endpoints[i].is_modem)
          {
            opts |= ADEV_SPECIAL;
            sprintf (devname, "mdmout%d", devc->num_mdmout++);
            devfile_name = devname;
          }

	// sprintf (tmp_name, "%s %s", devc->chip_name, endpoints[i].name);
	sprintf (tmp_name, "HD Audio play %s", endpoints[i].name);

	if ((audio_dev = oss_install_audiodev_with_devname (OSS_AUDIO_DRIVER_VERSION,
					       devc->osdev,
					       devc->osdev,
					       tmp_name,
					       &hda_audio_driver,
					       sizeof (audiodrv_t),
					       opts, formats, devc, -1,
					       devfile_name)) < 0)
	  {
	    return;
	  }

	if (output_num == 0)
	  devc->first_dev = audio_dev;

	adev = audio_engines[audio_dev];
	devc->num_outputs = i+1;

	adev->devc = devc;
	adev->portc = portc;
	adev->rate_source = devc->first_dev;
	adev->mixer_dev = devc->mixer_dev;
	adev->min_rate = 8000;
	adev->max_rate = 192000;
	adev->min_channels = 2;

	if (output_num == 0)
	  adev->max_channels = 8;
	else
	  adev->max_channels = 2;

        if (endpoints[i].is_modem)
          {
            adev->min_channels = 1;
            adev->max_channels = 1;
            adev->caps |= PCM_CAP_MODEM;
          }

	if (adev->max_channels > 4)
	  adev->dmabuf_alloc_flags |= DMABUF_LARGE | DMABUF_QUIET;
	adev->min_block = 128;	/* Hardware limitation */
	adev->min_fragments = 4;	/* Vmix doesn't work without this */
	portc->num = output_num;
	portc->open_mode = 0;
	portc->audio_enabled = 0;
	portc->audiodev = audio_dev;
	portc->port_type = PT_OUTPUT;
	portc->endpoint = &endpoints[i];
	init_adev_caps (devc, adev, portc->endpoint);
	portc->engine = NULL;

	if (portc->endpoint->is_digital)
	  {
	    int err;

	    devc->spdifout_endpoint = portc->endpoint;

/*
 * To be precise the right place for the spdc field might be portc instead of
 * devc. In this way it's possible to have multiple S/PDIF output DACs connected
 * to the same HDA controller. OTOH having it in devc saves space. Multiple 
 * oss_spdif_install() calls on the same spdc structure doesn't cause any 
 * problems.
 *
 * If spdc is moved to portc then care must be taken that oss_spdif_uninstall()
 * is called for all all output_portc instances.
 */
	    if ((err = oss_spdif_install (&devc->spdc, devc->osdev,
					  &hdaudio_spdif_driver,
					  sizeof (spdif_driver_t), devc, NULL,
					  devc->mixer_dev, SPDF_OUT,
					  DIG_PASSTHROUGH | DIG_EXACT |
					  DIG_CBITOUT_LIMITED | DIG_VBITOUT |
					  DIG_PRO | DIG_CONSUMER)) != 0)
	      {
		cmn_err (CE_WARN,
			 "S/PDIF driver install failed. Error %d\n", err);
	      }
	    else
	      {
		hdaudio_mixer_set_initfunc (devc->mixer, spdif_mixer_init);
	      }
	  }
	  output_num++;
      }
}

static void
install_inputdevs (hda_devc_t * devc)
{
  int i, n, audio_dev;
  char tmp_name[64];
  hdaudio_endpointinfo_t *endpoints;
  char devname[16], *devfile_name = "";

  if ((n =
       hdaudio_mixer_get_inendpoints (devc->mixer, &endpoints,
				      sizeof (*endpoints))) < 0)
    return;

  if (n > MAX_INPUTS)
    {
      cmn_err (CE_WARN,
	       "Only %d out of %d input devices can be installed\n",
	       MAX_INPUTS, n);
      n = MAX_INPUTS;
    }

  for (i = 0; i < n; i++)
    {
      adev_p adev;
      hda_portc_t *portc = &devc->input_portc[i];
      unsigned int formats = AFMT_S16_LE;
      int opts = ADEV_AUTOMODE | ADEV_NOOUTPUT;

/* Skip endpoints that are not physically connected on the motherboard. */
      if (endpoints[i].skip)
	continue;

      if (endpoints[i].is_digital)
	{
	  opts |= ADEV_SPECIAL;
	  sprintf (devname, "spdin%d", devc->num_spdin++);
	  devfile_name = devname;
	}
      if (endpoints[i].is_modem)
        {
          opts |= ADEV_SPECIAL;
          sprintf (devname, "mdmin%d", devc->num_mdmin++);
          devfile_name = devname;
        }

      //sprintf (tmp_name, "%s %s", devc->chip_name, endpoints[i].name);
      sprintf (tmp_name, "HD Audio rec %s", endpoints[i].name);

      if ((audio_dev = oss_install_audiodev_with_devname (OSS_AUDIO_DRIVER_VERSION,
					     devc->osdev,
					     devc->osdev,
					     tmp_name,
					     &hda_audio_driver,
					     sizeof (audiodrv_t),
					     opts, formats, devc, -1,
					     devfile_name)) < 0)
	{
	  return;
	}

      if (i == 0 && devc->first_dev == -1)
	devc->first_dev = audio_dev;

      adev = audio_engines[audio_dev];
      devc->num_inputs = i+1;

      adev->devc = devc;
      adev->portc = portc;
      adev->rate_source = devc->first_dev;
      adev->mixer_dev = devc->mixer_dev;
      adev->min_rate = 8000;
      adev->max_rate = 192000;
      adev->min_channels = 2;
      adev->max_channels = 2;

      if (endpoints[i].is_modem)
        {
          adev->min_channels = 1;
          adev->max_channels = 1;
          adev->caps |= PCM_CAP_MODEM;
        }

      adev->min_block = 128;	/* Hardware limitation */
      adev->min_fragments = 4;	/* Vmix doesn't work without this */
      portc->num = i;
      portc->open_mode = 0;
      portc->audio_enabled = 0;
      portc->audiodev = audio_dev;
      portc->port_type = PT_INPUT;
      portc->endpoint = &endpoints[i];
      portc->engine = NULL;
      init_adev_caps (devc, adev, portc->endpoint);
    }
}

static void
activate_vmix (hda_devc_t * devc)
{
#ifdef CONFIG_OSS_VMIX
/*
 * Attach vmix engines to all outputs and inputs.
 */

	if (devc->num_outputs > 0)
	   {
		   if (devc->num_inputs < 1)
		      vmix_attach_audiodev(devc->osdev, devc->output_portc[0].audiodev, -1, 0);
		   else
  		      vmix_attach_audiodev(devc->osdev, devc->output_portc[0].audiodev, devc->input_portc[0].audiodev, 0);
	   };
#endif
}

/* Defined further down (see the big i915 comment above hda_i915_init());
 * forward-declared here since init_HDA() needs it around the reset
 * below, before the rest of the i915 binding code is textually
 * defined. */
static void hda_i915_codec_wake_override (int enable);

static int
init_HDA (hda_devc_t * devc)
{
  unsigned int gcap;
  unsigned int tmp;

  /* Reset controller */

  /* On Intel platforms with a display-linked HDA controller, merely
   * holding the power well (hda_i915_power_up(), already done by our
   * caller) isn't enough to make the codec actually answer CORB/RIRB
   * across this reset -- confirmed against ALSA's own controller
   * driver, which wraps its equivalent of everything below with
   * exactly this override. A no-op if i915 binding never happened. */
  hda_i915_codec_wake_override (1);
  hda_intel_cgctl_set (devc, 0);

  if (!reset_controller (devc))
    {
      hda_intel_cgctl_set (devc, 1);
      hda_i915_codec_wake_override (0);
      return 0;
    }

  PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTCTL, PCI_READL (devc->osdev, devc->azbar + HDA_INTCTL) | 0xc0000000);	/* Intr enable */

  /*
   * Set CORB and RIRB sizes to 256, 16 or 2 entries. 
   *
   */
  tmp = (PCI_READB (devc->osdev, devc->azbar + HDA_CORBSIZE) >> 4) & 0x07;
  if (tmp & 0x4)		/* 256 entries */
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBSIZE, 0x2);
  else if (tmp & 0x2)		/* 16 entries */
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBSIZE, 0x1);
  else				/* Assume that 2 entries is supported */
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBSIZE, 0x0);

  tmp = (PCI_READB (devc->osdev, devc->azbar + HDA_RIRBSIZE) >> 4) & 0x07;
  if (tmp & 0x4)		/* 256 entries */
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSIZE, 0x2);
  else if (tmp & 0x2)		/* 16 entries */
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSIZE, 0x1);
  else				/* Assume that 2 entries is supported */
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSIZE, 0x0);

  /* setup the CORB/RIRB structs */
  if (!setup_controller (devc, 1))
    {
      hda_intel_cgctl_set (devc, 1);
      hda_i915_codec_wake_override (0);
      return 0;
    }

  /* Matches ALSA's own scope for both of these: just the controller
   * reset above, not the codec probing below -- once woken, the codec
   * keeps responding normally without the override forcing it. */
  hda_intel_cgctl_set (devc, 1);
  hda_i915_codec_wake_override (0);

  /* setup the engine structs */
  if (!setup_engines (devc))
    return 0;

  if (!devc->codecmask)
    {
      cmn_err (CE_WARN, "No codecs found after reset\n");
      return 0;
    }
  else
    devc->mixer = hdaudio_mixer_create (devc->chip_name, devc, devc->osdev,
					do_corb_write, corb_read,
					devc->codecmask, devc->vendor_id,
					devc->subvendor_id);
  if (devc->mixer == NULL)
    return 0;

  devc->mixer_dev = hdaudio_mixer_get_mixdev (devc->mixer);

  gcap = PCI_READW (devc->osdev, devc->azbar + HDA_GCAP);
  DDB (cmn_err (CE_CONT, " GCAP register content 0x%x\n", gcap));

  if (((gcap >> 3) & 0x0f) > 0)
    cmn_err (CE_WARN, "Bidirectional engines not supported\n");
  if (((gcap >> 1) & 0x03) > 0)
    cmn_err (CE_WARN, "Multiple SDOs not supported\n");

  install_outputdevs (devc);
  install_inputdevs (devc);
  activate_vmix (devc);

  return 1;
}

/*
 * On Intel platforms with an integrated GPU (Skylake and later -- this
 * covers essentially every current Intel laptop/desktop with an i915
 * display driver), the HD Audio controller's link shares a power well
 * with the display. That well is only kept on if something explicitly
 * asks i915 for it through the kernel's generic component framework;
 * nothing else keeps it powered on our behalf. Without this, the
 * codec silently stops responding to CORB/RIRB the moment i915
 * decides the well isn't needed for anything else -- which in
 * practice means the controller works once, right after a fresh boot
 * (BIOS/firmware happens to leave the well on that long), and then
 * never again after the first module reload or an actual
 * suspend/resume, no matter how many times or how carefully the HDA
 * link itself (GCTL/CORB/RIRB) is reset -- the reset succeeds, but
 * there's simply no power reaching the far side of it.
 *
 * i915 registers *itself* as a plain component (component_add_typed(),
 * type I915_COMPONENT_AUDIO -- see i915_audio_component_register() in
 * i915's own intel_audio.c). The HD-audio side is supposed to be the
 * *master*: component_master_add_with_match(), with a match function
 * that looks for a PCI device whose driver is named "i915" (or "xe")
 * offering that same component type -- see i915_component_master_match()
 * in ALSA's sound/hda/hdac_i915.c, which this mirrors (self-contained,
 * deliberately not linked against any part of ALSA). Once matched,
 * component_bind_all() from our own master .bind callback is what
 * actually invokes i915's component .bind, handing back a filled-in
 * struct i915_audio_component with real get_power()/put_power() ops.
 *
 * (An earlier version of this got the master/component roles backwards
 * -- registering oss_hdaudio as a second I915_COMPONENT_AUDIO component
 * instead of as the master -- which silently never bound, since nothing
 * was ever waiting to match two same-typed components against each
 * other. Confirmed against the actual kernel source, not guessed.)
 *
 * i915_component.h moved from <drm/i915_component.h> to
 * <drm/intel/i915_component.h> in Linux 6.11; older kernels needing
 * this will want the old path instead.
 */
#ifdef __linux__
#include "ossdip.h"	/* struct _dev_info_t's real fields (dip->dev) */

/*
 * Just enough to recognize a PCI device (dev_is_pci()'s own check,
 * done by hand) without pulling in the whole of linux/pci.h here --
 * that collides with this codebase's own PCI compat layer (wrap.h/
 * os.h), which needs to control that include itself; every other PCI
 * access in this file already goes through its own PCI-register and
 * pci-config-space wrapper calls instead of raw kernel PCI calls, for
 * the same reason.
 */
extern struct bus_type pci_bus_type;

static struct i915_audio_component hda_i915_acomp;
static unsigned long hda_i915_power_ref;
static int hda_i915_master_added;

static int
hda_i915_match (struct device *dev, int subcomponent, void *data)
{
  if (dev->bus != &pci_bus_type || dev->driver == NULL)
    return 0;

  if (strcmp (dev->driver->name, "i915") != 0 &&
      strcmp (dev->driver->name, "xe") != 0)
    return 0;

  return subcomponent == I915_COMPONENT_AUDIO;
}

static int
hda_i915_master_bind (struct device *dev)
{
  int ret;

  ret = component_bind_all (dev, &hda_i915_acomp);
  if (ret < 0)
    return ret;

  if (hda_i915_acomp.base.dev == NULL || hda_i915_acomp.base.ops == NULL)
    {
      component_unbind_all (dev, &hda_i915_acomp);
      return -ENODEV;
    }

  return 0;
}

static void
hda_i915_master_unbind (struct device *dev)
{
  component_unbind_all (dev, &hda_i915_acomp);
}

static const struct component_master_ops hda_i915_master_ops = {
  .bind = hda_i915_master_bind,
  .unbind = hda_i915_master_unbind,
};

static void
hda_i915_init (oss_device_t * osdev)
{
  struct component_match *match = NULL;

  memset (&hda_i915_acomp, 0, sizeof (hda_i915_acomp));
  hda_i915_master_added = 0;

  if (osdev->dip == NULL || osdev->dip->dev == NULL)
    return;

  /* No integrated GPU, or an i915/xe build too old/new to match this
   * way: component_master_add_with_match() just finds no candidate
   * and we carry on without ever holding a power reference, same as
   * before this existed -- get_power()/put_power() below are both
   * no-ops then. */
  component_match_add_typed (osdev->dip->dev, &match, hda_i915_match, NULL);
  hda_i915_master_added =
    (component_master_add_with_match (osdev->dip->dev, &hda_i915_master_ops,
				       match) == 0);
}

static void
hda_i915_exit (oss_device_t * osdev)
{
  if (!hda_i915_master_added)
    return;

  component_master_del (osdev->dip->dev, &hda_i915_master_ops);
  hda_i915_master_added = 0;
  memset (&hda_i915_acomp, 0, sizeof (hda_i915_acomp));
}

/*
 * component_master_add_with_match() above matches synchronously if
 * i915's own component is already registered (i915 loads well before
 * any HDA driver in every ordinary boot), so in practice
 * hda_i915_acomp.base.ops is already usable by the time this returns
 * -- no deferring needed. This stays as a safety net for the
 * unusual case where i915 (or xe) hasn't registered its component
 * yet: ask the driver core to retry our whole probe() shortly rather
 * than press on without ever holding the power well.
 *
 * Bounded (HDA_I915_MAX_DEFERS attempts): on hardware with no
 * integrated GPU at all, no match will ever appear, so this must not
 * defer indefinitely -- after the cap, attach proceeds without ever
 * holding a power reference, exactly like before any of this existed.
 */
#define HDA_I915_MAX_DEFERS	10
static int hda_i915_defer_count;

static int
hda_i915_should_defer (void)
{
  if (hda_i915_acomp.base.ops != NULL)
    return 0;			/* Already bound -- nothing to wait for */

  if (hda_i915_defer_count >= HDA_I915_MAX_DEFERS)
    return 0;			/* Given it a fair chance; move on */

  hda_i915_defer_count++;
  return 1;
}

static void
hda_i915_power_up (void)
{
  if (hda_i915_acomp.base.ops != NULL &&
      hda_i915_acomp.base.ops->get_power != NULL)
    hda_i915_power_ref =
      hda_i915_acomp.base.ops->get_power (hda_i915_acomp.base.dev);
}

static void
hda_i915_power_down (void)
{
  if (hda_i915_acomp.base.ops != NULL &&
      hda_i915_acomp.base.ops->put_power != NULL)
    hda_i915_acomp.base.ops->put_power (hda_i915_acomp.base.dev,
					 hda_i915_power_ref);
}

/*
 * Requesting the power well (get_power/put_power above) is not, on
 * its own, enough to make the codec answer CORB/RIRB during a link
 * reset -- confirmed against ALSA's actual controller driver
 * (sound/pci/hda/hda_intel.c, hda_intel_init_chip()): it wraps the
 * *entire* controller reset with this separate codec_wake_override
 * on/off pair, which is what actually makes the codec listen for and
 * respond to the reset. Without it, the well can be genuinely on and
 * the codec will still look "not physically present" to a GET_PARAMETER
 * probe right after reset -- which is exactly what got this whole
 * i915 investigation started in the first place. See its use around
 * reset_controller()/setup_controller() in init_HDA().
 */
static void
hda_i915_codec_wake_override (int enable)
{
  if (hda_i915_acomp.base.ops != NULL &&
      hda_i915_acomp.base.ops->codec_wake_override != NULL)
    hda_i915_acomp.base.ops->codec_wake_override (hda_i915_acomp.base.dev,
						   enable);
}
#else /* !__linux__: no such power-well concept on any other OSS target */
static void
hda_i915_init (oss_device_t * osdev)
{
}

static void
hda_i915_exit (oss_device_t * osdev)
{
}

static void
hda_i915_power_up (void)
{
}

static void
hda_i915_power_down (void)
{
}

static void
hda_i915_codec_wake_override (int enable)
{
}

static int
hda_i915_should_defer (void)
{
  return 0;
}
#endif

#ifndef EPROBE_DEFER
/* Matches Linux's <linux/errno.h>. Never actually returned on any
 * other OSS target, since hda_i915_should_defer() above always
 * returns 0 there -- only needed so the portable oss_hdaudio_attach()
 * below still compiles everywhere. */
#define EPROBE_DEFER 517
#endif

int
oss_hdaudio_attach (oss_device_t * osdev)
{
  unsigned char pci_irq_line, pci_revision, btmp;
  unsigned short pci_command, vendor, device, wtmp;
  unsigned short subvendor, subdevice;
  hda_devc_t *devc;
  static int already_attached = 0;
  int err;
  unsigned short devctl;	
	
  DDB (cmn_err (CE_CONT, "oss_hdaudio_attach entered\n"));

  if (already_attached)
    {
      cmn_err (CE_WARN, "oss_hdaudio_attach: Already attached\n");
      return 0;
    }

  /* Nothing allocated yet -- bail out cheaply and let the driver core
   * retry us later if the i915 power well isn't bound yet. See the
   * big comment above hda_i915_should_defer(). */
  hda_i915_init (osdev);
  if (hda_i915_should_defer ())
    {
      hda_i915_exit (osdev);
      return -EPROBE_DEFER;
    }

  already_attached = 1;

  pci_read_config_word (osdev, PCI_VENDOR_ID, &vendor);
  pci_read_config_word (osdev, PCI_DEVICE_ID, &device);

  pci_read_config_byte (osdev, PCI_REVISION_ID, &pci_revision);
  pci_read_config_word (osdev, PCI_COMMAND, &pci_command);
  pci_read_config_irq (osdev, PCI_INTERRUPT_LINE, &pci_irq_line);
  pci_read_config_word (osdev, PCI_SUBSYSTEM_VENDOR_ID, &subvendor);
  pci_read_config_word (osdev, PCI_SUBSYSTEM_ID, &subdevice);

  if ((devc = PMALLOC (osdev, sizeof (*devc))) == NULL)
    {
      cmn_err (CE_WARN, "Out of memory\n");
      return 0;
    }

  devc->osdev = osdev;
  osdev->devc = devc;
  devc->first_dev = -1;

  osdev->hw_info = PMALLOC (osdev, HWINFO_SIZE);	/* Text buffer for additional device info */
  memset (osdev->hw_info, 0, HWINFO_SIZE);

  devc->vendor_id = (vendor << 16) | device;
  devc->subvendor_id = (subvendor << 16) | subdevice;

  oss_pci_byteswap (osdev, 1);

  switch (device)
    {
    case INTEL_DEVICE_CPT:
    case INTEL_DEVICE_PBG:
    case INTEL_DEVICE_PPT:
    case INTEL_DEVICE_SCH:
  	  pci_read_config_word (osdev, 0x78, &devctl);
 	  DDB (cmn_err (CE_CONT, " DEVC register content  0x%04x\n", devctl);)
  	  pci_write_config_word (osdev, 0x78, (devctl & (~0x0800)) );
  	  DDB (pci_read_config_word (osdev, 0x78, &devctl);)
 	  DDB (cmn_err (CE_CONT, " DEVC register content (after clearing DEVC.NSNPEN)  0x%04x\n", devctl);)
	  /* continue is intentional */
    case INTEL_DEVICE_ICH6:
    case INTEL_DEVICE_ICH7:
    case INTEL_DEVICE_ESB2:
    case INTEL_DEVICE_ICH8:
    case INTEL_DEVICE_ICH9:
    case INTEL_DEVICE_ICH9_B:
    case INTEL_DEVICE_ICH10:
    case INTEL_DEVICE_ICH10_B:
    case INTEL_DEVICE_PCH:
    case INTEL_DEVICE_PCH_B:
    case INTEL_DEVICE_PCH_C:
    case INTEL_DEVICE_HSW:
    case INTEL_DEVICE_BDW:
    case INTEL_DEVICE_BYT:
    case INTEL_DEVICE_BSW:
    case INTEL_DEVICE_SKL_LP:
    case INTEL_DEVICE_SPT_LP:
    case INTEL_DEVICE_CNL_LP:
    case INTEL_DEVICE_WPT_LP:
    case INTEL_DEVICE_LPT_LP_0:
    case INTEL_DEVICE_LPT_LP_1:
    case INTEL_DEVICE_9_SERIES:
    case INTEL_DEVICE_SKL:
    case INTEL_DEVICE_KBL:
    case INTEL_DEVICE_KBL_H:
    case INTEL_DEVICE_CNL_H:
    case INTEL_DEVICE_CML_S:
    case INTEL_DEVICE_CML_LP:
    case INTEL_DEVICE_CML_H:
    case INTEL_DEVICE_ICL_LP:
    case INTEL_DEVICE_ICL_N:
    case INTEL_DEVICE_ICL_H:
    case INTEL_DEVICE_TGL_LP:
    case INTEL_DEVICE_TGL_H:
    case INTEL_DEVICE_ADL_P:
    case INTEL_DEVICE_ADL_PX:
    case INTEL_DEVICE_ADL_M:
    case INTEL_DEVICE_ADL_N:
    case INTEL_DEVICE_ADL_S:
    case INTEL_DEVICE_RPL_S:
    case INTEL_DEVICE_APL:
    case INTEL_DEVICE_GML:
      devc->chip_name = "Intel HD Audio";
      break;

    case NVIDIA_DEVICE_MCP51:
    case NVIDIA_DEVICE_MCP55:
    case NVIDIA_DEVICE_MCP61:
    case NVIDIA_DEVICE_MCP61A:
    case NVIDIA_DEVICE_MCP65:
    case NVIDIA_DEVICE_MCP67:
    case NVIDIA_DEVICE_MCP73:
    case NVIDIA_DEVICE_MCP78S:
    case NVIDIA_DEVICE_MCP79:
      devc->chip_name = "nVidia HD Audio";
      pci_read_config_byte (osdev, 0x4e, &btmp);
      pci_write_config_byte (osdev, 0x4e, (btmp & 0xf0) | 0x0f);
      pci_read_config_byte (osdev, 0x4d, &btmp);
      pci_write_config_byte (osdev, 0x4d, (btmp & 0xfe) | 0x01);
      pci_read_config_byte (osdev, 0x4c, &btmp);
      pci_write_config_byte (osdev, 0x4c, (btmp & 0xfe) | 0x01);
      break;

    case ATI_DEVICE_SB450:
    case ATI_DEVICE_SB600:
      devc->chip_name = "ATI HD Audio";
      pci_read_config_byte (osdev, 0x42, &btmp);
      pci_write_config_byte (osdev, 0x42, (btmp & 0xf8) | 0x2);
      break;

    case AMD_DEVICE_HUDSON:
	devc->chip_name = "AMD HD Audio";
	break;

    case VIA_DEVICE_HDA:
      devc->chip_name = "VIA HD Audio";
      break;

    case SIS_DEVICE_HDA:
      devc->chip_name = "SiS HD Audio";
      break;

    case ULI_DEVICE_HDA:
      devc->chip_name = "ULI HD Audio";
      pci_read_config_word (osdev, 0x40, &wtmp);
      pci_write_config_word (osdev, 0x40, wtmp | 0x10);
      pci_write_config_dword (osdev, PCI_MEM_BASE_ADDRESS_1, 0);
      break;

    case CREATIVE_XFI_HDA:
      devc->chip_name = "Creative Labs XFi XTreme Audio";
      break;

    default:
      devc->chip_name = "Azalia High Definition audio device";
    }

  pci_read_config_dword (osdev, PCI_MEM_BASE_ADDRESS_0, &devc->membar_addr);

  devc->membar_addr &= ~7;

  /* get virtual address */
  devc->azbar =
    (void *) MAP_PCI_MEM (devc->osdev, 0, devc->membar_addr, 16 * 1024);

  /*verify interrupt*/
  if (pci_irq_line == 0)
    {
      cmn_err (CE_WARN, "IRQ not assigned by BIOS.\n");
      return 0;
    }

  devc->irq = pci_irq_line;
   
  /* activate the device */
  pci_command |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
  pci_write_config_word (osdev, PCI_COMMAND, pci_command);


  MUTEX_INIT (devc->osdev, devc->mutex, MH_DRV);
  MUTEX_INIT (devc->osdev, devc->low_mutex, MH_DRV + 1);

  oss_register_device (osdev, devc->chip_name);

  if ((err = oss_register_interrupts (devc->osdev, 0, hdaintr, NULL)) < 0)
    {
      cmn_err (CE_WARN, "Can't register interrupt handler, err=%d\n", err);

      MUTEX_CLEANUP (devc->mutex);
      MUTEX_CLEANUP (devc->low_mutex);

      if (devc->membar_addr != 0)
	{
	  UNMAP_PCI_MEM (devc->osdev, 0, devc->membar_addr, devc->azbar,
			 16 * 1024);
	  devc->membar_addr = 0;
	}

      oss_unregister_device (devc->osdev);
      already_attached = 0;
      return 0;
    }

  devc->base = devc->membar_addr;

  /* Setup the TCSEL register. Don't do this with ATI chipsets. */
  if (vendor != ATI_VENDOR_ID)
    {
      pci_read_config_byte (osdev, 0x44, &btmp);
      pci_write_config_byte (osdev, 0x44, btmp & 0xf8);
     }

  hda_i915_power_up ();
  oss_udelay (200000);		/* Let the power well actually settle */

  err = init_HDA (devc);
  if (err == 0)
    {
      int j;

      cmn_err (CE_NOTE, "oss_hdaudio: init_HDA failed, cleaning up\n");

      hda_i915_power_down ();
      hda_i915_exit (osdev);

      oss_unregister_interrupts (devc->osdev);

      if (devc->corb != NULL)
	CONTIG_FREE (devc->osdev, devc->corb, 4096, devc->corb_dma_handle);
      devc->corb = NULL;

      for (j = 0; j < devc->num_outengines; j++)
	{
	  hda_engine_t *engine = &devc->outengines[j];

	  if (engine->bdl == NULL)
	    continue;
	  CONTIG_FREE (devc->osdev, engine->bdl, 4096,
		       engine->bdl_dma_handle);
	}

      for (j = 0; j < devc->num_inengines; j++)
	{
	  hda_engine_t *engine = &devc->inengines[j];

	  if (engine->bdl == NULL)
	    continue;
	  CONTIG_FREE (devc->osdev, engine->bdl, 4096,
		       engine->bdl_dma_handle);
	}

      if (devc->membar_addr != 0)
	{
	  UNMAP_PCI_MEM (devc->osdev, 0, devc->membar_addr, devc->azbar,
			 16 * 1024);
	  devc->membar_addr = 0;
	}

      oss_unregister_device (devc->osdev);
      already_attached = 0;
    }
  return err;
}

int
oss_hdaudio_detach (oss_device_t * osdev)
{
  hda_devc_t *devc = (hda_devc_t *) osdev->devc;
  int j;

  if (oss_disable_device (osdev) < 0)
    return 0;

  PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTSTS, 0xc0000000);	/* ack pending ints */

  PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTCTL, 0);	/* Intr disable */
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_STATESTS, 0x7);	/* Intr disable */
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_RIRBSTS, 0x5);	/* Intr disable */
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0);	/* Stop */
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBCTL, 0);	/* Stop */

  oss_unregister_interrupts (devc->osdev);

  MUTEX_CLEANUP (devc->mutex);
  MUTEX_CLEANUP (devc->low_mutex);

  if (devc->corb != NULL)
    CONTIG_FREE (devc->osdev, devc->corb, 4096, devc->corb_dma_handle);

  devc->corb = NULL;

  for (j = 0; j < devc->num_outengines; j++)
    {
      hda_engine_t *engine = &devc->outengines[j];

      if (engine->bdl == NULL)
	continue;
      CONTIG_FREE (devc->osdev, engine->bdl, 4096, engine->bdl_dma_handle);
    }

  for (j = 0; j < devc->num_inengines; j++)
    {
      hda_engine_t *engine = &devc->inengines[j];

      if (engine->bdl == NULL)
	continue;
      CONTIG_FREE (devc->osdev, engine->bdl, 4096, engine->bdl_dma_handle);
    }

  if (devc->membar_addr != 0)
    {
      UNMAP_PCI_MEM (devc->osdev, 0, devc->membar_addr, devc->azbar,
		     16 * 1024);
      devc->membar_addr = 0;
    }

  oss_spdif_uninstall (&devc->spdc);

  hda_i915_power_down ();
  hda_i915_exit (osdev);

  oss_unregister_device (devc->osdev);
  return 1;
}

/*
 * Suspend/resume (see the hda_verb_cache_ent_t comment above for the
 * overall design). Neither function touches any device file, mixer
 * control or DMA buffer -- only hardware registers -- so nothing here
 * needs (or is allowed to assume) that every process with the device
 * open has been closed first, unlike oss_hdaudio_detach().
 */
int
oss_hdaudio_suspend (oss_device_t * osdev)
{
  hda_devc_t *devc = (hda_devc_t *) osdev->devc;

  if (devc == NULL || devc->azbar == NULL)
    return 1;

  /* Quiesce the controller so it doesn't churn out interrupts or DMA
   * while power goes away. Mirrors the same lines in
   * oss_hdaudio_detach(), minus everything that frees memory or
   * unregisters something -- resume picks the very same devc/engine/
   * mixer/DMA-buffer structures back up in place. */
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTSTS, 0xc0000000);	/* ack pending ints */
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTCTL, 0);	/* Intr disable */
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_STATESTS, 0x7);
  PCI_WRITEL (devc->osdev, devc->azbar + HDA_RIRBSTS, 0x5);
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBCTL, 0);	/* Stop */
  PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBCTL, 0);	/* Stop */

  /* Give up the shared display/audio power well -- see the big
   * comment above hda_i915_init(). Re-requested in oss_hdaudio_resume()
   * before anything below touches a register again. */
  hda_i915_power_down ();

  return 1;
}

/*
 * Re-arm one already-open, already-running engine after resume: same
 * BDL buffer and dmap as before suspend (neither was freed), just
 * reprogram the stream descriptor registers -- the hardware forgot
 * them across the sleep -- and re-trigger. No-op for a portc that
 * isn't actually attached/running.
 */
static void
hda_engine_resume_one (hda_devc_t * devc, hda_portc_t * portc, int direction)
{
  dmap_t *dmap;
  hda_engine_t *engine;

  if (portc == NULL || portc->engine == NULL)
    return;

  if (!(portc->audio_enabled & direction))
    return;			/* Wasn't actually running */

  if (audio_engines[portc->audiodev] == NULL)
    return;

  dmap = (direction == PCM_ENABLE_OUTPUT) ?
    audio_engines[portc->audiodev]->dmap_out :
    audio_engines[portc->audiodev]->dmap_in;
  if (dmap == NULL)
    return;

  engine = portc->engine;

  /* Force hda_audio_trigger() below to see this direction as
   * "not yet armed" so it actually reprograms the run bit. */
  portc->trigger_bits &= ~direction;

  init_bdl (devc, engine, dmap);
  if (setup_audio_engine (devc, engine, portc, dmap) < 0)
    {
      cmn_err (CE_WARN,
	       "oss_hdaudio: resume: failed to re-arm engine for dev %d\n",
	       portc->audiodev);
      return;
    }

  hda_audio_trigger (portc->audiodev, direction);
}

int
oss_hdaudio_resume (oss_device_t * osdev)
{
  hda_devc_t *devc = (hda_devc_t *) osdev->devc;
  unsigned short pci_command;
  unsigned int tmp;
  int cad, i;

  if (devc == NULL || devc->azbar == NULL)
    return 1;

  /* Re-request the shared display/audio power well *first* -- see the
   * big comment above hda_i915_init(). Everything below is pointless
   * if the link on the far side of it isn't actually powered. */
  hda_i915_power_up ();

  /* Bus mastering/memory decode: pci_restore_state() (run by the PCI
   * core before calling us) should already have brought this back,
   * but re-assert explicitly, same as oss_hdaudio_attach() does, so
   * this doesn't silently depend on that. */
  pci_read_config_word (osdev, PCI_COMMAND, &pci_command);
  pci_command |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
  pci_write_config_word (osdev, PCI_COMMAND, pci_command);

  /* Controller-level bring-up: identical sequence to init_HDA(), minus
   * everything that allocates memory or creates OS-visible objects
   * (setup_engines()'s buffers, the mixer's widget/control graph, the
   * audio/mixer device files) -- all of that is still valid from the
   * original attach and must not be recreated. Same
   * hda_i915_codec_wake_override() wrapping as init_HDA(); see the
   * comment there. */
  hda_i915_codec_wake_override (1);
  hda_intel_cgctl_set (devc, 0);

  if (!reset_controller (devc))
    {
      hda_intel_cgctl_set (devc, 1);
      hda_i915_codec_wake_override (0);
      cmn_err (CE_WARN, "oss_hdaudio: resume: controller reset failed\n");
      return 0;
    }

  PCI_WRITEL (devc->osdev, devc->azbar + HDA_INTCTL,
	      PCI_READL (devc->osdev, devc->azbar + HDA_INTCTL) | 0xc0000000);

  tmp = (PCI_READB (devc->osdev, devc->azbar + HDA_CORBSIZE) >> 4) & 0x07;
  if (tmp & 0x4)
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBSIZE, 0x2);
  else if (tmp & 0x2)
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBSIZE, 0x1);
  else
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_CORBSIZE, 0x0);

  tmp = (PCI_READB (devc->osdev, devc->azbar + HDA_RIRBSIZE) >> 4) & 0x07;
  if (tmp & 0x4)
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSIZE, 0x2);
  else if (tmp & 0x2)
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSIZE, 0x1);
  else
    PCI_WRITEB (devc->osdev, devc->azbar + HDA_RIRBSIZE, 0x0);

  if (!setup_controller (devc, 0))
    {
      hda_intel_cgctl_set (devc, 1);
      hda_i915_codec_wake_override (0);
      cmn_err (CE_WARN, "oss_hdaudio: resume: CORB/RIRB setup failed\n");
      return 0;
    }

  hda_intel_cgctl_set (devc, 1);
  hda_i915_codec_wake_override (0);

  /* Wake every codec that was attached before suspend -- the link
   * reset above put them all back in a power-on-default state, which
   * on most codecs means D3/not responding until explicitly told to
   * power up (same wake step attach_codec() falls back to when a
   * codec doesn't answer on the first read). */
  for (cad = 0; cad < MAX_CODECS; cad++)
    if (devc->codecmask & (1 << cad))
      do_corb_write (devc, cad, 0, 0, SET_POWER_STATE, 0);

  oss_udelay (10000);

  /* Replay every persistent verb (amp gains, pin controls, EAPD,
   * selectors, converter/stream-format assignments, ...) issued since
   * attach -- this is what actually puts every widget (including
   * every board-specific fixup in hdaudio_gpio_handlers.c) back the
   * way it was, without re-walking or recreating the widget/mixer
   * graph itself. */
  for (i = 0; i < devc->verb_cache_n; i++)
    do_corb_write (devc, devc->verb_cache[i].cad, devc->verb_cache[i].nid,
		   devc->verb_cache[i].direct, devc->verb_cache[i].verb,
		   devc->verb_cache[i].parm);

  /* Re-arm whatever was actually mid-stream at suspend time. */
  for (i = 0; i < devc->num_outputs; i++)
    hda_engine_resume_one (devc, &devc->output_portc[i], PCM_ENABLE_OUTPUT);
  for (i = 0; i < devc->num_inputs; i++)
    hda_engine_resume_one (devc, &devc->input_portc[i], PCM_ENABLE_INPUT);

  return 1;
}
