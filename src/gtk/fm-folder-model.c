/*
 *      fm-folder-model.c
 *
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include "fm-config.h"
#include "fm-folder-model.h"
#include "fm-file-info.h"
#include "fm-icon-pixbuf.h"
#include "fm-thumbnail.h"

#include <gdk/gdk.h>

#include <string.h>
#include <gio/gio.h>

enum {
    LOADED,
    N_SIGNALS
};

typedef struct _FmFolderItem FmFolderItem;
struct _FmFolderItem
{
    FmFileInfo* inf;
    GdkPixbuf* big_icon;
    GdkPixbuf* small_icon;
};

static void fm_folder_model_tree_model_init(GtkTreeModelIface *iface);
static void fm_folder_model_tree_sortable_init(GtkTreeSortableIface *iface);
static void fm_folder_model_drag_source_init(GtkTreeDragSourceIface *iface);
static void fm_folder_model_drag_dest_init(GtkTreeDragDestIface *iface);

static void fm_folder_model_finalize(GObject *object);
G_DEFINE_TYPE_WITH_CODE( FmFolderModel, fm_folder_model, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL, fm_folder_model_tree_model_init)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_SORTABLE, fm_folder_model_tree_sortable_init)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_DRAG_SOURCE, fm_folder_model_drag_source_init)
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_DRAG_DEST, fm_folder_model_drag_dest_init) )

static GtkTreeModelFlags fm_folder_model_get_flags(GtkTreeModel *tree_model);
static gint fm_folder_model_get_n_columns(GtkTreeModel *tree_model);
static GType fm_folder_model_get_column_type(GtkTreeModel *tree_model,
                                             gint index);
static gboolean fm_folder_model_get_iter(GtkTreeModel *tree_model,
                                         GtkTreeIter *iter,
                                         GtkTreePath *path);
static GtkTreePath *fm_folder_model_get_path(GtkTreeModel *tree_model,
                                             GtkTreeIter *iter);
static void fm_folder_model_get_value(GtkTreeModel *tree_model,
                                      GtkTreeIter *iter,
                                      gint column,
                                      GValue *value);
static gboolean fm_folder_model_iter_next(GtkTreeModel *tree_model,
                                          GtkTreeIter *iter);
static gboolean fm_folder_model_iter_children(GtkTreeModel *tree_model,
                                              GtkTreeIter *iter,
                                              GtkTreeIter *parent);
static gboolean fm_folder_model_iter_has_child(GtkTreeModel *tree_model,
                                               GtkTreeIter *iter);
static gint fm_folder_model_iter_n_children(GtkTreeModel *tree_model,
                                            GtkTreeIter *iter);
static gboolean fm_folder_model_iter_nth_child(GtkTreeModel *tree_model,
                                               GtkTreeIter *iter,
                                               GtkTreeIter *parent,
                                               gint n);
static gboolean fm_folder_model_iter_parent(GtkTreeModel *tree_model,
                                            GtkTreeIter *iter,
                                            GtkTreeIter *child);
static gboolean fm_folder_model_get_sort_column_id(GtkTreeSortable* sortable,
                                                   gint* sort_column_id,
                                                   GtkSortType* order);
static void fm_folder_model_set_sort_column_id(GtkTreeSortable* sortable,
                                               gint sort_column_id,
                                               GtkSortType order);
static void fm_folder_model_set_sort_func(GtkTreeSortable *sortable,
                                          gint sort_column_id,
                                          GtkTreeIterCompareFunc sort_func,
                                          gpointer user_data,
                                          GtkDestroyNotify destroy);
static void fm_folder_model_set_default_sort_func(GtkTreeSortable *sortable,
                                                  GtkTreeIterCompareFunc sort_func,
                                                  gpointer user_data,
                                                  GtkDestroyNotify destroy);
static void fm_folder_model_sort(FmFolderModel* model);

/* signal handlers */
static void on_folder_loaded(FmFolder* folder, FmFolderModel* model);

static void on_icon_theme_changed(GtkIconTheme* theme, FmFolderModel* model);

static void on_big_icon_size_changed(FmConfig* cfg, FmFolderModel* model);
static void on_small_icon_size_changed(FmConfig* cfg, FmFolderModel* model);

static void on_thumbnail_loaded(FmThumbnailRequest* req, gpointer user_data);

#define IS_HIDDEN_FILE(fn)  \
    (fn[0] == '.' || g_str_has_suffix(fn, "~"))

//static void on_thumbnail_loaded( FmFolder* dir, VFSFileInfo* file, FmFolderModel* model );

static GType column_types[ N_FOLDER_MODEL_COLS ];
static guint signals[N_SIGNALS];

void fm_folder_model_init(FmFolderModel* model)
{
    model->sort_order = -1;
    model->sort_col = -1;
    /* Random int to check whether an iter belongs to our model */
    model->stamp = g_random_int();
    
    model->theme_change_handler = g_signal_connect(gtk_icon_theme_get_default(), "changed", 
                                                   G_CALLBACK(on_icon_theme_changed), model);

    /* config change notifications */
    g_signal_connect(fm_config, "changed::big_icon_size", G_CALLBACK(on_big_icon_size_changed), model);
    g_signal_connect(fm_config, "changed::small_icon_size", G_CALLBACK(on_small_icon_size_changed), model);
}

