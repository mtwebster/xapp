
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>

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

static void
initialize_item (SnItem *item)
{
    
}

SnItem *
sn_item_new (GDBusProxy *proxy)
{
    SnItem *item = g_object_new (sn_item_get_type (), NULL);

    item->proxy = proxy;

    initialize_item (item);

    return item;
}