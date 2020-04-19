#include <stdlib.h>
#include <gtk/gtk.h>

#include <libxapp/xapp-status-icon.h>
#include <glib-unix.h>

#include "sn-watcher-interface.h"
#include "sn-item-interface.h"
#include "sn-item.h"

#define XAPP_TYPE_SN_WATCHER xapp_sn_watcher_get_type ()
G_DECLARE_FINAL_TYPE (XAppSnWatcher, xapp_sn_watcher, XAPP, SN_WATCHER, GtkApplication)

struct _XAppSnWatcher
{
    GtkApplication parent_instance;

    SnWatcherInterface *skeleton;
    GDBusConnection *connection;

    guint owner_id;
    guint listener_id;

    GHashTable *items;

    gboolean shutdown_pending;
};

G_DEFINE_TYPE (XAppSnWatcher, xapp_sn_watcher, GTK_TYPE_APPLICATION)

#define NOTIFICATION_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define NOTIFICATION_WATCHER_PATH "/StatusNotifierWatcher"
#define STATUS_ICON_MONITOR_PREFIX "org.x.StatusIconMonitor"

#define FDO_DBUS_NAME "org.freedesktop.DBus"
#define FDO_DBUS_PATH "/org/freedesktop/DBus"

#define STATUS_ICON_MONITOR_MATCH "org.x.StatusIconMonitor"
#define APPINDICATOR_PATH_PREFIX "/org/ayatana/NotificationItem/"

static void
handle_name_owner_appeared (XAppSnWatcher *watcher,
                            const gchar   *name,
                            const gchar   *old_owner)
{
    if (g_str_has_prefix (name, STATUS_ICON_MONITOR_PREFIX))
    {
        if (watcher->shutdown_pending)
        {
            g_debug ("A monitor appeared on the bus, cancelling shutdown\n");

            watcher->shutdown_pending = FALSE;
            g_application_hold (G_APPLICATION (watcher));
        }
    }
}

static void
handle_name_owner_lost (XAppSnWatcher *watcher,
                        const gchar   *name,
                        const gchar   *old_owner)
{
    GList *keys, *l;

    keys = g_hash_table_get_keys (watcher->items);
    g_printerr ("owner lost\n");
    for (l = keys; l != NULL; l = l->next)
    {
        const gchar *key = l->data;

        if (g_str_has_prefix (key, name))
        {
            g_debug ("Client %s has exited, removing status icon", key);
            g_hash_table_remove (watcher->items, key);
            break;
        }
    }

    g_list_free (keys);

    if (g_str_has_prefix (name, STATUS_ICON_MONITOR_PREFIX))
    {
        g_debug ("Lost a monitor, checking for any more");

        if (xapp_status_icon_any_monitors ())
        {
            g_debug ("Still have a monitor, continuing");

            return;
        }
        else
        {
            g_debug ("Lost our last monitor, starting countdown\n");

            g_application_release (G_APPLICATION (watcher));
        }
    }
}

static void
name_owner_changed (GDBusConnection *connection,
                    const gchar     *sender_name,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    g_debug("XAppSnWatcher: NameOwnerChanged signal received");

    if (g_strcmp0 (signal_name, "NameOwnerChanged") == 0)
    {
        gchar **args = g_variant_dup_strv (parameters, NULL);

        if (!args)
        {
            return;
        }

        if (g_strcmp0 (args[2], "") == 0)
        {
            handle_name_owner_lost (watcher, args[0], args[1]);
        }
        else
        {
            handle_name_owner_appeared (watcher, args[0], args[2]);
        }

        g_strfreev (args);
    }

}

static void
add_name_listener (XAppSnWatcher *watcher)
{
    g_debug ("XAppSnWatcher: Adding NameOwnerChanged listener for status monitor existence");

    watcher->listener_id = g_dbus_connection_signal_subscribe (watcher->connection,
                                                               FDO_DBUS_NAME,
                                                               FDO_DBUS_NAME,
                                                               "NameOwnerChanged",
                                                               FDO_DBUS_PATH,
                                                               STATUS_ICON_MONITOR_MATCH,
                                                               G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE,
                                                               name_owner_changed,
                                                               watcher,
                                                               NULL);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    g_debug ("Lost StatusNotifierWatcher name (maybe something replaced us), exiting immediately");
    g_application_quit (G_APPLICATION (watcher));
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    g_debug ("Name acquired on dbus");
}