void fm_folder_model_class_init(FmFolderModelClass *klass)
{
    GObjectClass * object_class;

    fm_folder_model_parent_class = ( GObjectClass* )g_type_class_peek_parent(klass);
    object_class = ( GObjectClass* )klass;

    object_class->finalize = fm_folder_model_finalize;

    signals[LOADED]=
        g_signal_new("loaded",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(FmFolderModelClass, loaded),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);
}

void fm_folder_model_tree_model_init(GtkTreeModelIface *iface)
{
    iface->get_flags = fm_folder_model_get_flags;
    iface->get_n_columns = fm_folder_model_get_n_columns;
    iface->get_column_type = fm_folder_model_get_column_type;
    iface->get_iter = fm_folder_model_get_iter;
    iface->get_path = fm_folder_model_get_path;
    iface->get_value = fm_folder_model_get_value;
    iface->iter_next = fm_folder_model_iter_next;
    iface->iter_children = fm_folder_model_iter_children;
    iface->iter_has_child = fm_folder_model_iter_has_child;
    iface->iter_n_children = fm_folder_model_iter_n_children;
    iface->iter_nth_child = fm_folder_model_iter_nth_child;
    iface->iter_parent = fm_folder_model_iter_parent;

    column_types [ COL_FILE_BIG_ICON ] = GDK_TYPE_PIXBUF;
    column_types [ COL_FILE_SMALL_ICON ] = GDK_TYPE_PIXBUF;
    column_types [ COL_FILE_NAME ] = G_TYPE_STRING;
    column_types [ COL_FILE_DESC ] = G_TYPE_STRING;
    column_types [ COL_FILE_SIZE ] = G_TYPE_STRING;
    column_types [ COL_FILE_DESC ] = G_TYPE_STRING;
    column_types [ COL_FILE_PERM ] = G_TYPE_STRING;
    column_types [ COL_FILE_OWNER ] = G_TYPE_STRING;
    column_types [ COL_FILE_MTIME ] = G_TYPE_STRING;
    column_types [ COL_FILE_INFO ] = G_TYPE_POINTER;
    column_types [ COL_FILE_GICON ] = G_TYPE_ICON;
}

void fm_folder_model_tree_sortable_init(GtkTreeSortableIface *iface)
{
    /* iface->sort_column_changed = fm_folder_model_sort_column_changed; */
    iface->get_sort_column_id = fm_folder_model_get_sort_column_id;
    iface->set_sort_column_id = fm_folder_model_set_sort_column_id;
    iface->set_sort_func = fm_folder_model_set_sort_func;
    iface->set_default_sort_func = fm_folder_model_set_default_sort_func;
    iface->has_default_sort_func = ( gboolean (*)(GtkTreeSortable *) )gtk_false;
}

void fm_folder_model_drag_source_init(GtkTreeDragSourceIface *iface)
{
    /* FIXME: Unused. Will this cause any problem? */
}

void fm_folder_model_drag_dest_init(GtkTreeDragDestIface *iface)
{
    /* FIXME: Unused. Will this cause any problem? */
}

void fm_folder_model_finalize(GObject *object)
{
    FmFolderModel* model = ( FmFolderModel* )object;
    int i;
    /*
    char* str = fm_path_to_str(model->dir->dir_path);
    g_debug("FINALIZE FOLDER MODEL(%p): %s", model, str);
    g_free(str);
    */
    fm_folder_model_set_folder(model, NULL);
    g_signal_handler_disconnect(gtk_icon_theme_get_default(), 
                                model->theme_change_handler);

    g_signal_handlers_disconnect_by_func(fm_config, on_big_icon_size_changed, object);
    g_signal_handlers_disconnect_by_func(fm_config, on_small_icon_size_changed, object);

    for(i=0; i<=FM_FOLDER_MODEL_THUMBNAIL_EXTRA; ++i)
    {
        g_list_foreach(model->thumbnail_requests[i], (GFunc)fm_thumbnail_request_cancel, NULL);
        g_list_free(model->thumbnail_requests[i]);
    }

    /* must chain up - finalize parent */
    (*G_OBJECT_CLASS(fm_folder_model_parent_class)->finalize)(object);
}

FmFolderModel *fm_folder_model_new(FmFolder* dir, gboolean show_hidden)
{
    FmFolderModel* model;
    model = ( FmFolderModel* )g_object_new(FM_TYPE_FOLDER_MODEL, NULL);
    model->items = NULL;
    model->hidden = NULL;
    model->show_hidden = show_hidden;
    fm_folder_model_set_folder(model, dir);
    return model;
}

inline FmFolderItem* fm_folder_item_new(FmFileInfo* inf)
{
    FmFolderItem* item = g_slice_new0(FmFolderItem);
    item->inf = fm_file_info_ref(inf);
    return item;
}

inline void fm_folder_item_free(FmFolderItem* item)
{
    if( item->big_icon )
        g_object_unref(item->big_icon);
    if( item->small_icon )
        g_object_unref(item->small_icon);
    fm_file_info_unref(item->inf);
    g_slice_free(FmFolderItem, item);
}

static void _fm_folder_model_insert_item(FmFolder* dir,
                                         FmFolderItem* new_item,
                                         FmFolderModel* model);

