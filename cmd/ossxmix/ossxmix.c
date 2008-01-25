/*
 * Purpose: This is the ossxmix (GTK++ GUI) program shipped with OSS
 *
 * Description:
 * The {!xlink ossxmix} program is the primary mixer and control panel utility
 * available for OSS. It shows how the new mixer API of OSS can be
 * used in GUI type of programs See the "{!link mixer}" section of the
 * OSS Developer's manual for more info.
 *
 * This program is fully dynamic as required by the mixer interface. It doesn't
 * contain anything that is specific to certain device. All the mixer structure
 * information is loaded in the beginning of the program by using the
 * {!nlink SNDCTL_MIX_EXTINFO} ioctl (and the related calls).
 *
 * Note that this program was written before the final mixer API
 * was ready. For this reason handling of some special situations is missing
 * or incompletely implemented. For example handling of the
 * {!nlink EIDRM} is "emulated" simply by closing and re-execing the
 * program. This is bit iritating but works.
 *
 * What might be interesting in this program is how to create the GUI layout
 * based on the control tree obtained using the SNDCTL_MIX_EXTINFO routine.
 * However unfortunately this part of the program is not particularily easy
 * understand.
 *
 * {!notice Please read the mixer programming documentation very carefully
 * before studying this program.
 *
 * The {!nlink ossmix.c} program is a command line version of this one.
 *
 * The {!nlink mixext.c} program is a very simple program that shows how
 * "non-mixer" applications can do certain mixer changes.
 *
 * This program uses a "LED" bar widget contained in gtkvu.c.
 */
#define COPYING Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#ifdef __hpux
#define G_INLINE_FUNC
#endif
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <soundcard.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "gtkvu.h"

#undef  TEST_JOY

#ifdef TEST_JOY
#include "gtkjoy.h"
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#if 0
/*
 * Memory allocation debugging
 */
void *
ossxmix_malloc (size_t sz, char *f, int l)
{
  printf ("malloc(%d)\t%s:%d\n", sz, f, l);
  return malloc (sz);
}

#define malloc(sz) ossxmix_malloc(sz, __FILE__, __LINE__)
#endif

int ngroups = 0;
int set_counter = 0;
int mixerfd = -1;
int dev = -1;
int prev_update_counter = 0;
oss_mixext *extrec = NULL;
oss_mixext_root *root = NULL;
char *extnames[256] = { NULL };
GtkWidget *widgets[256] = { NULL };
int orient[256] = { 0 };
int nrext = 0;
int show_all = 1;
int fully_started = 0;
int use_layout_b = 0;

int width_adjust = 0;

int saved_argc;
char *saved_argv[10];

#define LEFT 	1
#define RIGHT	2
#define MONO	3
#define BOTH	4

#define PEAK_DECAY		 6
#define PEAK_POLL_INTERVAL	50
#define VALUE_POLL_INTERVAL	5000

#ifndef EIDRM
#define EIDRM EFAULT
#endif

typedef struct ctlrec
{
  struct ctlrec *next;
  oss_mixext *mixext;
  GtkObject *left, *right;
  GtkWidget *gang, *frame;
#define FRAME_NAME_LENGTH 8
  char frame_name[FRAME_NAME_LENGTH];
  int lastleft, lastright;
  int full_scale;
  int whattodo;
#define WHAT_LABEL	1
#define WHAT_UPDATE	2
  int parm;
}
ctlrec_t;

ctlrec_t *control_list = NULL;
ctlrec_t *peak_list = NULL;
ctlrec_t *value_poll_list = NULL;
ctlrec_t *check_list = NULL;

GtkWidget *window, *scrolledwin;

gint CloseRequest (GtkWidget * theWindow, gpointer data);

void
store_name (int n, char *name)
{
  int i;

  for (i = 0; i < strlen (name); i++)
    if (name[i] >= 'A' && name[i] <= 'Z')
      name[i] += 32;

  extnames[n] = malloc (strlen (name) + 1);
  strcpy (extnames[n], name);
/* fprintf(stderr, "Control = %s\n", name); */
}

char *
cut_name (char *name)
{
  char *s = name;
  while (*s)
    if (*s++ == '_')
      return s;

  if (name[0] == '@')
    return &name[1];

  return name;
}

char *
get_name (int n)
{
  char *p, *s;

#if 1
  s = p = extnames[n];
  while (*p)
    {
      if (*p == '.')
	s = p + 1;
      p++;
    }

  return s;
#else

  p = extnames[n];

  while (*p && *p != '.')
    p++;

  if (*p == '.')
    {
      p++;
      return p;
    }
  return extnames[n];
#endif
}

char *
showenum (char *extname, oss_mixext * rec, int val)
{
  char *p, *tmp;
  oss_mixer_enuminfo ei;

  tmp = malloc (100);
  *tmp = 0;

  if (*extname == '.')
    extname++;

  if (val > rec->maxvalue)
    {
      sprintf (tmp, "%d(too large (%d)?)", val, rec->maxvalue);
      return tmp;
    }

  ei.dev = rec->dev;
  ei.ctrl = rec->ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_ENUMINFO, &ei) != -1)
    {
      if (val >= ei.nvalues)
	{
	  sprintf (tmp, "%d(too large2 (%d)?)", val, ei.nvalues);
	  return tmp;
	}

      p = ei.strings + ei.strindex[val];
      strcpy (tmp, p);
      return tmp;
    }

  sprintf (tmp, "%d", val);
  return tmp;
}

GList *
load_enum_values (char *extname, oss_mixext * rec)
{
  int i;
  static char tmp[4096];
  GList *list = NULL;
  oss_mixer_enuminfo ei;

  memset (&ei, 0, sizeof (ei));

  ei.dev = rec->dev;
  ei.ctrl = rec->ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_ENUMINFO, &ei) != -1)
    {
      int n = ei.nvalues;
      char *p;

      if (n > rec->maxvalue)
	n = rec->maxvalue;

      for (i = 0; i < rec->maxvalue; i++)
	if (rec->enum_present[i / 8] & (1 << (i % 8)))
	  {
	    p = ei.strings + ei.strindex[i];
	    list = g_list_append (list, strdup (p));
	  }

      return list;
    }

  *tmp = 0;
  if (*extname == '.')
    extname++;

  for (i = 0; i < rec->maxvalue; i++)
    if (rec->enum_present[i / 8] & (1 << (i % 8)))
      {
	list = g_list_append (list, showenum (extname, rec, i));
      }

  return list;
}