static gboolean
handle_register_host (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
    // Nothing to do - we wouldn't be here if there wasn't a host (status applet)
    sn_watcher_interface_complete_register_status_notifier_host (skeleton,
                                                                 invocation);

    return TRUE;
}

static void
populate_published_list (const gchar *key,
                         gpointer     item,
                         GPtrArray   *array)
{
    g_ptr_array_add (array, g_strdup (key));
}

static void
update_published_items (XAppSnWatcher *watcher)
{
    GPtrArray *array;
    gpointer as;

    array = g_ptr_array_new ();

    g_hash_table_foreach (watcher->items, (GHFunc) populate_published_list, array);
    g_ptr_array_add (array, NULL);

    as = g_ptr_array_free (array, FALSE);

    sn_watcher_interface_set_registered_status_notifier_items (watcher->skeleton,
                                                               (const gchar * const *) as);

    g_strfreev ((gchar **) as);

    g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
}

static gboolean
create_key (const gchar  *sender,
            const gchar  *service,
            gchar       **key,
            gchar       **bus_name,
            gchar       **path)
{
    gchar *temp_key, *temp_bname, *temp_path;

    temp_key = temp_bname = temp_path = NULL;
    *key = *bus_name = *path = NULL;

    if (g_str_has_prefix (service, "/"))
    {
        temp_bname = g_strdup (sender);
        temp_path = g_strdup (service);
    }
    else
    {
        temp_bname = g_strdup (service);
        temp_path = g_strdup ("/StatusNotifierItem");
    }

    if (!g_dbus_is_name (temp_bname))
    {
        g_free (temp_bname);
        g_free (temp_path);

        return FALSE;
    }

    temp_key = g_strdup_printf ("%s%s", temp_bname, temp_path);

    g_debug ("Key: '%s', busname '%s', path '%s'", temp_key, temp_bname, temp_path);

    *key = temp_key;
    *bus_name = temp_bname;
    *path = temp_path;

    return TRUE;
}

static gboolean
handle_register_item (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
    SnItem *item;
    GError *error;
    const gchar *sender;
    g_autofree gchar *key, *bus_name, *path;

    sender = g_dbus_method_invocation_get_sender (invocation);

    if (!create_key (sender, service, &key, &bus_name, &path))
    {
        error = g_error_new (g_dbus_error_quark (),
                             G_DBUS_ERROR_INVALID_ARGS,
                             "Invalid bus name from: %s, %s", service, sender);
        g_dbus_method_invocation_return_gerror (invocation, error);

        return FALSE;
    }

    item = g_hash_table_lookup (watcher->items, key);

    if (item == NULL)
    {
        SnItemInterface *proxy;
        error = NULL;
        g_debug ("Key: '%s'", key);

        proxy = sn_item_interface_proxy_new_sync (watcher->connection,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  bus_name,
                                                  path,
                                                  NULL,
                                                  &error);

        if (error != NULL)
        {
            g_debug ("Could not create new status notifier proxy item for item at %s: %s", bus_name, error->message);

            g_dbus_method_invocation_return_gerror (invocation, error);

            return FALSE;
        }

        item = sn_item_new ((GDBusProxy *) proxy,
                            g_str_has_prefix (path, APPINDICATOR_PATH_PREFIX));

        g_hash_table_insert (watcher->items,
                             g_strdup (key),
                             item);

        update_published_items (watcher);

        sn_watcher_interface_emit_status_notifier_item_registered (skeleton,
                                                                   service);
    }

    sn_watcher_interface_complete_register_status_notifier_item (skeleton,
                                                                 invocation);

    return TRUE;
}

static gboolean
export_watcher_interface (XAppSnWatcher *watcher)
{
    GError *error = NULL;

    if (watcher->skeleton) {
        return TRUE;
    }

    watcher->skeleton = sn_watcher_interface_skeleton_new ();

    g_debug ("XAppSnWatcher: exporting StatusNotifierWatcher dbus interface to %s", NOTIFICATION_WATCHER_PATH);

    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (watcher->skeleton),
                                      watcher->connection,
                                      NOTIFICATION_WATCHER_PATH,
                                      &error);

    if (error != NULL) {
        g_critical ("XAppSnWatcher: could not export StatusNotifierWatcher interface: %s", error->message);
        g_error_free (error);

        return FALSE;
    }

    g_signal_connect (watcher->skeleton,
                      "handle-register-status-notifier-item",
                      G_CALLBACK (handle_register_item),
                      watcher);

    g_signal_connect (watcher->skeleton,
                      "handle-register-status-notifier-host",
                      G_CALLBACK (handle_register_host),
                      watcher);

    return TRUE;
}