static void _fm_folder_model_files_changed(FmFolder* dir, GSList* files,
                                           FmFolderModel* model)
{
    GSList* l;
    for( l = files; l; l=l->next )
        fm_folder_model_file_changed(model, l->data);
}

static void _fm_folder_model_add_file(FmFolderModel* model, FmFileInfo* file)
{
    if( !model->show_hidden && IS_HIDDEN_FILE(file->path->name) )
        g_sequence_append( model->hidden, fm_folder_item_new(file) );
    else
        fm_folder_model_file_created(model, file);
}

static void _fm_folder_model_files_added(FmFolder* dir, GSList* files,
                                         FmFolderModel* model)
{
    GSList* l;
    FmFileInfo* file;
    for( l = files; l; l=l->next )
        _fm_folder_model_add_file(model, (FmFileInfo*)l->data);
}


static void _fm_folder_model_files_removed(FmFolder* dir, GSList* files,
                                           FmFolderModel* model)
{
    GSList* l;
    for( l = files; l; l=l->next )
        fm_folder_model_file_deleted(model, (FmFileInfo*)l->data);
}

void fm_folder_model_set_folder(FmFolderModel* model, FmFolder* dir)
{
    GSequenceIter *it;
    if( model->dir == dir )
        return;
    if( model->dir )
    {
        g_signal_handlers_disconnect_by_func(model->dir,
                                             _fm_folder_model_files_added, model);
        g_signal_handlers_disconnect_by_func(model->dir,
                                             _fm_folder_model_files_removed, model);
        g_signal_handlers_disconnect_by_func(model->dir,
                                             _fm_folder_model_files_changed, model);
        g_signal_handlers_disconnect_by_func(model->dir,
                                             on_folder_loaded, model);

        g_sequence_free(model->items);
        g_sequence_free(model->hidden);
        g_object_unref(model->dir);
    }
    model->dir = dir;
    model->items = g_sequence_new(fm_folder_item_free);
    model->hidden = g_sequence_new(fm_folder_item_free);
    if( !dir )
        return;

    model->dir = (FmFolder*)g_object_ref(model->dir);

    g_signal_connect(model->dir, "files-added",
                     G_CALLBACK(_fm_folder_model_files_added),
                     model);
    g_signal_connect(model->dir, "files-removed",
                     G_CALLBACK(_fm_folder_model_files_removed),
                     model);
    g_signal_connect(model->dir, "files-changed",
                     G_CALLBACK(_fm_folder_model_files_changed),
                     model);
    g_signal_connect(model->dir, "loaded",
                     G_CALLBACK(on_folder_loaded), model);

    if( !fm_list_is_empty(dir->files) )
    {
        GList *l;
        for( l = fm_list_peek_head_link(dir->files); l; l = l->next )
            _fm_folder_model_add_file(model, (FmFileInfo*)l->data);
    }

    if( !fm_folder_get_is_loading(model->dir) ) /* if it's already loaded */
        on_folder_loaded(model->dir, model);  /* emit 'loaded' signal */
}

gboolean fm_folder_model_get_is_loading(FmFolderModel* model)
{
    return fm_folder_get_is_loading(model->dir);
}

GtkTreeModelFlags fm_folder_model_get_flags(GtkTreeModel *tree_model)
{
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), ( GtkTreeModelFlags )0);
    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}

gint fm_folder_model_get_n_columns(GtkTreeModel *tree_model)
{
    return N_FOLDER_MODEL_COLS;
}

GType fm_folder_model_get_column_type(GtkTreeModel *tree_model,
                                      gint index)
{
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), G_TYPE_INVALID);
    g_return_val_if_fail(index < G_N_ELEMENTS(column_types) && index >= 0, G_TYPE_INVALID);
    return column_types[ index ];
}

gboolean fm_folder_model_get_iter(GtkTreeModel *tree_model,
                                  GtkTreeIter *iter,
                                  GtkTreePath *path)
{
    FmFolderModel* model;
    gint *indices, n, depth;
    GSequenceIter* items_it;

    g_assert( FM_IS_FOLDER_MODEL(tree_model) );
    g_assert(path!=NULL);

    model = FM_FOLDER_MODEL(tree_model);

    indices = gtk_tree_path_get_indices(path);
    depth   = gtk_tree_path_get_depth(path);

    /* we do not allow children */
    g_assert(depth == 1); /* depth 1 = top level; a list only has top level nodes and no children */

    n = indices[0]; /* the n-th top level row */

    if( n >= g_sequence_get_length(model->items) || n < 0 )
        return FALSE;

    items_it = g_sequence_get_iter_at_pos(model->items, n);

    g_assert( items_it  != g_sequence_get_end_iter(model->items) );

    /* We simply store a pointer in the iter */
    iter->stamp = model->stamp;
    iter->user_data  = items_it;

    return TRUE;
}

GtkTreePath *fm_folder_model_get_path(GtkTreeModel *tree_model,
                                      GtkTreeIter *iter)
{
    GtkTreePath* path;
    GSequenceIter* items_it;
    FmFolderModel* model = FM_FOLDER_MODEL(tree_model);

    g_return_val_if_fail(model, NULL);
    g_return_val_if_fail(iter->stamp == model->stamp, NULL);
    g_return_val_if_fail(iter != NULL, NULL);
    g_return_val_if_fail(iter->user_data != NULL, NULL);

    items_it = (GSequenceIter*)iter->user_data;
    path = gtk_tree_path_new();
    gtk_tree_path_append_index( path, g_sequence_iter_get_position(items_it) );
    return path;
}