int
findenum (oss_mixext * rec, const char *arg)
{
  int i, v;
  oss_mixer_enuminfo ei;

  ei.dev = rec->dev;
  ei.ctrl = rec->ctrl;

  if (ioctl (mixerfd, SNDCTL_MIX_ENUMINFO, &ei) != -1)
    {
      int n = ei.nvalues;
      char *p;

      if (n > rec->maxvalue)
	n = rec->maxvalue;

      for (i = 0; i < rec->maxvalue; i++)
	if (rec->enum_present[i / 8] & (1 << (i % 8)))
	  {
	    p = ei.strings + ei.strindex[i];
	    if (strcmp (p, arg) == 0)
	      return i;
	  }
    }

  if (sscanf (arg, "%d", &v) == 1)
    return v;

  fprintf (stderr, "Invalid enumerated value '%s'\n", arg);
  return 0;
}

static int
get_value (oss_mixext * thisrec)
{
  oss_mixer_value val;
  extern int errno;

  val.dev = dev;
  val.ctrl = thisrec->ctrl;
  val.timestamp = thisrec->timestamp;

  if (ioctl (mixerfd, SNDCTL_MIX_READ, &val) == -1)
    {
      if (errno == EPIPE)
	{
	  fprintf (stderr,
		   "ossxmix: Mixer device disconnected from the system\n");
	  exit (-1);
	}

      if (errno == EIDRM)
	{
	  if (fully_started)
	    {
/*
 * The mixer structure got changed for some reason. This program
 * is not designed to handle this event properly so all we can do
 * is a brute force restart.
 *
 * Well written applications should just dispose the changed GUI elements 
 * (by comparing the {!code timestamp} fields. Then the new fields can be
 * created just like we did when starting the program.
 */
	      fprintf (stderr,
		       "ossxmix: Mixer structure changed - restarting.\n");
	      if (execlp (saved_argv[0], saved_argv[0],
			  saved_argv[1], saved_argv[2],
			  saved_argv[3], saved_argv[4],
			  saved_argv[5], saved_argv[6],
			  saved_argv[7], saved_argv[8], NULL) == -1)
		perror (saved_argv[0]);
	      exit (-1);
	    }
	  else
	    {
	      fprintf (stderr,
		       "ossxmix: Mixer structure changed - aborting.\n");
	      exit (-1);
	    }
	}
      fprintf (stderr, "%s\n", thisrec->id);
      perror ("SNDCTL_MIX_READ");
      return 0;
    }

  return val.value;
}

static void
set_value (oss_mixext * thisrec, int value)
{
  oss_mixer_value val;
  extern int errno;

  if (!(thisrec->flags & MIXF_WRITEABLE))
    return;
  val.dev = dev;
  val.ctrl = thisrec->ctrl;
  val.value = value;
  val.timestamp = thisrec->timestamp;
  set_counter++;

  if (ioctl (mixerfd, SNDCTL_MIX_WRITE, &val) == -1)
    {
      if (errno == EIDRM)
	{
	  if (fully_started)
	    {
	      fprintf (stderr,
		       "ossxmix: Mixer structure changed - restarting.\n");
	      if (execlp (saved_argv[0], saved_argv[0],
			  saved_argv[1], saved_argv[2],
			  saved_argv[3], saved_argv[4],
			  saved_argv[5], saved_argv[6],
			  saved_argv[7], saved_argv[8], NULL) == -1)
		perror (saved_argv[0]);
	      exit (-1);
	    }
	  else
	    {
	      fprintf (stderr,
		       "ossxmix: Mixer structure changed - aborting.\n");
	      exit (-1);
	    }
	}
      fprintf (stderr, "%s\n", thisrec->id);
      perror ("SNDCTL_MIX_WRITE");
    }
}

void
create_update (GtkWidget * frame, GtkObject * left, GtkObject * right,
	       GtkWidget * gang, oss_mixext * thisrec, int what, int parm)
{
  ctlrec_t *srec;

  srec = malloc (sizeof (*srec));
  srec->mixext = thisrec;
  srec->parm = parm;
  srec->whattodo = what;
  srec->frame = frame;
  srec->left = left;
  srec->right = right;
  srec->gang = gang;
  srec->frame_name[0] = '\0';

  srec->next = check_list;
  check_list = srec;
}

void
manage_label (GtkWidget * frame, oss_mixext * thisrec)
{
  char new_label[FRAME_NAME_LENGTH], tmp[64];

  if (thisrec->id[0] != '@')
    return;

  *new_label = 0;

  strcpy (tmp, &thisrec->id[1]);

  if ((tmp[0] == 'd' && tmp[1] == 's' && tmp[2] == 'p') ||
      (tmp[0] == 'p' && tmp[1] == 'c' && tmp[2] == 'm'))
    {
      int dspnum;
      oss_audioinfo ainfo;

      if (sscanf (&tmp[3], "%d", &dspnum) != 1)
	return;

      ainfo.dev = dspnum;
      if (ioctl (mixerfd, SNDCTL_ENGINEINFO, &ainfo) == -1)
	{
	  perror ("SNDCTL_ENGINEINFO");
	  return;
	}
      create_update (frame, NULL, NULL, NULL, thisrec, WHAT_LABEL, dspnum);
      if (*ainfo.label != 0)
	{
	  strncpy (new_label, ainfo.label, FRAME_NAME_LENGTH);
	  new_label[FRAME_NAME_LENGTH-1] = 0;
	}
    }


  if (*new_label != 0)
    gtk_frame_set_label (GTK_FRAME (frame), new_label);

}