static gboolean
on_interrupt (XAppSnWatcher *watcher)
{
    g_debug ("SIGINT - shutting down immediately");

    g_application_quit (G_APPLICATION (watcher));
    return FALSE;
}

static void
continue_startup (XAppSnWatcher *watcher)
{
    GError *error = NULL;

    g_debug ("Trying to acquire session bus connection");

    watcher->connection = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                          NULL,
                                          &error);

    if (error != NULL)
    {
        g_critical ("Could not open connection, exiting: %s\n", error->message);
        g_error_free (error);

        g_application_quit (G_APPLICATION (watcher));
    }

    g_unix_signal_add (SIGINT, (GSourceFunc) on_interrupt, watcher);
    g_application_hold (G_APPLICATION (watcher));

    add_name_listener (watcher);
    export_watcher_interface (watcher);

    watcher->owner_id = g_bus_own_name_on_connection (watcher->connection,
                                                      NOTIFICATION_WATCHER_NAME,
                                                      G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                                      on_name_acquired,
                                                      on_name_lost,
                                                      watcher,
                                                      NULL);
}

static void
watcher_startup (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher*) application;
    G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->startup (application);

    watcher->items = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_object_unref);

    /* This buys us 30 seconds (gapp timeout) - we'll either be re-held immediately
     * because there's a monitor or exit after the 30 seconds. */
    g_application_hold (application);
    g_application_release (application);

    if (xapp_status_icon_any_monitors ())
    {
        continue_startup (watcher);
    }
    else
    {
        g_printerr("No active monitors, exiting in 30s\n");
        watcher->shutdown_pending = TRUE;
    }
}

static gint
watcher_command_line (GApplication            *application,
                      GApplicationCommandLine *command_line)
{
 
    // def do_command_line(self, command_line):
        // options = command_line.get_options_dict()
        // options = options.end().unpack()

        // if "quit" in options:
        //     if self.watcher != None:
        //         print("Shutting down the XApp StatusNotifierWatcher")
        //         self.shutdown()
        //     else:
        //         print("XApp StatusNotifierWatcher not running")
        //         exit(0)

    return 0;
}

static void
watcher_activate (GApplication *application)
{
}

static void
watcher_open (GApplication  *application,
                GFile        **files,
                gint           n_files,
                const gchar   *hint)
{
}

static void
watcher_finalize (GObject *object)
{
  G_OBJECT_CLASS (xapp_sn_watcher_parent_class)->finalize (object);
}

static void
watcher_shutdown (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher *) application;

    if (watcher->listener_id > 0)
    {
        g_dbus_connection_signal_unsubscribe (watcher->connection, watcher->listener_id);
        watcher->listener_id = 0;
    }

    g_clear_pointer (&watcher->items, g_hash_table_unref);

    if (watcher->owner_id > 0)
    {
        g_bus_unown_name (watcher->owner_id);
    }

    if (watcher->skeleton != NULL)
    {
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (watcher->skeleton));
        g_clear_object (&watcher->skeleton);
    }

    g_clear_object (&watcher->connection);

    G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->shutdown (application);
}

static void
xapp_sn_watcher_init (XAppSnWatcher *app)
{
}

static void
xapp_sn_watcher_class_init (XAppSnWatcherClass *class)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  application_class->startup = watcher_startup;
  application_class->shutdown = watcher_shutdown;
  application_class->activate = watcher_activate;
  application_class->open = watcher_open;
  application_class->command_line = watcher_command_line;

  object_class->finalize = watcher_finalize;
}

XAppSnWatcher *
watcher_new (void)
{
  XAppSnWatcher *watcher;

  g_set_application_name ("xapp-sn-watcher");

  watcher = g_object_new (xapp_sn_watcher_get_type (),
                          "application-id", "org.x.StatusNotifierWatcher",
                          "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                          "inactivity-timeout", 30000,
                          "register-session", TRUE,
                          NULL);

  return watcher;
}

int
main (int argc, char **argv)
{
  XAppSnWatcher *watcher;
  int status;

  watcher = watcher_new ();

  status = g_application_run (G_APPLICATION (watcher), argc, argv);

  g_object_unref (watcher);

  return status;
}