void fm_folder_model_get_value(GtkTreeModel *tree_model,
                               GtkTreeIter *iter,
                               gint column,
                               GValue *value)
{
    GSequenceIter* item_it;
    FmFolderModel* model = FM_FOLDER_MODEL(tree_model);

    g_return_if_fail(iter != NULL);
    g_return_if_fail( column < G_N_ELEMENTS(column_types) );

    g_value_init(value, column_types[column]);

    item_it = (GSequenceIter*)iter->user_data;
    g_return_if_fail(item_it != NULL);

    FmFolderItem* item = (FmFolderItem*)g_sequence_get(item_it);
    FmFileInfo* info = item->inf;

    switch( column )
    {
    case COL_FILE_GICON:
        g_value_set_object(value, info->icon->gicon);
        break;
    case COL_FILE_BIG_ICON:
    {
        if( G_UNLIKELY(!item->big_icon) )
        {
            if( !info->icon )
                return;
            item->big_icon = fm_icon_get_pixbuf(info->icon, fm_config->big_icon_size);
        }
        g_value_set_object(value, item->big_icon);
        break;
    }
    case COL_FILE_SMALL_ICON:
        if( G_UNLIKELY(!item->small_icon) )
        {
            if( !info->icon )
                return;
            item->small_icon = fm_icon_get_pixbuf(info->icon, fm_config->small_icon_size);
        }
        g_value_set_object(value, item->small_icon);
        break;
    case COL_FILE_NAME:
        g_value_set_string(value, info->disp_name);
        break;
    case COL_FILE_SIZE:
        g_value_set_string( value, fm_file_info_get_disp_size(info) );
        break;
    case COL_FILE_DESC:
        g_value_set_string( value, fm_file_info_get_desc(info) );
        break;
    case COL_FILE_PERM:
//        g_value_set_string( value, fm_file_info_get_disp_perm(info) );
        break;
    case COL_FILE_OWNER:
//        g_value_set_string( value, fm_file_info_get_disp_owner(info) );
        break;
    case COL_FILE_MTIME:
        g_value_set_string( value, fm_file_info_get_disp_mtime(info) );
        break;
    case COL_FILE_INFO:
        g_value_set_pointer(value, info);
        break;
    }
}

gboolean fm_folder_model_iter_next(GtkTreeModel *tree_model,
                                   GtkTreeIter *iter)
{
    GSequenceIter* item_it, *next_item_it;
    FmFolderModel* model;

    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), FALSE);

    if( iter == NULL || iter->user_data == NULL )
        return FALSE;

    model = FM_FOLDER_MODEL(tree_model);
    item_it = (GSequenceIter *)iter->user_data;

    /* Is this the last iter in the list? */
    next_item_it = g_sequence_iter_next(item_it);

    if( g_sequence_iter_is_end(next_item_it) )
        return FALSE;

    iter->stamp = model->stamp;
    iter->user_data = next_item_it;

    return TRUE;
}

gboolean fm_folder_model_iter_children(GtkTreeModel *tree_model,
                                       GtkTreeIter *iter,
                                       GtkTreeIter *parent)
{
    FmFolderModel* model;
    GSequenceIter* items_it;
    g_return_val_if_fail(parent == NULL || parent->user_data != NULL, FALSE);

    /* this is a list, nodes have no children */
    if( parent )
        return FALSE;

    /* parent == NULL is a special case; we need to return the first top-level row */
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), FALSE);
    model = FM_FOLDER_MODEL(tree_model);

    /* No rows => no first row */
//    if ( model->dir->n_items == 0 )
//        return FALSE;

    /* Set iter to first item in list */
    g_sequence_get_begin_iter(model->items);
    iter->stamp = model->stamp;
    iter->user_data  = items_it;
    return TRUE;
}

gboolean fm_folder_model_iter_has_child(GtkTreeModel *tree_model,
                                        GtkTreeIter *iter)
{
    return FALSE;
}

gint fm_folder_model_iter_n_children(GtkTreeModel *tree_model,
                                     GtkTreeIter *iter)
{
    FmFolderModel* model;
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), -1);
    g_return_val_if_fail(iter == NULL || iter->user_data != NULL, FALSE);
    model = FM_FOLDER_MODEL(tree_model);
    /* special case: if iter == NULL, return number of top-level rows */
    if( !iter )
        return g_sequence_get_length(model->items);
    return 0; /* otherwise, this is easy again for a list */
}

gboolean fm_folder_model_iter_nth_child(GtkTreeModel *tree_model,
                                        GtkTreeIter *iter,
                                        GtkTreeIter *parent,
                                        gint n)
{
    GSequenceIter* items_it;
    FmFolderModel* model;

    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), FALSE);
    model = FM_FOLDER_MODEL(tree_model);

    /* a list has only top-level rows */
    if( parent )
        return FALSE;

    /* special case: if parent == NULL, set iter to n-th top-level row */
    if( n >= g_sequence_get_length(model->items) || n < 0 )
        return FALSE;

    items_it = g_sequence_get_iter_at_pos(model->items, n);
    g_assert( items_it  != g_sequence_get_end_iter(model->items) );

    iter->stamp = model->stamp;
    iter->user_data  = items_it;

    return TRUE;
}