void
Scrolled (GtkAdjustment * adjust, gpointer data)
{
  int val, origval, lval, rval;
  int side, gang_on;
  ctlrec_t *srec = (ctlrec_t *) data;
  int shift = 8;

  val = srec->mixext->maxvalue - (int) adjust->value;
  origval = (int) adjust->value;

  if (srec->mixext->type == MIXT_MONOSLIDER16
      || srec->mixext->type == MIXT_STEREOSLIDER16)
    shift = 16;

  if (srec->full_scale)
    side = BOTH;
  else if (srec->right == NULL)
    side = MONO;
  else if (GTK_OBJECT (adjust) == srec->left)
    side = LEFT;
  else
    side = RIGHT;

  if (srec->mixext->type == MIXT_3D)
    {
#ifdef TEST_JOY
#else
      lval = 100 - (int) GTK_ADJUSTMENT (srec->left)->value;
      rval = 360 - (int) GTK_ADJUSTMENT (srec->right)->value;
      val = (50 << 8) | (lval & 0xff) | (rval << 16);
      set_value (srec->mixext, val);
#endif
      return;
    }

  if (side == BOTH)
    {
      set_value (srec->mixext, val);
      return;
    }

  if (side == MONO)
    {
      val = val | (val << shift);
      set_value (srec->mixext, val);
      return;
    }

  gang_on = 0;

  if (srec->gang != NULL)
    {
      gang_on = GTK_TOGGLE_BUTTON (srec->gang)->active;
    }

  if (gang_on)
    {
      val = val | (val << shift);
      set_value (srec->mixext, val);

      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->left), origval);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->right), origval);

      return;
    }

  lval = srec->mixext->maxvalue - (int) GTK_ADJUSTMENT (srec->left)->value;
  rval = srec->mixext->maxvalue - (int) GTK_ADJUSTMENT (srec->right)->value;
  val = lval | (rval << shift);
  set_value (srec->mixext, val);
}

void
GangChange (GtkToggleButton * but, gpointer data)
{
  ctlrec_t *srec = (ctlrec_t *) data;
  int val, lval, rval;

  if (!but->active)
    return;

  lval = srec->mixext->maxvalue - (int) GTK_ADJUSTMENT (srec->left)->value;
  rval = srec->mixext->maxvalue - (int) GTK_ADJUSTMENT (srec->right)->value;
  if (lval < rval)
    lval = rval;
  val = lval | (lval << 8);
  set_value (srec->mixext, val);

  val = srec->mixext->maxvalue - (val & 0xff);
  gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->left), val);
  gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->right), val);
}

/*ARGSUSED*/
void
ChangeEnum (GtkToggleButton * but, gpointer data)
{
  ctlrec_t *srec = (ctlrec_t *) data;
  int val;
  const char *entry;

  entry = gtk_entry_get_text (GTK_ENTRY (srec->left));
  if (*entry == 0)		/* Empty - Why? */
    return;

  val = findenum (srec->mixext, entry);

  set_value (srec->mixext, val);
}


void
ChangeOnoff (GtkToggleButton * but, gpointer data)
{
  ctlrec_t *srec = (ctlrec_t *) data;
  int val;

  val = but->active;

  set_value (srec->mixext, val);
}

void
store_ctl (ctlrec_t * rec)
{
  rec->next = control_list;
  control_list = rec;
}

void
connect_scrollers (oss_mixext * thisrec, GtkObject * left, GtkObject * right,
		   GtkWidget * gang)
{
  ctlrec_t *srec;

  srec = malloc (sizeof (*srec));
  srec->mixext = thisrec;
  srec->left = left;
  srec->right = right;
  srec->full_scale = (thisrec->type == MIXT_SLIDER);
  srec->gang = gang;
  gtk_signal_connect (GTK_OBJECT (left), "value_changed",
		      GTK_SIGNAL_FUNC (Scrolled), srec);
  if (right != NULL)
    gtk_signal_connect (GTK_OBJECT (right), "value_changed",
			GTK_SIGNAL_FUNC (Scrolled), srec);
  if (gang != NULL)
    gtk_signal_connect (GTK_OBJECT (gang), "toggled",
			GTK_SIGNAL_FUNC (GangChange), srec);

  store_ctl (srec);

}

void
connect_peak (oss_mixext * thisrec, GtkWidget * left, GtkWidget * right)
{
  ctlrec_t *srec;

  srec = malloc (sizeof (*srec));
  srec->mixext = thisrec;
  srec->left = GTK_OBJECT (left);
  if (right == NULL)
    srec->right = NULL;
  else
    srec->right = GTK_OBJECT (right);
  srec->gang = NULL;
  srec->lastleft = 0;
  srec->lastright = 0;

  srec->next = peak_list;
  peak_list = srec;
}

void
connect_value_poll (oss_mixext * thisrec, GtkWidget * wid)
{
  ctlrec_t *srec;

  srec = malloc (sizeof (*srec));
  srec->mixext = thisrec;
  srec->left = GTK_OBJECT (wid);
  srec->right = NULL;
  srec->gang = NULL;
  srec->lastleft = 0;
  srec->lastright = 0;

  srec->next = value_poll_list;
  value_poll_list = srec;
}

void
connect_enum (oss_mixext * thisrec, GtkObject * entry)
{
  ctlrec_t *srec;

  srec = malloc (sizeof (*srec));
  srec->mixext = thisrec;
  srec->left = entry;
  srec->right = NULL;
  srec->gang = NULL;
  gtk_signal_connect (entry, "changed", GTK_SIGNAL_FUNC (ChangeEnum), srec);
  store_ctl (srec);

}

static void update_label (oss_mixext * mixext, GtkWidget * wid, int val);

void
connect_onoff (oss_mixext * thisrec, GtkObject * entry)
{
  ctlrec_t *srec;

  srec = malloc (sizeof (*srec));
  srec->mixext = thisrec;
  srec->left = entry;
  srec->right = NULL;
  srec->gang = NULL;
  gtk_signal_connect (entry, "toggled", GTK_SIGNAL_FUNC (ChangeOnoff), srec);
  store_ctl (srec);

}

/*
 * The load_devinfo() routine loads the mixer definitions and creates the
 * GUI structure based on it.
 *
 * In short the algorithm is to create GTK vbox or hbox widgets for
 * each group. A vbox is created for the root group. Then the orientation is
 * changed in each level of sub-groups. However there are some exceptions to
 * this rule (will be described in the documentation.
 *
 * The individual controls are just placed inside the hbox/vbx widgets of
 * the parent groups. However the "legacy" mixer controls (before
 * MIXT_MARKER) will be handled in slightly different way (please consult
 * the documentation).
 */

