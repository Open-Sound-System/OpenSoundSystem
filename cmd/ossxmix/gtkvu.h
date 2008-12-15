#ifndef __GTK_VU_H__
#define __GTK_VU_H__

#define COPYING2 Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.

#include <gdk/gdk.h>
#include <gtk/gtkadjustment.h>
#include <gtk/gtkwidget.h>


#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GTK_VU(obj)          GTK_CHECK_CAST (obj, gtk_vu_get_type (), GtkVU)
#define GTK_VU_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, gtk_vu_get_type (), GtkVUClass)
#define GTK_IS_VU(obj)       GTK_CHECK_TYPE (obj, gtk_vu_get_type ())


  typedef struct _GtkVU GtkVU;
  typedef struct _GtkVUClass GtkVUClass;

  struct _GtkVU
  {
    GtkWidget widget;

    guint level;
    GdkGC *gc;
    GdkPixmap *pixmap;
    GdkColor colors[7];

  };

  struct _GtkVUClass
  {
    GtkWidgetClass parent_class;
  };


  GtkWidget *gtk_vu_new (void);
  GtkType gtk_vu_get_type (void);
  void gtk_vu_set_level (GtkVU * vu, guint new_level);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GTK_VU_H__ */
