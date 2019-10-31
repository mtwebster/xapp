
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n-lib.h>

#include "xapp-favorites-manager.h"

#define FAVORITES_PATH "xapps/xapp-favorites.json"
#define NO_POSITION -1

G_DEFINE_BOXED_TYPE (XAppFavoriteInfo, xapp_favorite_info, xapp_favorite_info_copy, xapp_favorite_info_free);

XAppFavoriteInfo *
xapp_favorite_info_copy (const XAppFavoriteInfo *info)
{
    g_debug ("XAppFavoriteInfo: copy:");

    XAppFavoriteInfo *_info = g_slice_dup (XAppFavoriteInfo, info);
    _info->uri = g_strdup (info->uri);
    _info->mimetype = g_strdup (info->mimetype);
    _info->index = info->index;

    return _info;
}

void
xapp_favorite_info_free (XAppFavoriteInfo *info)
{
    g_debug ("XAppFavoriteInfo free (%s)", info->uri);

    g_free (info->uri);
    g_free (info->mimetype);
    g_slice_free (XAppFavoriteInfo, info);
}

/*******************************************************************************/

/**
 * SECTION:xapp-favorites-manager
 * @Short_description: Broadcasts status information over DBUS
 * @Title: XAppFavoritesManager
 *
 * The XAppFavoritesManager allows applications to share status info
 * about themselves. It replaces the obsolete and very similar
 * Gtk.FavoritesManager widget.
 *
 * If used in an environment where no applet is handling XAppFavoritesManagers,
 * the XAppFavoritesManager delegates its calls to a Gtk.FavoritesManager.
 */

typedef struct
{
    GHashTable *items_by_order;
    GHashTable *items_by_uri;

    gchar *path;
} XAppFavoritesManagerPrivate;

struct _XAppFavoritesManager
{
    GObject parent_instance;
    XAppFavoritesManagerPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppFavoritesManager, xapp_favorites_manager, G_TYPE_OBJECT)