void
load_devinfo (int dev)
{
  int i, n, val, left, right, mx, g;
  int angle, vol;
  int width;
  oss_mixext *thisrec;
  GtkWidget *wid, *wid2, *gang, *rootwid, *pw, *frame, *box;
  GtkObject *adjust;
  GtkObject *adjust2;
  int change_orient = 1;

  n = dev;

  if (ioctl (mixerfd, SNDCTL_MIX_NREXT, &n) == -1)
    {
      perror ("SNDCTL_MIX_NREXT");
      if (errno == EINVAL)
	fprintf (stderr, "Error: OSS version 3.9 or later is required\n");
      exit (-1);
    }

  if ((extrec = malloc (n * sizeof (oss_mixext))) == NULL)
    {
      fprintf (stderr, "malloc of %d entries failed\n", n);
      exit (-1);
    }

  nrext = n;
  for (i = 0; i < n; i++)
    {
      char tmp[1024], *name;
      int parent, ori;
      int mask = 0xff, shift = 8;
      gboolean expand = TRUE;

      thisrec = &extrec[i];
      thisrec->dev = dev;
      thisrec->ctrl = i;

      if (ioctl (mixerfd, SNDCTL_MIX_EXTINFO, thisrec) == -1)
	{
	  if (errno == EINVAL)
	    {
	      printf ("Incompatible OSS version\n");
	      exit (-1);
	    }
	  perror ("SNDCTL_MIX_EXTINFO");
	  exit (-1);
	}

      if (thisrec->id[0] == '-')	/* Hidden one */
	thisrec->id[0] = 0;

      if (thisrec->type == MIXT_STEREOSLIDER16
	  || thisrec->type == MIXT_MONOSLIDER16)
	{
	  mask = 0xffff;
	  shift = 16;
	}

      switch (thisrec->type)
	{
	case MIXT_DEVROOT:
	  root = (oss_mixext_root *) & thisrec->data;
	  extnames[i] = "";
	  rootwid = pw = gtk_vbox_new (FALSE, 2);
	  wid = gtk_hbox_new (FALSE, 1);
	  gtk_box_pack_start (GTK_BOX (pw), wid, TRUE, TRUE, 1);
	  gtk_widget_show (wid);
	  gtk_widget_show (pw);
	  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW
						 (scrolledwin), pw);
	  widgets[i] = wid;
	  break;

	case MIXT_GROUP:
	  if (!show_all)
	    break;
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*extnames[parent] == 0)
	    strcpy (tmp, name);
	  else
	    sprintf (tmp, "%s.%s", extnames[parent], name);
	  store_name (i, tmp);
	  pw = widgets[parent];
	  if (!change_orient && parent == 0)
	    pw = rootwid;
	  if (thisrec->flags & MIXF_FLAT)	/* Group contains only ENUM controls */
	    expand = FALSE;
	  if (pw == NULL)
	    fprintf (stderr, "Root group %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  ori = !orient[parent];
	  if (change_orient)
	    ori = !ori;
	  orient[i] = ori;

	  switch (ori)
	    {
	    case 0:
	      wid = gtk_vbox_new (FALSE, 1);
	      break;

	    default:
	      ngroups++;
	      if (!use_layout_b)
		ori = !ori;
	      orient[i] = ori;
	      wid = gtk_hbox_new (FALSE, 1);
	    }

	  frame = gtk_frame_new (get_name (i));
	  manage_label (frame, thisrec);
	  gtk_box_pack_start (GTK_BOX (pw), frame, expand, TRUE, 1);
	  gtk_container_add (GTK_CONTAINER (frame), wid);
	  gtk_widget_show (frame);
	  gtk_widget_show (wid);
	  widgets[i] = wid;
	  break;

	case MIXT_HEXVALUE:
	case MIXT_VALUE:
	  if (!show_all)
	    break;
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  val = get_value (thisrec);
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);

	  wid = gtk_label_new ("????");
	  gtk_box_pack_start (GTK_BOX (pw), wid, FALSE, TRUE, 0);
	  if (thisrec->flags & MIXF_POLL)
	    connect_value_poll (thisrec, wid);
	  else
	    create_update (NULL, NULL, NULL, wid, thisrec, WHAT_UPDATE, i);
	  update_label (thisrec, wid, val);
	  gtk_widget_show (wid);
	  break;

	case MIXT_STEREODB:
	case MIXT_MONODB:
	  if (!show_all)
	    break;
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  wid = gtk_button_new_with_label (get_name (i));
	  gtk_box_pack_start (GTK_BOX (pw), wid, FALSE, TRUE, 0);
	  gtk_widget_show (wid);
	  break;

	case MIXT_ONOFF:
	  if (!show_all)
	    break;
	  parent = thisrec->parent;
	  val = get_value (thisrec) & 0x01;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  wid = gtk_check_button_new_with_label (get_name (i));
	  connect_onoff (thisrec, GTK_OBJECT (wid));
	  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wid), val);
	  create_update (NULL, NULL, NULL, wid, thisrec, WHAT_UPDATE, 0);
	  gtk_box_pack_start (GTK_BOX (pw), wid, FALSE, TRUE, 0);
	  gtk_widget_show (wid);
	  break;

	case MIXT_STEREOVU:
	case MIXT_STEREOPEAK:
	  if (!show_all)
	    break;
	  val = get_value (thisrec);
	  mx = thisrec->maxvalue;
	  left = mx - (val & 0xff);
	  right = mx - ((val >> 8) & 0xff);
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
#if 0
	  adjust = GTK_OBJECT (gtk_adjustment_new (left, 0, mx, 1, 5, 0));
	  adjust2 = GTK_OBJECT (gtk_adjustment_new (right, 0, mx, 1, 5, 0));
	  wid = gtk_vscale_new (GTK_ADJUSTMENT (adjust));
	  gtk_scale_set_digits (GTK_SCALE (wid), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid), FALSE);
	  wid2 = gtk_vscale_new (GTK_ADJUSTMENT (adjust2));
	  gtk_scale_set_digits (GTK_SCALE (wid2), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid2), FALSE);