gboolean fm_folder_model_iter_parent(GtkTreeModel *tree_model,
                                     GtkTreeIter *iter,
                                     GtkTreeIter *child)
{
    return FALSE;
}

gboolean fm_folder_model_get_sort_column_id(GtkTreeSortable* sortable,
                                            gint* sort_column_id,
                                            GtkSortType* order)
{
    FmFolderModel* model = (FmFolderModel*)sortable;
    if( sort_column_id )
        *sort_column_id = model->sort_col;
    if( order )
        *order = model->sort_order;
    return TRUE;
}

void fm_folder_model_set_sort_column_id(GtkTreeSortable* sortable,
                                        gint sort_column_id,
                                        GtkSortType order)
{
    FmFolderModel* model = (FmFolderModel*)sortable;
    if( model->sort_col == sort_column_id && model->sort_order == order )
        return;
    model->sort_col = sort_column_id;
    model->sort_order = order;
    gtk_tree_sortable_sort_column_changed(sortable);
    fm_folder_model_sort(model);
}

void fm_folder_model_set_sort_func(GtkTreeSortable *sortable,
                                   gint sort_column_id,
                                   GtkTreeIterCompareFunc sort_func,
                                   gpointer user_data,
                                   GtkDestroyNotify destroy)
{
    g_warning("fm_folder_model_set_sort_func: Not supported\n");
}

void fm_folder_model_set_default_sort_func(GtkTreeSortable *sortable,
                                           GtkTreeIterCompareFunc sort_func,
                                           gpointer user_data,
                                           GtkDestroyNotify destroy)
{
    g_warning("fm_folder_model_set_default_sort_func: Not supported\n");
}

static gint fm_folder_model_compare(FmFolderItem* item1,
                                    FmFolderItem* item2,
                                    FmFolderModel* model)
{
    FmFileInfo* file1 = item1->inf;
    FmFileInfo* file2 = item2->inf;
    int ret = 0;

    /* put folders before files */
    ret = fm_file_info_is_dir(file1) - fm_file_info_is_dir(file2);
    if( ret )
        return -ret;

    switch( model->sort_col )
    {
    case COL_FILE_NAME:
    {
        const char* key1 = fm_file_info_get_collate_key(file1);
        const char* key2 = fm_file_info_get_collate_key(file2);
        ret = g_ascii_strcasecmp(key1, key2);
        break;
    }
    case COL_FILE_SIZE:
        ret = file1->size - file2->size;
        break;
    case COL_FILE_MTIME:
        ret = file1->mtime - file2->mtime;
        break;
    }
    return model->sort_order == GTK_SORT_ASCENDING ? -ret : ret;
}

void fm_folder_model_sort(FmFolderModel* model)
{
    GHashTable* old_order;
    gint *new_order;
    GSequenceIter *items_it;
    GtkTreePath *path;

    /* if there is only one item */
    if( model->items == NULL || g_sequence_get_length(model->items) <= 1 )
        return;

    old_order = g_hash_table_new(g_direct_hash, g_direct_equal);
    /* save old order */
    items_it = g_sequence_get_begin_iter(model->items);
    while( !g_sequence_iter_is_end(items_it) )
    {
        int i = g_sequence_iter_get_position(items_it);
        g_hash_table_insert( old_order, items_it, GINT_TO_POINTER(i) );
        items_it = g_sequence_iter_next(items_it);
    }

    /* sort the list */
    g_sequence_sort(model->items, fm_folder_model_compare, model);

    /* save new order */
    new_order = g_new( int, g_sequence_get_length(model->items) );
    items_it = g_sequence_get_begin_iter(model->items);
    while( !g_sequence_iter_is_end(items_it) )
    {
        int i = g_sequence_iter_get_position(items_it);
        new_order[i] = (guint)g_hash_table_lookup(old_order, items_it);
        items_it = g_sequence_iter_next(items_it);
    }
    g_hash_table_destroy(old_order);
    path = gtk_tree_path_new();
    gtk_tree_model_rows_reordered(GTK_TREE_MODEL(model),
                                  path, NULL, new_order);
    gtk_tree_path_free(path);
    g_free(new_order);
}

void fm_folder_model_file_created(FmFolderModel* model, FmFileInfo* file)
{
    FmFolderItem* new_item = fm_folder_item_new(file);
    _fm_folder_model_insert_item(model->dir, new_item, model);
}

void _fm_folder_model_insert_item(FmFolder* dir,
                                  FmFolderItem* new_item,
                                  FmFolderModel* model)
{
    GList* l;
    GtkTreeIter it;
    GtkTreePath* path;
    FmFolderItem* item;
    FmFileInfo* file = new_item->inf;

    GSequenceIter *item_it = g_sequence_insert_sorted(model->items, new_item, fm_folder_model_compare, model);

    it.stamp = model->stamp;
    it.user_data  = item_it;

    path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(item_it), -1);
    gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &it);
    gtk_tree_path_free(path);
}