enum
{
    CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

static GList *
list_all_favorites (XAppFavoritesManager *manager)
{
    XAppFavoritesManagerPrivate *priv = manager->priv;

    GList *ret = NULL;

    gint i;

    for (i = 0; i < g_hash_table_size (priv->items_by_order); i++)
    {
        gpointer data = g_hash_table_lookup (priv->items_by_order, GINT_TO_POINTER (i));

        if (data)
        {
            ret = g_list_insert (ret, xapp_favorite_info_copy (data), 0);
        }
    }

    if (ret)
    {
        ret = g_list_reverse (ret);
    }

    return ret;
}


static void
move_favorite (XAppFavoritesManager *manager,
               XAppFavoriteInfo     *move_info,
               gint                  new_index)
{
    XAppFavoritesManagerPrivate *priv = manager->priv;
    GHashTable *new_ordered_table, *old_ordered_table;
    gint old_table_counter, new_table_counter;

    if (new_index == move_info->index)
    {
        g_debug ("XAppFavoritesManager: move_favorite, info for '%s' is already at requested position (%d)",
                 move_info->uri, new_index);
        return;
    }

    new_ordered_table = g_hash_table_new (g_direct_hash,
                                          g_direct_equal);

    g_hash_table_insert (new_ordered_table, GINT_TO_POINTER (new_index), move_info);
    move_info->index = new_index;

    new_table_counter = 0;

    for (old_table_counter = 0; old_table_counter < g_hash_table_size (priv->items_by_order); old_table_counter++)
    {
        gpointer data = g_hash_table_lookup (priv->items_by_order, GINT_TO_POINTER (old_table_counter));

        XAppFavoriteInfo *info = (XAppFavoriteInfo *) data;

        if (move_info == info)
        {
            continue;
        }

        if (new_table_counter == new_index)
        {
            new_table_counter++;
            old_table_counter--;
            continue;
        }

        info->index = new_table_counter;
        g_hash_table_insert (new_ordered_table, GINT_TO_POINTER (new_table_counter), info);

        new_table_counter++;
    }

    old_ordered_table = priv->items_by_order;
    priv->items_by_order = new_ordered_table;
    g_hash_table_unref (old_ordered_table);
}

static void
delete_favorite (XAppFavoritesManager *manager,
                 XAppFavoriteInfo     *delete_info)
{
    XAppFavoritesManagerPrivate *priv = manager->priv;
    gint end_index;

    // Move the info to the end of the sequence then trim it off
    end_index = g_hash_table_size (priv->items_by_uri) - 1;

    move_favorite (manager, delete_info, end_index);

    g_hash_table_remove (priv->items_by_order, GINT_TO_POINTER (end_index));
    g_hash_table_remove (priv->items_by_uri, delete_info->uri);
}

static void
add_favorite_info (XAppFavoritesManager *manager,
                   const gchar          *uri,
                   const gchar          *mimetype,
                   gint                  index)
{
    XAppFavoritesManagerPrivate *priv = manager->priv;
    XAppFavoriteInfo *info;
    gpointer existing_info;

    existing_info = g_hash_table_lookup (priv->items_by_uri, uri);

    if (existing_info)
    {
        g_debug ("XAppFavoritesManager: favorite for '%s' exists, ignoring", uri);
        return;
    }

    info = g_slice_new0 (XAppFavoriteInfo);
    info->uri = g_strdup (uri);
    info->mimetype = g_strdup (mimetype);

    if (index != NO_POSITION)
    {
        info->index = index;
    }
    else
    {
        info->index = g_hash_table_size (priv->items_by_order);
    }

    g_hash_table_insert (priv->items_by_uri, g_strdup (uri), info);
    g_hash_table_insert (priv->items_by_order, GINT_TO_POINTER (info->index), info);

    g_debug ("XAppFavoritesManager: added favorite: %s", uri);
}

static void
on_json_file_written (GObject      *source,
                      GAsyncResult *res,
                      gpointer      user_data)
{
    XAppFavoritesManager *manager = XAPP_FAVORITES_MANAGER (user_data);
    GFile *file = G_FILE (source);
    GError *error;
    error = NULL;

    if (!g_file_replace_contents_finish (file, res, NULL, &error))
    {
        if (error)
        {
            g_debug ("XAppFavoritesManager: could not write json file: %s", error->message);
            g_error_free (error);
        }
    }

    g_object_unref (file);

    g_debug ("XAppFavoritesManager: json file written");
    g_signal_emit (manager, signals[CHANGED], 0);
}

static void
save_favorites_to_file (XAppFavoritesManager *manager)
{
    XAppFavoritesManagerPrivate *priv = manager->priv;
    JsonNode *root_node;
    JsonGenerator *generator;
    JsonBuilder *builder;
    GFile *file;
    GBytes *content_bytes;
    gchar *contents;
    gsize length;
    gint i;

    builder = json_builder_new ();

    json_builder_begin_object (builder);

    for (i = 0; i < g_hash_table_size (priv->items_by_order); i++)
    {
        gpointer data;

        data = g_hash_table_lookup (priv->items_by_order, GINT_TO_POINTER (i));

        if (data)
        {
            XAppFavoriteInfo *info = (XAppFavoriteInfo *) data;
            g_printerr ("%d, %s\n", info->index, info->uri);

            json_builder_set_member_name (builder, info->uri);
            json_builder_begin_object (builder);

            json_builder_set_member_name (builder, "mimetype");
            json_builder_add_string_value (builder, info->mimetype);

            json_builder_set_member_name (builder, "index");
            json_builder_add_int_value (builder, info->index);

            json_builder_end_object (builder);
        }
    }

    json_builder_end_object (builder);
    root_node = json_builder_get_root (builder);

    generator = json_generator_new ();
    json_generator_set_pretty (generator, TRUE);

    json_generator_set_root (generator, root_node);

    contents = json_generator_to_data (generator, &length);
    content_bytes = g_bytes_new_take ((gpointer) contents, length);

    file = g_file_new_for_path (priv->path);

    g_file_replace_contents_bytes_async (file,
                                         content_bytes,
                                         NULL,
                                         FALSE,
                                         G_FILE_CREATE_NONE,
                                         NULL,
                                         on_json_file_written,
                                         manager);

    g_object_unref (builder);
    g_object_unref (generator);
    g_bytes_unref (content_bytes);
}

static void
load_json_from_contents (XAppFavoritesManager *manager,
                         const gchar          *contents,
                         gsize                 length)
{
    XAppFavoritesManagerPrivate *priv = manager->priv;
    GError *error;
    JsonParser *parser;
    JsonObjectIter favorite_iter;
    JsonNode *root, *favorite_node;
    JsonObject *root_object;
    const gchar *favorite_name;

    parser = json_parser_new ();

    if (!json_parser_load_from_data (parser,
                                     contents,
                                     length,
                                     &error))
    {
        if (error != NULL)
        {
            g_debug ("XAppFavoritesManager: could not parse favorites file (%s): %s", priv->path, error->message);
        }

        g_error_free (error);
        g_object_unref (parser);

        return;
    }

    root = json_parser_steal_root (parser);
    g_object_unref (parser);

    root_object = json_node_get_object (root);

    json_object_iter_init (&favorite_iter, root_object);
    while (json_object_iter_next (&favorite_iter, &favorite_name, &favorite_node))
    {
        JsonObject *details_object;
        JsonObjectIter details_iter;
        JsonNode *detail_node;
        const gchar *detail_name;
        gchar *mimetype;
        gint index;

        if (!JSON_NODE_HOLDS_OBJECT (favorite_node))
        {
            g_debug ("XAppFavoritesManager: uri node must contain an object (node name: %s)",
                     favorite_name);

            continue;
        }

        details_object = json_node_get_object (favorite_node);

        mimetype = NULL;

        json_object_iter_init (&details_iter, details_object);
        while (json_object_iter_next (&details_iter, &detail_name, &detail_node))
        {
            if (!JSON_NODE_HOLDS_VALUE (detail_node))
            {
                g_debug ("XAppFavoritesManager: ignoring invalid data (uri: %s, node name: %s",
                         favorite_name, detail_name);

                continue;
            }

            if (g_strcmp0 (detail_name, "mimetype") == 0)
            {
                mimetype = json_node_dup_string (detail_node);
            }

            if (g_strcmp0 (detail_name, "index") == 0)
            {
                index = json_node_get_int (detail_node);
            }
        }

        add_favorite_info (manager, favorite_name, (const gchar *) mimetype, index);

        g_free (mimetype);
    }

    g_debug ("XAppFavoritesManager: favorites loaded, sending changed.");

    g_signal_emit (manager, signals[CHANGED], 0);
}

static void
on_favorites_file_contents_read (GObject      *source,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
    XAppFavoritesManager *manager = XAPP_FAVORITES_MANAGER (user_data);
    XAppFavoritesManagerPrivate *priv = manager->priv;
    GError *error;
    gchar *contents;
    gsize length;

    error = NULL;
    contents = NULL;

    if (!g_file_load_contents_finish (G_FILE (source),
                                      res,
                                      &contents,
                                      &length,
                                      NULL,
                                      &error))
    {
        if (error != NULL)
        {
            if (error->code == G_FILE_ERROR_NOENT)
            {
                g_debug ("XAppFavoritesManager: could not read favorites file (%s): %s", priv->path, error->message);
            }

            g_free (contents);
            g_error_free (error);
        }
    }

    g_object_unref (G_OBJECT (source));

    if (!contents)
    {
        return;
    }

    load_json_from_contents (manager, contents, length);

    g_free (contents);
}

static void
on_content_type_info_received (GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    XAppFavoritesManager *manager = XAPP_FAVORITES_MANAGER (user_data);
    GFile *file;
    GFileInfo *file_info;
    GError *error;
    gchar *mimetype, *uri;

    file = G_FILE (source);
    uri = g_file_get_uri (file);
    error = NULL;
    mimetype = NULL;

    file_info = g_file_query_info_finish (file, res, &error);

    if (error)
    {
        g_debug ("XAppFavoritesManager: problem trying to figure out content type for uri '%s': %s",
                 uri, error->message);
        g_error_free (error);
    }

    if (file_info)
    {
        mimetype = g_strdup (g_file_info_get_content_type (file_info));

        add_favorite_info (manager,
                           uri,
                           mimetype,
                           NO_POSITION);
    }

    g_free (uri);
    g_free (mimetype);
    g_clear_object (&file_info);
    g_object_unref (file);

    save_favorites_to_file (manager);
}


static void
xapp_favorites_manager_init (XAppFavoritesManager *self)
{
    GFile *file;

    self->priv = xapp_favorites_manager_get_instance_private (self);

    g_debug ("XAppFavoritesManager: init:");

    self->priv->path = g_build_filename (g_get_user_config_dir (),
                                         FAVORITES_PATH,
                                         NULL);

    // items_by_uri will be the 'owner' of keys and data
    self->priv->items_by_uri = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      (GDestroyNotify) g_free,
                                                      (GDestroyNotify) xapp_favorite_info_free);