#endif
	  wid = gtk_vu_new ();
	  wid2 = gtk_vu_new ();

	  connect_peak (thisrec, wid, wid2);
	  gtk_box_set_child_packing (GTK_BOX (rootwid), rootwid, TRUE, TRUE,
				     100, GTK_PACK_START);
	  if (strcmp (get_name (parent), get_name (i)) != 0)
	    {
	      frame = gtk_frame_new (get_name (i));
	      manage_label (frame, thisrec);
	      gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	      gtk_box_pack_start (GTK_BOX (pw), frame, FALSE, TRUE, 0);
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_container_add (GTK_CONTAINER (frame), box);
	      gtk_box_pack_start (GTK_BOX (box), wid, TRUE, TRUE, 1);
	      gtk_box_pack_start (GTK_BOX (box), wid2, TRUE, TRUE, 1);
	      gtk_widget_show (frame);
	      gtk_widget_show (box);
	      gtk_widget_show (wid);
	      gtk_widget_show (wid2);
	    }
	  else
	    {
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_box_pack_start (GTK_BOX (pw), box, FALSE, TRUE, 1);
	      gtk_box_pack_start (GTK_BOX (box), wid, TRUE, TRUE, 1);
	      gtk_box_pack_start (GTK_BOX (box), wid2, TRUE, TRUE, 1);
	      gtk_widget_show (wid);
	      gtk_widget_show (box);
	      gtk_widget_show (wid2);
	    }
	  break;

	case MIXT_MONOVU:
	case MIXT_MONOPEAK:
	  if (!show_all)
	    break;
	  val = get_value (thisrec);
	  mx = thisrec->maxvalue;
	  left = mx - (val & 0xff);
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  wid = gtk_vu_new ();

	  connect_peak (thisrec, wid, NULL);
	  gtk_box_set_child_packing (GTK_BOX (rootwid), rootwid, TRUE, TRUE,
				     100, GTK_PACK_START);
	  if (strcmp (get_name (parent), get_name (i)) != 0)
	    {
	      frame = gtk_frame_new (get_name (i));
	      manage_label (frame, thisrec);
	      gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	      gtk_box_pack_start (GTK_BOX (pw), frame, FALSE, TRUE, 0);
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_container_add (GTK_CONTAINER (frame), box);
	      gtk_box_pack_start (GTK_BOX (box), wid, TRUE, TRUE, 1);
	      gtk_widget_show (frame);
	      gtk_widget_show (box);
	      gtk_widget_show (wid);
	    }
	  else
	    {
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_box_pack_start (GTK_BOX (pw), box, FALSE, TRUE, 1);
	      gtk_box_pack_start (GTK_BOX (box), wid, TRUE, TRUE, 1);
	      gtk_widget_show (wid);
	      gtk_widget_show (box);
	    }
	  break;

	case MIXT_STEREOSLIDER:
	case MIXT_STEREOSLIDER16:
	  if (!show_all)
	    break;
	  width = -1;

	  if (width_adjust < 0)
	    width = 12;
	  val = get_value (thisrec);
	  mx = thisrec->maxvalue;
	  left = mx - (val & mask);
	  right = mx - ((val >> shift) & mask);
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  adjust = GTK_OBJECT (gtk_adjustment_new (left, 0, mx, 1, 5, 0));
	  adjust2 = GTK_OBJECT (gtk_adjustment_new (right, 0, mx, 1, 5, 0));
	  gang = gtk_check_button_new ();
	  g = (left == right);
	  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gang), g);

	  connect_scrollers (thisrec, adjust, adjust2, gang);
	  create_update (NULL, adjust, adjust2, gang, thisrec, WHAT_UPDATE,
			 0);

	  wid = gtk_vscale_new (GTK_ADJUSTMENT (adjust));
#ifndef GTK1_ONLY
	  gtk_widget_set_size_request (wid, width, 80);
#endif
	  gtk_scale_set_digits (GTK_SCALE (wid), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid), FALSE);

	  wid2 = gtk_vscale_new (GTK_ADJUSTMENT (adjust2));
#ifndef GTK1_ONLY
	  gtk_widget_set_size_request (wid2, width, 80);
#endif
	  gtk_scale_set_digits (GTK_SCALE (wid2), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid2), FALSE);

	  if (strcmp (get_name (parent), get_name (i)) != 0)
	    {
	      frame = gtk_frame_new (get_name (i));
	      manage_label (frame, thisrec);
	      /* gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN); */
	      gtk_box_pack_start (GTK_BOX (pw), frame, FALSE, FALSE, 1);
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_container_add (GTK_CONTAINER (frame), box);
	      gtk_box_pack_start (GTK_BOX (box), wid, FALSE, TRUE, 0);
	      gtk_box_pack_start (GTK_BOX (box), wid2, FALSE, TRUE, 0);
	      gtk_box_pack_start (GTK_BOX (box), gang, FALSE, TRUE, 0);
	      gtk_widget_show (frame);
	      gtk_widget_show (box);
	      gtk_widget_show (wid);
	      gtk_widget_show (wid2);
	      gtk_widget_show (gang);
	    }
	  else
	    {
	      box = gtk_hbox_new (FALSE, 1);
#if 1
	      gtk_box_pack_start (GTK_BOX (pw), box, TRUE, TRUE, 1);
#else
	      gtk_box_pack_start (GTK_BOX (pw), box, FALSE, FALSE, 1);
#endif
	      gtk_box_pack_start (GTK_BOX (box), wid, FALSE, TRUE, 0);
	      gtk_box_pack_start (GTK_BOX (box), wid2, FALSE, TRUE, 0);
	      gtk_box_pack_start (GTK_BOX (box), gang, FALSE, TRUE, 0);
	      gtk_widget_show (wid);
	      gtk_widget_show (box);
	      gtk_widget_show (gang);
	      gtk_widget_show (wid2);
	    }
	  break;

	case MIXT_3D:
#ifdef TEST_JOY
	  if (!show_all)
	    break;
	  val = get_value (thisrec);
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  wid = gtk_joy_new ();
	  create_update (NULL, NULL, NULL, wid, thisrec, WHAT_UPDATE, 0);

	  gtk_box_set_child_packing (GTK_BOX (rootwid), rootwid, TRUE, TRUE,
				     100, GTK_PACK_START);
	  if (strcmp (get_name (parent), get_name (i)) != 0)
	    {
	      frame = gtk_frame_new (get_name (i));
	      manage_label (frame, thisrec);
	      gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	      gtk_box_pack_start (GTK_BOX (pw), frame, FALSE, TRUE, 0);
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_container_add (GTK_CONTAINER (frame), box);
	      gtk_box_pack_start (GTK_BOX (box), wid, TRUE, TRUE, 1);
	      gtk_widget_show (frame);
	      gtk_widget_show (box);
	      gtk_widget_show (wid);
	    }
	  else
	    {
	      box = gtk_hbox_new (FALSE, 1);
	      gtk_box_pack_start (GTK_BOX (pw), box, FALSE, TRUE, 1);
	      gtk_box_pack_start (GTK_BOX (box), wid, TRUE, TRUE, 1);
	      gtk_widget_show (wid);
	      gtk_widget_show (box);
	    }
	  break;
#else
	  if (!show_all)
	    break;
	  val = get_value (thisrec);
	  mx = thisrec->maxvalue;
	  vol = 100 - (val & 0x00ff);
	  angle = 360 - ((val >> 16) & 0xffff);
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  adjust = GTK_OBJECT (gtk_adjustment_new (vol, 0, 100, 1, 5, 0));
	  adjust2 = GTK_OBJECT (gtk_adjustment_new (angle, 0, 360, 1, 5, 0));
	  connect_scrollers (thisrec, adjust, adjust2, NULL);
	  create_update (NULL, adjust, adjust2, NULL, thisrec, WHAT_UPDATE,
			 0);
	  wid = gtk_vscale_new (GTK_ADJUSTMENT (adjust));
	  gtk_scale_set_digits (GTK_SCALE (wid), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid), FALSE);
	  wid2 = gtk_vscale_new (GTK_ADJUSTMENT (adjust2));
	  gtk_scale_set_digits (GTK_SCALE (wid2), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid2), FALSE);

	  frame = gtk_frame_new (get_name (i));
	  manage_label (frame, thisrec);
	  /* gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN); */
	  gtk_box_pack_start (GTK_BOX (pw), frame, FALSE, FALSE, 1);
	  box = gtk_hbox_new (FALSE, 1);
	  gtk_container_add (GTK_CONTAINER (frame), box);
	  gtk_box_pack_start (GTK_BOX (box), wid, FALSE, TRUE, 0);
	  gtk_box_pack_start (GTK_BOX (box), wid2, TRUE, TRUE, 0);
	  gtk_widget_show (frame);
	  gtk_widget_show (box);
	  gtk_widget_show (wid);
	  gtk_widget_show (wid2);
	  gtk_widget_show (gang);
	  break;
#endif

	case MIXT_MONOSLIDER:
	case MIXT_MONOSLIDER16:
	case MIXT_SLIDER:
	  if (!show_all)
	    break;
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  val = get_value (thisrec);
	  mx = thisrec->maxvalue;

	  if (thisrec->type == MIXT_MONOSLIDER)
	    val &= 0xff;
	  else if (thisrec->type == MIXT_MONOSLIDER16)
	    val &= 0xffff;

	  val = mx - val;
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);
	  adjust = GTK_OBJECT (gtk_adjustment_new (val, 0, mx, 1, 5, 0));
	  connect_scrollers (thisrec, adjust, NULL, NULL);
	  create_update (NULL, adjust, NULL, NULL, thisrec, WHAT_UPDATE, 0);
	  wid = gtk_vscale_new (GTK_ADJUSTMENT (adjust));
#ifndef GTK1_ONLY
	  gtk_widget_set_size_request (wid, -1, 80);
#endif
	  gtk_scale_set_digits (GTK_SCALE (wid), 0);
	  gtk_scale_set_draw_value (GTK_SCALE (wid), FALSE);

	  if (strcmp (get_name (parent), get_name (i)) != 0)
	    {
	      frame = gtk_frame_new (get_name (i));
	      manage_label (frame, thisrec);
	      /* gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN); */
	      gtk_box_pack_start (GTK_BOX (pw), frame, FALSE, FALSE, 1);
	      gtk_container_add (GTK_CONTAINER (frame), wid);
	      gtk_widget_show (frame);
	      gtk_widget_show (wid);
	    }
	  else
	    {
	      gtk_box_pack_start (GTK_BOX (pw), wid, FALSE, FALSE, 1);
	      gtk_widget_show (wid);
	    }
	  break;

	case MIXT_ENUM:

	  if (!show_all)
	    break;
	  parent = thisrec->parent;
	  name = cut_name (thisrec->id);
	  if (*thisrec->id == 0)
	    extnames[i] = extnames[parent];
	  else
	    {
	      sprintf (tmp, "%s.%s", extnames[parent], name);
	      store_name (i, tmp);
	    }
	  val = get_value (thisrec) & 0xff;
	  pw = widgets[parent];
	  if (pw == NULL)
	    fprintf (stderr, "Control %d/%s: Parent(%d)==NULL\n", i,
		     extnames[i], parent);

	  wid = gtk_combo_new ();
	  {
	    GList *opt = NULL;

	    if (!(thisrec->flags & MIXF_WIDE))
	      gtk_widget_set_usize (wid, 100 + 20 * width_adjust, -1);
	    opt = load_enum_values (extnames[i], thisrec);
	    gtk_combo_set_popdown_strings (GTK_COMBO (wid), opt);

	    gtk_combo_set_use_arrows_always (GTK_COMBO (wid), 1);
	    gtk_entry_set_editable (GTK_ENTRY (GTK_COMBO (wid)->entry),
				    FALSE);
	    gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (wid)->entry),
				showenum (extnames[i], thisrec, val));
	  }
	  connect_enum (thisrec, GTK_OBJECT (GTK_COMBO (wid)->entry));
	  create_update (NULL, NULL, NULL, wid, thisrec, WHAT_UPDATE, i);
	  frame = gtk_frame_new (get_name (i));
	  manage_label (frame, thisrec);
	  gtk_box_pack_start (GTK_BOX (pw), frame, TRUE, FALSE, 0);
	  gtk_container_add (GTK_CONTAINER (frame), wid);
	  gtk_widget_show (frame);
	  gtk_widget_show (wid);
	  break;

	case MIXT_MARKER:
	  show_all = 1;
	  change_orient = 0;
	  break;

	default:;
	  fprintf (stderr, "Unknown type for control %s\n", extnames[i]);
	}

    }
}