void fm_folder_model_file_deleted(FmFolderModel* model, FmFileInfo* file)
{
    GSequenceIter *seq_it;
    /* not required for hidden files */
    gboolean update_view;
#if 0
    /* If there is no file info, that means the dir itself was deleted. */
    if( G_UNLIKELY(!file) )
    {
        /* Clear the whole list */
        GSequenceIter *items_it = g_sequence_get_begin_iter(model->items);
        path = gtk_tree_path_new_from_indices(0, -1);
        while( !g_sequence_iter_is_end(items_it) )
        {
            gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
            file  = (VFSFileInfo*)g_sequence_get(items_it);
            items_it = g_sequence_iter_next(it);
            vfs_file_info_unref(file);
        }
        for( l = model->items; l; l = model->items )
        {
            gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
            file = (VFSFileInfo*)l->data;
            model->items = g_list_delete_link(model->items, l);
            vfs_file_info_unref(file);
        }
        g_sequence_remove_range( g_sequence_get_begin_iter(model->items), g_sequence_get_end_iter(model->items) );
        gtk_tree_path_free(path);
        return;
    }
#endif

    if( !model->show_hidden && IS_HIDDEN_FILE(file->path->name) ) /* if this is a hidden file */
    {
        update_view = FALSE;
        seq_it = g_sequence_get_begin_iter(model->hidden);
    }
    else
    {
        update_view = TRUE;
        seq_it = g_sequence_get_begin_iter(model->items);
    }

    while( !g_sequence_iter_is_end(seq_it) )
    {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(seq_it);
        if( item->inf == file )
            break;
        seq_it = g_sequence_iter_next(seq_it);
    }

    if( update_view )
    {
        GtkTreePath* path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(seq_it), -1);
        gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
        gtk_tree_path_free(path);
    }
    g_sequence_remove(seq_it);
}

void fm_folder_model_file_changed(FmFolderModel* model, FmFileInfo* file)
{
    FmFolderItem* item;
    GSequenceIter* items_it;
    GtkTreeIter it;
    GtkTreePath* path;

    if( !model->show_hidden && IS_HIDDEN_FILE(file->path->name) )
        return;

    items_it = g_sequence_get_begin_iter(model->items);
    /* FIXME: write a  GCompareDataFunc for this */
    while( !g_sequence_iter_is_end(items_it) )
    {
        item = (FmFolderItem*)g_sequence_get(items_it);
        if( item->inf == file )
            break;
        items_it = g_sequence_iter_next(items_it);
    }

    if( items_it == g_sequence_get_end_iter(model->items) )
        return;

    /* update the icon */
    if( item->big_icon )
    {
        g_object_unref(item->big_icon);
        item->big_icon = NULL;
    }
    if( item->small_icon )
    {
        g_object_unref(item->small_icon);
        item->small_icon = NULL;
    }

    it.stamp = model->stamp;
    it.user_data  = items_it;

    path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(items_it), -1);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &it);
    gtk_tree_path_free(path);
}


#if 0

gboolean fm_folder_model_find_iter(FmFolderModel* model, GtkTreeIter* it, VFSFileInfo* fi)
{
    GSequenceIter* items_it = g_sequence_get_begin_iter(model->items);
    while( !g_sequence_iter_is_end(items_it) )
    {
        VFSFileInfo* fi2 = (VFSFileInfo*)g_sequence_get(items_it);
        if( G_UNLIKELY( fi2 == fi
                       || 0 == strcmp( vfs_file_info_get_name(fi), vfs_file_info_get_name(fi2) ) ) )
        {
            it->stamp = model->stamp;
            it->user_data = items_it;
            return TRUE;
        }
        items_it = g_sequence_iter_next(items_it);
    }
    return FALSE;
}


void on_thumbnail_loaded(FmFolder* dir, VFSFileInfo* file, FmFolderModel* model)
{
    /* g_debug( "LOADED: %s", file->path->name ); */
    fm_folder_model_file_changed(dir, file, model);
}

void fm_folder_model_show_thumbnails(FmFolderModel* model, gboolean is_big,
                                     int max_file_size)
{
    GSequenceIter *it = g_sequence_get_begin_iter(model->items);
    VFSFileInfo* file;
    int old_max_thumbnail;

    old_max_thumbnail = model->max_thumbnail;
    model->max_thumbnail = max_file_size;
    model->big_thumbnail = is_big;
    /* FIXME: This is buggy!!! Further testing might be needed.
     */
    if( 0 == max_file_size )
    {
        if( old_max_thumbnail > 0 ) /* cancel thumbnails */
        {
            vfs_thumbnail_loader_cancel_all_requests(model->dir, model->big_thumbnail);
            g_signal_handlers_disconnect_by_func(model->dir, on_thumbnail_loaded, model);

            while( !g_sequence_iter_is_end(items_it) )
            {
                file = (VFSFileInfo*)g_sequence_get(items_it);
                if( vfs_file_info_is_image(file)
                   && vfs_file_info_is_thumbnail_loaded(file, is_big) )
                {
                    /* update the model */
                    fm_folder_model_file_changed(model->dir, file, model);
                }
                items_it = g_sequence_iter_next(items_it);
            }

        }
        return;
    }

    g_signal_connect(model->dir, "thumbnail-loaded",
                     G_CALLBACK(on_thumbnail_loaded), model);

    while( !g_sequence_iter_is_end(items_it) )
    {
        file = (VFSFileInfo*)g_sequence_get(items_it);
        if( vfs_file_info_is_image(file)
            && vfs_file_info_get_size(file) < model->max_thumbnail )
        {
            if( vfs_file_info_is_thumbnail_loaded(file, is_big) )
                fm_folder_model_file_changed(model->dir, file, model);
            else
            {
                vfs_thumbnail_loader_request(model->dir, file, is_big);
                /* g_debug( "REQUEST: %s", file->path->name ); */
            }
        }
        items_it = g_sequence_iter_next(items_it);
    }
}