    self->priv->items_by_order = g_hash_table_new (g_direct_hash,
                                                   g_direct_equal);

    file = g_file_new_for_path (self->priv->path);

    g_file_load_contents_async (file,
                                NULL,
                                (GAsyncReadyCallback) on_favorites_file_contents_read,
                                self);
}


static void
xapp_favorites_manager_dispose (GObject *object)
{
    g_debug ("XAppFavoritesManager dispose (%p)", object);

    G_OBJECT_CLASS (xapp_favorites_manager_parent_class)->dispose (object);
}

static void
xapp_favorites_manager_finalize (GObject *object)
{
    XAppFavoritesManager *self = XAPP_FAVORITES_MANAGER (object);
    g_debug ("XAppFavoritesManager finalize (%p)", object);

    g_clear_pointer (&self->priv->path, g_free);

    g_hash_table_unref (self->priv->items_by_order);
    g_hash_table_unref (self->priv->items_by_uri);

    G_OBJECT_CLASS (xapp_favorites_manager_parent_class)->finalize (object);
}

static void
xapp_favorites_manager_class_init (XAppFavoritesManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_favorites_manager_dispose;
    gobject_class->finalize = xapp_favorites_manager_finalize;

    /**
     * XAppFavoritesManager::changed:

     * list changed
     */
    signals [CHANGED] =
        g_signal_new ("changed",
                      XAPP_TYPE_FAVORITES_MANAGER,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

/**
 * xapp_favorites_manager_get_default:
 *
 * Returns the #XAppFavoritesManager instance, creating one if necessary
 *
 * Returns: (transfer none): the XAppFavoritesManager.
 *
 * Since: 1.6
 */
XAppFavoritesManager *
xapp_favorites_manager_get_default (void)
{
    static XAppFavoritesManager *manager = NULL;

    if (G_UNLIKELY (!manager))
    {
        manager = g_object_new (XAPP_TYPE_FAVORITES_MANAGER, NULL);
    }

    return manager;
}

/**
 * xapp_favorites_manager_get_favorites:
 * @manager: The #XAppFavoritesManager
 * @mimetype: (nullable): The mimetype to filter by for results
 *
 * The mimetype and group arguments are mutually exclusive - you can use one or the other
 * If both are %NULL, the full list will be returned.
 *
 * In any case, the list will be ordered according to #FileInfo:index.
 *
 * Returns: (element-type XAppFavoriteInfo) (transfer full): a list of XAppFavoriteInfos.
            Free the list with g_list_free, free elements with g_object_unref.
 *
 * Since: 1.6
 */
GList *
xapp_favorites_manager_get_favorites (XAppFavoritesManager *manager,
                                      const gchar          *mimetype)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES_MANAGER (manager), NULL);
    XAppFavoritesManagerPrivate *priv = manager->priv;
    GList *ret = NULL;

    if (!mimetype)
    {
        g_debug ("XAppFavoritesManager: get_favorites returning full list (%d items)",
                 g_hash_table_size (priv->items_by_order));

        return list_all_favorites (manager);
    }

    GHashTableIter iter;
    gpointer key, data;

    g_hash_table_iter_init (&iter, priv->items_by_order);
    while (g_hash_table_iter_next (&iter, &key, &data))
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) data;

        if (g_strcmp0 (mimetype, info->mimetype) == 0)
        {
            ret = g_list_insert (ret, xapp_favorite_info_copy (data), 0);
        }
    }

    ret = g_list_reverse (ret);

    g_debug ("XAppFavoritesManager: get_favorites returning filtered list (%d items for mimetype: %s)",
             g_list_length (ret), mimetype);

    return ret;
}

