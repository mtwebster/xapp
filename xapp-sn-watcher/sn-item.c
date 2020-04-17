
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include <libxapp/xapp-status-icon.h>
#include <libdbusmenu-gtk/menu.h>
#include "sn-item.h"

G_DEFINE_TYPE (SnItem, sn_item, G_TYPE_OBJECT)

static void
sn_item_init (SnItem *self)
{
}

static void
sn_item_dispose (GObject *object)
{
    // SnItem *item = SN_ITEM (object);

    g_debug ("SnItem dispose (%p)", object);

     G_OBJECT_CLASS (sn_item_parent_class)->dispose (object);
}

static void
sn_item_finalize (GObject *object)
{
    g_debug ("SnItem finalize (%p)", object);

    G_OBJECT_CLASS (sn_item_parent_class)->finalize (object);
}

static void
sn_item_class_init (SnItemClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = sn_item_dispose;
    gobject_class->finalize = sn_item_finalize;

}

static gchar *
get_string_property (SnItem      *item,
                     const gchar *prop_name)
{
    GVariant *var, *box, *result;
    GError *error = NULL;
    gchar *ret;

    box = g_dbus_proxy_call_sync (item->prop_proxy,
                                  "Get",
                                  g_variant_new ("(ss)",
                                                 g_dbus_proxy_get_interface_name (item->sn_item_proxy),
                                                 prop_name),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  5 * 1000,
                                  NULL,
                                  &error);

    if (error != NULL)
    {
        return NULL;
    }

    var = g_variant_get_child_value (box, 0);
    result = g_variant_get_variant (var);
    ret = g_variant_dup_string (result, NULL);

    g_variant_unref (box);
    g_variant_unref (var);
    g_variant_unref (result);

    return ret;
}


static void
icon_changed (SnItem *item)
{
    gchar *icon_name, *theme_path;

    icon_name = get_string_property (item, "IconName");
    g_printerr ("icon name: %s\n", icon_name);
    xapp_status_icon_set_icon_name (item->status_icon, icon_name);

    g_free (icon_name);
}

static void
menu_changed (SnItem *item)
{
    gchar *menu_path;

    menu_path = get_string_property (item, "Menu");

    if (menu_path == NULL)
    {
        g_clear_object (&item->menu);

        xapp_status_icon_set_secondary_menu (item->status_icon, NULL);
        return;
    }

    item->menu = GTK_WIDGET (dbusmenu_gtkmenu_new ((gchar *) g_dbus_proxy_get_name (item->sn_item_proxy), menu_path));

    g_object_ref_sink (item->menu);
    xapp_status_icon_set_secondary_menu (item->status_icon, GTK_MENU (item->menu));

    g_free (menu_path);
}

static void
tooltip_changed (SnItem *item)
{
    gchar *tooltip;

    if (item->is_ai)
    {
        tooltip = get_string_property (item, "XAyatanaLabel");
    }
    else
    {
        tooltip = g_strdup ("");
    }

    xapp_status_icon_set_tooltip_text (item->status_icon, tooltip);

    g_free (tooltip);
}

static void
status_changed (SnItem *item)
{
    gchar *status;

    status = get_string_property (item, "Status");

    if (g_strcmp0 (status, "Passive") == 0)
    {
        xapp_status_icon_set_visible (item->status_icon, FALSE);
    }
    else
    {
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }
}

static void
sn_signal_received (GDBusProxy  *sn_item_proxy,
                    const gchar *sender_name,
                    const gchar *signal_name,
                    GVariant    *parameters,
                    gpointer     user_data)
{
    SnItem *item = SN_ITEM (user_data);

    if (item->prop_proxy == NULL)
    {
        return;
    }

    if (g_strcmp0 (signal_name, "NewIcon") == 0 ||
        g_strcmp0 (signal_name, "NewAttentionIcon") == 0 ||
        g_strcmp0 (signal_name, "NewOverlayIcon") == 0)
    {
        icon_changed (item);
    }
    else
    if (g_strcmp0 (signal_name, "NewStatus") == 0)
    {
        status_changed (item);
    }
    else
    if (g_strcmp0 (signal_name, "NewMenu") == 0)
    {
        menu_changed (item);
    }
    else
    if (g_strcmp0 (signal_name, "XAyatanaNewLabel") ||
        g_strcmp0 (signal_name, "Tooltip"))
    {
        tooltip_changed (item);
    }
}

static void
property_proxy_acquired (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    SnItem *item = SN_ITEM (user_data);
    GError *error = NULL;

    item->prop_proxy = g_dbus_proxy_new_finish (res, &error);

    if (error != NULL)
    {
        g_printerr ("Could not get prop proxy: %s\n", error->message);
        g_error_free (error);
        return;
    }

    g_signal_connect (item->sn_item_proxy,
                      "g-signal",
                      G_CALLBACK (sn_signal_received),
                      item);

    menu_changed (item);
    tooltip_changed (item);
}

static void
initialize_item (SnItem *item)
{
    item->status_icon = xapp_status_icon_new ();

    g_dbus_proxy_new (g_dbus_proxy_get_connection (item->sn_item_proxy),
                      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                      NULL,
                      g_dbus_proxy_get_name (item->sn_item_proxy),
                      g_dbus_proxy_get_object_path (item->sn_item_proxy),
                      "org.freedesktop.DBus.Properties",
                      NULL,
                      property_proxy_acquired,
                      item);
}

SnItem *
sn_item_new (GDBusProxy *sn_item_proxy,
             gboolean    is_ai)
{
    SnItem *item = g_object_new (sn_item_get_type (), NULL);

    item->sn_item_proxy = sn_item_proxy;
    item->is_ai = is_ai;

    initialize_item (item);
    return item;
}