#endif

gboolean fm_folder_model_get_show_hidden(FmFolderModel* model)
{
    return model->show_hidden;
}


void fm_folder_model_set_show_hidden(FmFolderModel* model, gboolean show_hidden)
{
    FmFolderItem* item;
    GList *l, *next;
    GSequenceIter *items_it;

    if( model->show_hidden == show_hidden )
        return;

    model->show_hidden = show_hidden;
    if( show_hidden ) /* add previously hidden items back to the list */
    {
        GSequenceIter *hidden_it = g_sequence_get_begin_iter(model->hidden);
        while( !g_sequence_iter_is_end(hidden_it) )
        {
            GtkTreeIter it;
            GSequenceIter *next_hidden_it;
            GSequenceIter *insert_item_it = g_sequence_search(model->items, g_sequence_get(hidden_it),
                                                              fm_folder_model_compare, model);
            next_hidden_it = g_sequence_iter_next(hidden_it);
            item = (FmFolderItem*)g_sequence_get(hidden_it);
            it.stamp = model->stamp;
            it.user_data  = hidden_it;
            g_sequence_move(hidden_it, insert_item_it);
            GtkTreePath *path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(hidden_it), -1);
            gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &it);
            gtk_tree_path_free(path);
            hidden_it = next_hidden_it;
        }
    }
    else /* move invisible items to hidden list */
    {
        GSequenceIter *items_it = g_sequence_get_begin_iter(model->items);
        while( !g_sequence_iter_is_end(items_it) )
        {
            GtkTreePath* tp;
            GSequenceIter *next_item_it = g_sequence_iter_next(items_it);
            item = (FmFolderItem*)g_sequence_get(items_it);
            if( IS_HIDDEN_FILE(item->inf->path->name) )
            {
                gint delete_pos = g_sequence_iter_get_position(items_it);
                g_sequence_move( items_it, g_sequence_get_begin_iter(model->hidden) );
                tp = gtk_tree_path_new_from_indices(delete_pos, -1);
                /* tell everybody that we removed an item */
                gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), tp);
                gtk_tree_path_free(tp);
            }
            items_it = next_item_it;
        }
    }
}

void on_folder_loaded(FmFolder* folder, FmFolderModel* model)
{
    g_signal_emit(model, signals[LOADED], 0);
}

static void reload_icons(FmFolderModel* model, gboolean big, gboolean small)
{
    g_debug("reload folder icons: %s", model->dir->dir_path->name);
    /* Reload icons */
    GSequenceIter* it = g_sequence_get_begin_iter(model->items);
    GtkTreePath* tp = gtk_tree_path_new_from_indices(0, -1);
    for( ; !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it) )
    {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(it);
        gboolean changed = FALSE;
        if(big && item->big_icon)
        {
            g_object_unref(item->big_icon);
            item->big_icon = NULL;
            changed = TRUE;
        }
        if(small && item->small_icon)
        {
            g_object_unref(item->small_icon);
            item->small_icon = NULL;
            changed = TRUE;
        }
        
        if(changed)
        {
            GtkTreeIter tree_it = {0};
            tree_it.stamp = model->stamp;
            tree_it.user_data = it;
            gtk_tree_model_row_changed(model, tp, &tree_it);
        }
        gtk_tree_path_next(tp);
    }
    gtk_tree_path_free(tp);

    it = g_sequence_get_begin_iter(model->hidden);
    for( ; !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it) )
    {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(it);
        if(big && item->big_icon)
        {
            g_object_unref(item->big_icon);
            item->big_icon = NULL;
        }
        if(small && item->small_icon)
        {
            g_object_unref(item->small_icon);
            item->small_icon = NULL;
        }
    }    
}

void on_icon_theme_changed(GtkIconTheme* theme, FmFolderModel* model)
{
    reload_icons(model, TRUE, TRUE);
}

void on_big_icon_size_changed(FmConfig* cfg, FmFolderModel* model)
{
    reload_icons(model, TRUE, FALSE);
}

void on_small_icon_size_changed(FmConfig* cfg, FmFolderModel* model)
{
    reload_icons(model, FALSE, TRUE);
}