/*
 * The update_label() routine is used to update the values of certain
 * read only controls.
 */

static void
update_label (oss_mixext * mixext, GtkWidget * wid, int val)
{
  char tmp[100];

  if (mixext->type == MIXT_HEXVALUE)
    sprintf (tmp, "[%s: 0x%x] ", get_name (mixext->ctrl), val);
  else
    sprintf (tmp, "[%s: %d] ", get_name (mixext->ctrl), val);

  if (mixext->flags & MIXF_HZ)
    {
      if (val > 1000000)
	{
	  sprintf (tmp, "[%s: %d.%03d MHz] ", get_name (mixext->ctrl),
		   val / 1000000, (val / 1000) % 1000);
	}
      else if (val > 1000)
	{
	  sprintf (tmp, "[%s: %d.%03d kHz] ", get_name (mixext->ctrl),
		   val / 1000, val % 1000);
	}
      else
	sprintf (tmp, "[%s: %d Hz] ", get_name (mixext->ctrl), val);
    }
  else if (mixext->flags & MIXF_OKFAIL)
    {
      if (val != 0)
	sprintf (tmp, "[%s: Ok] ", get_name (mixext->ctrl));
      else
	sprintf (tmp, "[%s: Fail] ", get_name (mixext->ctrl));
    }
  gtk_label_set (GTK_LABEL (wid), tmp);
}

/*
 * The do_update() routine reads a value of certain mixer control
 * and updates the on-screen value depending on the type of the control.
 */

static void
do_update (ctlrec_t * srec)
{
  int val, mx, left, right, vol, angle;
  char *p;
  int mask = 0xff, shift = 8;

  val = get_value (srec->mixext);

  if (srec->mixext->type == MIXT_MONOSLIDER16
      || srec->mixext->type == MIXT_STEREOSLIDER16)
    {
      mask = 0xffff;
      shift = 16;
    }

  switch (srec->mixext->type)
    {
    case MIXT_ONOFF:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (srec->gang), val);
      break;

    case MIXT_ENUM:
      gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (srec->gang)->entry),
			  p =
			  showenum (extnames[srec->parm], srec->mixext, val));
      free (p);
      break;

    case MIXT_VALUE:
    case MIXT_HEXVALUE:
      update_label (srec->mixext, (srec->gang), val);
      break;

    case MIXT_SLIDER:
      mx = srec->mixext->maxvalue;
      val = mx - val;
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->left), val);
      break;

    case MIXT_MONOSLIDER:
      mx = srec->mixext->maxvalue;
      val = mx - (val & mask);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->left), val);
      break;

    case MIXT_STEREOSLIDER:
    case MIXT_STEREOSLIDER16:
      mx = srec->mixext->maxvalue;
      left = mx - (val & mask);
      right = mx - ((val >> shift) & mask);
      if (srec->gang != NULL)
	if (left != right)
	  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (srec->gang), 0);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->left), left);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->right), right);
      break;

    case MIXT_3D:
#ifdef TEST_JOY
      if (srec->gang != NULL)
	gtk_joy_set_level (GTK_JOY (srec->gang), val);
#else
      vol = 100 - (val & 0x00ff);
      angle = 360 - ((val >> 16) & 0xffff);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->left), vol);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (srec->right), angle);
#endif
      break;
    }
}

/*
 * The poll_all() routine get's called reqularily. It checks the 
 * modify counter for the mixer by calling {!nlink SNDCTL_MIXERINFO}.
 * It checks if some other mixer program has made changes to the settings
 * by comparing the modify counter against the "expected" value.
 *
 * If the mixer was chganged then all the controls will be reloaded and updated.
 */

/*ARGSUSED*/
gint
poll_all (gpointer data)
{
  ctlrec_t *srec;
  oss_audioinfo ainfo;
  char new_label[FRAME_NAME_LENGTH] = "";
  int status_changed = 0;
  oss_mixerinfo inf;

  inf.dev = -1;
  if (ioctl (mixerfd, SNDCTL_MIXERINFO, &inf) == -1)
    {
      perror ("SNDCTL_MIXERINFO");
      exit (-1);
    }

/*
 * Compare the modify counter.
 */
  if ((inf.modify_counter - prev_update_counter) > set_counter)
    status_changed = 1;
  prev_update_counter = inf.modify_counter;
  set_counter = 0;

  srec = check_list;

  while (srec != NULL)
    {
      switch (srec->whattodo)
	{
	case WHAT_LABEL:
/*
 * Names of certain mixer controls depend on the application that is using
 * the associated audio device. Handling for this is here
 */
	  ainfo.dev = srec->parm;
	  if (ioctl (mixerfd, SNDCTL_ENGINEINFO, &ainfo) == -1)
	    {
	      perror ("SNDCTL_ENGINEINFO");
	      continue;
	    }
	  if (*ainfo.label != 0)
	    {
	      strncpy (new_label, ainfo.label, FRAME_NAME_LENGTH);
	      new_label[FRAME_NAME_LENGTH-1] = '\0';
	    }
	  else
	    {
	      snprintf (new_label, FRAME_NAME_LENGTH, "pcm%d", srec->parm);
	    }
	  if ((srec->frame != NULL) &&
	      (strncmp(srec->frame_name, new_label, FRAME_NAME_LENGTH)))
	    {
	      strcpy(srec->frame_name, new_label);
     	      gtk_frame_set_label (GTK_FRAME (srec->frame), new_label);
	    }
	  break;
	case WHAT_UPDATE:
	  if (status_changed)
	    do_update (srec);
	  break;
	}
      srec = srec->next;
    }
  return TRUE;
}