/**
 * xapp_favorites_manager_add:
 * @manager: The #XAppFavoritesManager
 * @uri: The uri the favorite is for
 *
 * Adds a new favorite.  If the uri already exists, this does nothing.
 *
 * Since: 1.6
 */
void
xapp_favorites_manager_add (XAppFavoritesManager *manager,
                            const gchar          *uri)
{
    g_return_if_fail (XAPP_IS_FAVORITES_MANAGER (manager));
    g_return_if_fail (uri != NULL);
    GFile *file;

    file = g_file_new_for_uri (uri);

    g_file_query_info_async (file,
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_LOW,
                             NULL,
                             on_content_type_info_received,
                             manager);
}

/**
 * xapp_favorites_manager_delete:
 * @manager: The #XAppFavoritesManager
 * @uri: The uri for the favorite being removed
 *
 * Since: 1.6
 */
void
xapp_favorites_manager_delete (XAppFavoritesManager *manager,
                               const gchar          *uri)
{
    g_return_if_fail (XAPP_IS_FAVORITES_MANAGER (manager));
    g_return_if_fail (uri != NULL);
    XAppFavoritesManagerPrivate *priv = manager->priv;
    gpointer data;

    data = g_hash_table_lookup (priv->items_by_uri, uri);

    if (!data)
    {
        g_debug ("XAppFavoritesManager: remove - couldn't find existing favorite for uri '%s'", uri);
        return;
    }

    delete_favorite (manager, (XAppFavoriteInfo *) data);

    save_favorites_to_file (manager);
}

/**
 * xapp_favorites_manager_move:
 * @manager: The #XAppFavoritesManager
 * @uri: The uri for the favorite being moved
 * @index: The new position to move the favorite to.
 *
 * Since: 1.6
 */
void
xapp_favorites_manager_move (XAppFavoritesManager *manager,
                             const gchar          *uri,
                             gint                  index)
{
    g_return_if_fail (XAPP_IS_FAVORITES_MANAGER (manager));
    g_return_if_fail (uri != NULL);
    XAppFavoritesManagerPrivate *priv = manager->priv;
    gpointer data;

    index = CLAMP (index, 0, g_hash_table_size (priv->items_by_uri) - 1);

    data = g_hash_table_lookup (priv->items_by_uri, uri);

    if (!data)
    {
        g_debug ("XAppFavoritesManager: reorder - couldn't find existing favorite for uri '%s'", uri);
        return;
    }
    g_debug ("XAppFavoritesManager: reorder - moving '%s' to index %d", uri, index);

    move_favorite (manager, (XAppFavoriteInfo *) data, index);

    save_favorites_to_file (manager);
}