void fm_folder_model_get_common_suffix_for_prefix(FmFolderModel* model,
                                                  const gchar* prefix,
                                                  gboolean (*file_info_predicate)(FmFileInfo*),
                                                  gchar* common_suffix)
{
    GSequenceIter *item_it;
    gint prefix_len;
    gboolean common_suffix_initialized = FALSE;

    g_return_if_fail(common_suffix != NULL);

    if( !model )
        return;

    prefix_len = strlen(prefix);
    common_suffix[0] = 0;

    for( item_it = g_sequence_get_begin_iter(model->items);
        !g_sequence_iter_is_end(item_it);
        item_it = g_sequence_iter_next(item_it) )
    {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(item_it);
        gboolean predicate_ok = (file_info_predicate == NULL) || file_info_predicate(item->inf);
        gint i = 0;
        if( predicate_ok && g_str_has_prefix(item->inf->disp_name, prefix) )
        {
            /* first match -> init */
            if( !common_suffix_initialized )
            {
                strcpy(common_suffix,  item->inf->disp_name + prefix_len);
                common_suffix_initialized = TRUE;
            }
            else
            {
                while( common_suffix[i] == item->inf->disp_name[prefix_len + i] )
                    i++;
                common_suffix[i] = 0;
            }

        }
    }
}

gboolean fm_folder_model_find_iter_by_filename(FmFolderModel* model, GtkTreeIter* it, const char* name)
{
    GSequenceIter *item_it = g_sequence_get_begin_iter(model->items);
    for( ; !g_sequence_iter_is_end(item_it); item_it = g_sequence_iter_next(item_it) )
    {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(item_it);
        if( g_strcmp0(item->inf->path->name, name) == 0 )
        {
            it->stamp = model->stamp;
            it->user_data  = item_it;
            return TRUE;
        }
    }
    return FALSE;
}

void on_thumbnail_loaded(FmThumbnailRequest* req, gpointer user_data)
{
    FmFolderModel* model = (FmFolderModel*)user_data;
    FmFileInfo* fi = fm_thumbnail_request_get_file_info(req);
    GdkPixbuf* pix = fm_thumbnail_request_get_pixbuf(req);
    GtkTreeIter it;
    guint size = fm_thumbnail_request_get_size(req);
    FmFolderModelThumbnailSize size_type;
    GSequenceIter* seq_it;

    g_debug("thumbnail loaded for %s, %p, size = %d", fi->path->name, pix, size);

    if(size == fm_config->big_icon_size)
        size_type = FM_FOLDER_MODEL_THUMBNAIL_BIG;
    else if(size == fm_config->small_icon_size)
        size_type = FM_FOLDER_MODEL_THUMBNAIL_SMALL;
//    else if(size == fm_config->thumbnail_icon_size)
//        size_type = FM_FOLDER_MODEL_THUMBNAIL_EXTRA;
    else
        return;

    /* remove the request from list */
    model->thumbnail_requests[size_type] = g_list_remove(model->thumbnail_requests[size_type], req);

    /* FIXME: it's better to find iter by file_info */
    if(fm_folder_model_find_iter_by_filename(model, &it, fi->path->name))
    {
        FmFolderItem* item;
        seq_it = (GSequenceIter*)it.user_data;
        item = (FmFolderItem*)g_sequence_get(seq_it);
        if(pix)
        {
            GtkTreePath* tp = fm_folder_model_get_path(model, &it);
            switch(size_type)
            {
            case FM_FOLDER_MODEL_THUMBNAIL_BIG:
                if(item->big_icon)
                    g_object_unref(item->big_icon);
                item->big_icon = g_object_ref(pix);
                break;
            case FM_FOLDER_MODEL_THUMBNAIL_SMALL:
                if(item->small_icon)
                    g_object_unref(item->small_icon);
                item->small_icon = g_object_ref(pix);
                break;
            case FM_FOLDER_MODEL_THUMBNAIL_EXTRA:
                break;
            }
            gtk_tree_model_row_changed(model, tp, &it);
            gtk_tree_path_free(tp);
        }
    }
}

void fm_folder_model_load_thumbnails( FmFolderModel* model, FmFolderModelThumbnailSize size)
{
    g_return_if_fail(size >=0 && size < 3);
    ++model->n_thumbnail_users[size];
    /* the caller of this function is the only one who request thumbnail of this size */
    if(model->n_thumbnail_users[size] == 1)
    {
        GSequenceIter* it = g_sequence_get_begin_iter(model->items);
        /* FIXME: handle changes of fm_config->big_icon_size */
        int req_sizes[] = {fm_config->big_icon_size, fm_config->small_icon_size, fm_config->big_icon_size};
        for( ; !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it) )
        {
            FmFolderItem* item = (FmFolderItem*)g_sequence_get(it);
            FmThumbnailRequest* req;
            /* FIXME: we need fm_file_info_can_thumbnail() */
            if(!fm_file_info_is_dir(item->inf))
            {
                req = fm_thumbnail_request(item->inf, req_sizes[size], on_thumbnail_loaded, model);
                model->thumbnail_requests[size] = g_list_prepend(model->thumbnail_requests[size], req);
            }
        }
    }
}

void fm_folder_model_unload_thumbnails( FmFolderModel* model, FmFolderModelThumbnailSize size)
{
    g_return_if_fail(size >=0 && size < 3);

    --model->n_thumbnail_users[size];
    /* no other people needs thumbnail of this size */
    if(0 == model->n_thumbnail_users[size])
    {
        g_list_foreach(model->thumbnail_requests[size], (GFunc)fm_thumbnail_request_cancel, NULL);
        g_list_free(model->thumbnail_requests[size]);
        model->thumbnail_requests[size] = NULL;
    }
}