/*
 * The poll_peaks() routine gets called several times per second to update the
 * VU/peak meter LED bar widgets.
 */

/*ARGSUSED*/
gint
poll_peaks (gpointer data)
{
  ctlrec_t *srec;
  int val, left, right;

  srec = peak_list;

  while (srec != NULL)
    {
      val = get_value (srec->mixext);

      left = val & 0xff;
      right = (val >> 8) & 0xff;

      if (left > srec->lastleft)
	srec->lastleft = left;

      if (right > srec->lastright)
	srec->lastright = right;

      left = srec->lastleft;
      right = srec->lastright;

      /*      gtk_adjustment_set_value(GTK_ADJUSTMENT(srec->left), left);
         gtk_adjustment_set_value(GTK_ADJUSTMENT(srec->right), right); */
      gtk_vu_set_level (GTK_VU (srec->left),
			(left * 8) / srec->mixext->maxvalue);

      if (srec->right != NULL)
	gtk_vu_set_level (GTK_VU (srec->right),
			  (right * 8) / srec->mixext->maxvalue);


      if (srec->lastleft > 0)
	srec->lastleft -= PEAK_DECAY;
      if (srec->lastright > 0)
	srec->lastright -= PEAK_DECAY;

      srec = srec->next;
    }

  return TRUE;
}

/*ARGSUSED*/
gint
poll_values (gpointer data)
{
  ctlrec_t *srec;
  int val;

  srec = value_poll_list;

  while (srec != NULL)
    {
      val = get_value (srec->mixext);

      update_label (srec->mixext, GTK_WIDGET (srec->left), val);

      srec = srec->next;
    }

  return TRUE;
}

static int
find_default_mixer (void)
{
  oss_sysinfo si;
  oss_mixerinfo mi;
  int i, best = 0, bestpri = 0;
  int mixerfd;

  if ((mixerfd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      perror ("/dev/mixer");
      exit (-1);
    }

  if (ioctl (mixerfd, SNDCTL_SYSINFO, &si) == -1)
    {
      perror ("SNDCTL_SYSINFO");
      if (errno == EINVAL)
	fprintf (stderr, "Error: OSS version 4.0 or later is required\n");
      exit (-1);
    }

  for (i = 0; i < si.nummixers; i++)
    {
      mi.dev = i;

      if (ioctl (mixerfd, SNDCTL_MIXERINFO, &mi) == -1)
	continue;		/* Ignore errors */

      if (mi.priority > bestpri)
	{
	  best = i;
	  bestpri = mi.priority;
	}
    }
  close (mixerfd);

  return best;
}

int
main (int argc, char *argv[])
{
  char tmp[100];
  int i, v;
  oss_mixerinfo mi;

  saved_argc = argc;
  if (saved_argc > 9)
    saved_argc = 9;
  for (i = 0; i < saved_argc; i++)
    saved_argv[i] = argv[i];
  saved_argv[saved_argc] = NULL;

  /* Get Gtk to process the startup arguments */
  gtk_init (&argc, &argv);

  for (i = 1; i < argc; i++)
    if (argv[i][0] == '-')
      switch (argv[i][1])
	{
	case 'd':
	  dev = atoi (&argv[i][2]);
	  break;

	case 'w':
	  v = atoi (&argv[i][2]);
	  if (v <= 0)
	    v = 1;
	  width_adjust += v;
	  break;

	case 'n':
	  if (v <= 0)
	    v = 1;
	  width_adjust -= v;
	  break;

	case 'x':
	  show_all = 0;
	  break;

	case 'h':
	  printf ("Usage: %s [options...]\n", argv[0]);
	  printf ("       -h          Prints help (this screen)\n");
	  printf ("       -d<dev#>    Selects the mixer device\n");
	  printf ("       -x          Hides the \"legacy\" mixer controls\n");
	  printf ("       -w[val]     Make mixer bit wider on screen\n");
	  printf ("       -n[val]     Make mixer bit narrower on screen\n");
	  exit (0);
	  break;
	}

  if (dev == -1)
    dev = find_default_mixer ();

  if (width_adjust < -4)
    width_adjust = -4;
  if (width_adjust > 4)
    width_adjust = 4;

  /* Create the app's main window */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  scrolledwin = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwin),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_widget_show (scrolledwin);
  gtk_container_add (GTK_CONTAINER (window), scrolledwin);

  if ((mixerfd = open ("/dev/mixer", O_RDWR, 0)) == -1)
    {
      perror ("/dev/mixer");
      exit (-1);
    }


  mi.dev = dev;

  if (ioctl (mixerfd, SNDCTL_MIXERINFO, &mi) == -1)
    {
      perror ("SNDCTL_MIXERINFO");
      exit (-1);
    }

  if (mi.caps & MIXER_CAP_LAYOUT_B)
    use_layout_b = 1;

  if (mi.caps & MIXER_CAP_NARROW)
    width_adjust = 1;

  load_devinfo (dev);
  fully_started = 1;

  if (peak_list != NULL)
    gtk_timeout_add (PEAK_POLL_INTERVAL, poll_peaks, NULL);
  if (value_poll_list != NULL)
    gtk_timeout_add (VALUE_POLL_INTERVAL, poll_values, NULL);
  gtk_timeout_add (100, poll_all, NULL);

  if (root == NULL)
    {
      fprintf (stderr, "No device root node\n");
      exit (-1);
    }

  /* Connect a window's signal to a signal function */
  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
		      GTK_SIGNAL_FUNC (CloseRequest), NULL);

  sprintf (tmp, "ossxmix - device %d / %s", dev, root->name);

  gtk_window_set_title (GTK_WINDOW (window), tmp);

  gtk_widget_show (window);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwin),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  gtk_main ();

  return 0;
}

/*
 * Function to handle a close signal on the window
 */
/*ARGSUSED*/
gint
CloseRequest (GtkWidget * theWindow, gpointer data)
{
  peak_list = NULL;		/* Stop polling */
  gtk_main_quit ();
  return (FALSE);
}
