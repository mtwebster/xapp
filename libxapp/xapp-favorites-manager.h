#ifndef __XAPP_FAVORITES_MANAGER_H__
#define __XAPP_FAVORITES_MANAGER_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_FAVORITES_MANAGER            (xapp_favorites_manager_get_type ())

G_DECLARE_FINAL_TYPE (XAppFavoritesManager, xapp_favorites_manager, XAPP, FAVORITES_MANAGER, GObject)

XAppFavoritesManager *xapp_favorites_manager_new                    (void);
XAppFavoritesManager *xapp_favorites_manager_get_default            (void);
GList *               xapp_favorites_manager_get_favorites          (XAppFavoritesManager *manager,
                                                                     const gchar          *mimetype);
void                  xapp_favorites_manager_add                    (XAppFavoritesManager *manager,
                                                                     const gchar          *uri);
void                  xapp_favorites_manager_delete                 (XAppFavoritesManager *manager,
                                                                     const gchar          *uri);
void                  xapp_favorites_manager_move                   (XAppFavoritesManager *manager,
                                                                     const gchar          *uri,
                                                                     gint                  index);


#define XAPP_TYPE_FAVORITE_INFO (xapp_favorite_info_get_type ())
typedef struct _XAppFavoriteInfo XAppFavoriteInfo;

/**
 * XAppFavoriteInfo:
 * @uri: The uri to the favorite file.
 * @mimetype: The mimetype for the file.
 * @label: The display label for the file.
 * @index: The index of the item within the list of favorites.
 *
 * Information related to a single favorite file.
 */
struct _XAppFavoriteInfo
{
    gchar *uri; // mandatory
    gchar *mimetype; // mandatory

    gint index;
};

GType             xapp_favorite_info_get_type (void) G_GNUC_CONST;
XAppFavoriteInfo *xapp_favorite_info_copy     (const XAppFavoriteInfo *info);
void              xapp_favorite_info_free     (XAppFavoriteInfo *info);

G_END_DECLS

#endif  /* __XAPP_FAVORITES_MANAGER_H__ */
