#ifndef __SN_ITEM_H__
#define __SN_ITEM_H__

#include <stdio.h>

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_ITEM (sn_item_get_type ())

G_DECLARE_FINAL_TYPE (SnItem, sn_item, SN, ITEM, GObject)

struct _SnItem
{
    GObject parent_instance;

    GDBusProxy *sn_item_proxy; // SnItemProxy
    GDBusProxy *prop_proxy; // dbus properties (we can't trust SnItemProxy)

    GtkWidget *menu;
    XAppStatusIcon *status_icon;

    gchar *status;
    gchar *last_png_path;
    gchar *png_path;

    gint current_icon_id;

    gboolean is_ai;
};

SnItem *sn_item_new (GDBusProxy *sn_item_proxy, gboolean is_ai);

G_END_DECLS

#endif  /* __SN_ITEM_H__ */
