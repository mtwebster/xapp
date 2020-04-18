
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

typedef enum
{
    STATUS_PASSIVE,
    STATUS_ACTIVE,
    STATUS_NEEDS_ATTENTION
} Status;

struct _SnItem
{
    GObject parent_instance;

    GDBusProxy *sn_item_proxy; // SnItemProxy
    GDBusProxy *prop_proxy; // dbus properties (we can't trust SnItemProxy)

    GtkWidget *menu;
    XAppStatusIcon *status_icon;

    Status status;
    gchar *last_png_path;
    gchar *png_path;

    gint current_icon_id;

    gboolean is_ai;
};

G_DEFINE_TYPE (SnItem, sn_item, G_TYPE_OBJECT)

static void update_menu (SnItem *item);
static void update_status (SnItem *item);
static void update_tooltip (SnItem *item);
static void update_icon (SnItem *item);

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

static GVariant *
get_property (SnItem      *item,
              const gchar *prop_name)
{
    GVariant *var, *box, *result;
    GError *error = NULL;

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

    g_variant_unref (box);
    g_variant_unref (var);

    return result;
}

static gchar *
get_string_property (SnItem               *item,
                     const gchar          *name)
{
    GVariant *var_result = NULL;
    gchar *result = NULL;

    var_result = get_property (item, name);

    if (var_result == NULL)
    {
        return NULL;
    }

    result = g_variant_dup_string (var_result, NULL);

    g_variant_unref (var_result);

    return result;
}

static void
set_icon_from_pixmap (SnItem *item)
{

}

static gchar *
construct_filename (const gchar *icon_theme_path,
                    const gchar *icon_name,
                    const gchar *extension)
{
    gchar *basename = g_strdup_printf ("%s.%s", icon_name, extension);
    gchar *full_path = g_build_path ("/", icon_theme_path, basename, NULL);

    g_free (basename);

    return full_path;
}

static void
process_icon_name (SnItem *item,
                   const gchar *icon_theme_path,
                   const gchar *icon_name)
{
    if (g_path_is_absolute (icon_name) || !icon_theme_path)
    {
        xapp_status_icon_set_icon_name (item->status_icon, icon_name);
    }
    else
    {
        gchar *filename = construct_filename (icon_theme_path, icon_name, "png");

        if (!g_file_test (filename, G_FILE_TEST_EXISTS))
        {
            g_free (filename);

            filename = construct_filename (icon_theme_path, icon_name, "svg");

            if (!g_file_test (filename, G_FILE_TEST_EXISTS))
            {
                g_warning ("No valid images found at theme path: %s (icon name: %s)",
                           icon_theme_path, icon_name);
            }
        }

        xapp_status_icon_set_icon_name (item->status_icon, filename);
        g_free (filename);
    }
}

static void
set_icon_name_or_path (SnItem *item,
                       const gchar *icon_theme_path,
                       const gchar *icon_name,
                       const gchar *att_icon_name,
                       const gchar *olay_icon_name)
{
    const gchar *name_to_use = NULL;

    if (item->status == STATUS_ACTIVE)
    {
        if (icon_name)
        {
            name_to_use = icon_name;
        }
    }
    else if (item->status == STATUS_NEEDS_ATTENTION)
    {
        if (att_icon_name)
        {
            name_to_use = att_icon_name;
        }
        else
        if (icon_name)
        {
            name_to_use = icon_name;
        }
    }

    if (name_to_use == NULL)
    {
        name_to_use = "image-missing";
    }

    process_icon_name (item, icon_theme_path, name_to_use);
}

static void
update_icon (SnItem *item)
{
    gchar *icon_theme_path;
    gchar *icon_name, *att_icon_name, *olay_icon_name;

    icon_theme_path = get_string_property (item, "IconThemePath");

    icon_name = get_string_property (item, "IconName");
    att_icon_name = get_string_property (item, "AttentionIconName");
    olay_icon_name = get_string_property (item, "OverlayIconName");

    if (icon_name || att_icon_name || olay_icon_name)
    {
        if (g_strcmp0 (icon_theme_path, "") == 0)
        {
            g_clear_pointer (&icon_theme_path, g_free);
        }

        set_icon_name_or_path (item,
                               icon_theme_path,
                               icon_name,
                               att_icon_name,
                               olay_icon_name);
    }
    else
    {
        set_icon_from_pixmap (item);
    }

    g_free (icon_theme_path);
    g_free (icon_name);
    g_free (att_icon_name);
    g_free (olay_icon_name);
}

static void
update_menu (SnItem *item)
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
update_tooltip (SnItem *item)
{
    gchar *text;

    if (item->is_ai)
    {
        text = get_string_property (item, "XAyatanaLabel");
    }
    else
    {
        // TODO: text = get_string_property (item, "ToolTip");
        text = NULL;
    }

    xapp_status_icon_set_tooltip_text (item->status_icon, text ? text : "");

    g_free (text);
}

static void
update_status (SnItem *item)
{
    Status old_status;
    gchar *status;

    old_status = item->status;

    status = get_string_property (item, "Status");

    if (g_strcmp0 (status, "Passive") == 0)
    {
        item->status = STATUS_PASSIVE;
        xapp_status_icon_set_visible (item->status_icon, FALSE);
    }
    else if (g_strcmp0 (status, "NeedsAttention") == 0)
    {
        item->status = STATUS_NEEDS_ATTENTION;
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }
    else
    {
        item->status = STATUS_ACTIVE;
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }

    if (old_status != item->status)
    {
        update_icon (item);
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
        update_icon (item);
    }
    else
    if (g_strcmp0 (signal_name, "NewStatus") == 0)
    {
        update_status (item); // This will update_icon(item) also.
    }
    else
    if (g_strcmp0 (signal_name, "NewMenu") == 0)
    {
        update_menu (item);
    }
    else
    if (g_strcmp0 (signal_name, "XAyatanaNewLabel") ||
        g_strcmp0 (signal_name, "Tooltip"))
    {
        update_tooltip (item);
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

    update_status (item);
    update_menu (item);
    update_tooltip (item);
    update_icon (item);
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