/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_getRatio */

#include "file-list.h"
#include "hig.h"

enum
{
    SUB_STATE_HIGH          = (1<<0),
    SUB_STATE_NORMAL        = (1<<1),
    SUB_STATE_LOW           = (1<<2),
    SUB_STATE_PRIORITY_MASK = (SUB_STATE_HIGH|SUB_STATE_NORMAL|SUB_STATE_LOW),
    SUB_STATE_DOWNLOAD      = (1<<4),
    SUB_STATE_IGNORE        = (1<<5),
    SUB_STATE_DOWNLOAD_MASK = (SUB_STATE_DOWNLOAD|SUB_STATE_IGNORE)
};

enum
{
    FC_STOCK,
    FC_LABEL,
    FC_PROG,
    FC_KEY,
    FC_INDEX,
    FC_SIZE,
    FC_HAVE,
    FC_PRIORITY,
    FC_ENABLED,
    FC_IS_FILE,
    FC_SUB_SIZE,
    FC_SUB_HAVE,
    FC_SUB_STATE,
    N_FILE_COLS
};

typedef struct
{
    TrTorrent * gtor;
    GtkWidget * top;
    GtkWidget * view;
    GtkTreeModel * model; /* same object as store, but recast */
    GtkTreeStore * store; /* same object as model, but recast */
    tr_file_stat * refresh_file_stat;
    guint timeout_tag;
}
FileData;

static void
clearData( FileData * data )
{
    data->gtor = NULL;

    if( data->timeout_tag ) {
        g_source_remove( data->timeout_tag );
        data->timeout_tag = 0;
    }
}

static void
freeData( gpointer gdata )
{
    FileData * data = gdata;
    clearData( data );
    g_free( data );
}

/***
****
***/

static void
parsepath( const tr_torrent  * tor,
           GtkTreeStore      * store,
           GtkTreeIter       * ret,
           const char        * path,
           tr_file_index_t     index,
           uint64_t            size )
{
    GtkTreeModel * model;
    GtkTreeIter  * parent, start, iter;
    char         * file, * lower, * mykey;
    const char   * stock;
    int            priority = 0;
    gboolean       enabled = TRUE;
    gboolean       is_file;

    model  = GTK_TREE_MODEL( store );
    parent = NULL;
    file   = g_path_get_basename( path );
    if( 0 != strcmp( file, path ) )
    {
        char * dir = g_path_get_dirname( path );
        parsepath( tor, store, &start, dir, index, size );
        parent = &start;
        g_free( dir );
    }

    lower = g_utf8_casefold( file, -1 );
    mykey = g_utf8_collate_key( lower, -1 );
    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
        gboolean stop;
        char * modelkey;
        gtk_tree_model_get( model, &iter, FC_KEY, &modelkey, -1 );
        stop = (modelkey!=NULL) && !strcmp(mykey,modelkey);
        g_free (modelkey);
        if (stop) goto done;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    gtk_tree_store_append( store, &iter, parent );
    if(( is_file = !ret ))
    {
        stock = GTK_STOCK_FILE;
        priority = tr_torrentGetFilePriority( tor, index );
        enabled  = tr_torrentGetFileDL( tor, index );
    }
    else
    {
        stock = GTK_STOCK_DIRECTORY;
        size  = 0;
    }

#if 0
    gtk_tree_store_set( store, &iter, FC_INDEX, index,
                                      FC_LABEL, file,
                                      FC_KEY, mykey,
                                      FC_STOCK, stock,
                                      FC_PRIORITY, priority,
                                      FC_ENABLED, enabled,
                                      FC_IS_FILE, is_file,
                                      FC_SIZE, size,
                                      FC_HAVE, 0,
                                      -1 );
#else
    gtk_tree_store_set( store, &iter, FC_INDEX, index, -1 );
    gtk_tree_store_set( store, &iter, FC_LABEL, file, -1 );
    gtk_tree_store_set( store, &iter, FC_KEY, mykey, -1 );
    gtk_tree_store_set( store, &iter, FC_STOCK, stock, -1 );
    gtk_tree_store_set( store, &iter, FC_PRIORITY, priority, -1 );
    gtk_tree_store_set( store, &iter, FC_ENABLED, enabled, -1 );
    gtk_tree_store_set( store, &iter, FC_IS_FILE, is_file, -1 );
    gtk_tree_store_set( store, &iter, FC_SIZE, size, -1 );
    gtk_tree_store_set( store, &iter, FC_HAVE, 0, -1 );
#endif

  done:
    g_free( mykey );
    g_free( lower );
    g_free( file );
    if( NULL != ret )
      *ret = iter;
}


/***
****
***/

static gboolean
refreshFilesForeach( GtkTreeModel  * model,
                     GtkTreePath   * path UNUSED,
                     GtkTreeIter   * iter,
                     gpointer        gdata )
{
    FileData * data = gdata;
    gboolean is_file;
    unsigned int index;
    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file, FC_INDEX, &index, -1  );
    if( is_file ) {
        GtkTreeStore * store = GTK_TREE_STORE( model );
        tr_torrent * tor = tr_torrent_handle( data->gtor );
        int download = tr_torrentGetFileDL( tor, index );
        int priority = tr_torrentGetFilePriority( tor, index );
        uint64_t have = data->refresh_file_stat[index].bytesCompleted;
        gtk_tree_store_set( store, iter, FC_PRIORITY, priority,
                                         FC_ENABLED, download,
                                         FC_HAVE, have,
                                         -1 );
    }
    return FALSE; /* keep walking */
}

static gboolean
resetSubForeach( GtkTreeModel  * model,
                 GtkTreePath   * path UNUSED,
                 GtkTreeIter   * iter,
                 gpointer        gdata UNUSED )
{
    /* set the subs to the lowest values... */
    gtk_tree_store_set( GTK_TREE_STORE( model ), iter,
                        FC_SUB_STATE, 0,
                        FC_SUB_SIZE, (uint64_t)0,
                        FC_SUB_HAVE, (uint64_t)0,
                        -1 );
    return FALSE; /* keep walking */
}

static gboolean
addSubForeach( GtkTreeModel  * model,
               GtkTreePath   * path UNUSED,
               GtkTreeIter   * iter,
               gpointer        gdata UNUSED )
{
    uint64_t size;
    uint64_t have;
    int priority;
    gboolean enabled;
    gboolean is_file;
    gtk_tree_model_get( model, iter, FC_SIZE, &size,
                                     FC_HAVE, &have,
                                     FC_PRIORITY, &priority,
                                     FC_ENABLED, &enabled,
                                     FC_IS_FILE, &is_file,
                                     -1 );
    if( is_file )
    {
        GtkTreeIter child = *iter;
        GtkTreeIter parent;
        while(( gtk_tree_model_iter_parent( model, &parent, &child )))
        {
            uint64_t sub_size;
            uint64_t sub_have;
            int sub_state;
            gtk_tree_model_get( model, &parent, FC_SUB_SIZE, &sub_size,
                                                FC_SUB_HAVE, &sub_have,
                                                FC_SUB_STATE, &sub_state,
                                                -1 );
            sub_have += have;
            sub_size += size;
            switch( priority ) {
                case TR_PRI_HIGH:   sub_state |= SUB_STATE_HIGH;   break;
                case TR_PRI_NORMAL: sub_state |= SUB_STATE_NORMAL; break;
                case TR_PRI_LOW:    sub_state |= SUB_STATE_LOW;    break;
            }
            sub_state |= ( enabled ? SUB_STATE_DOWNLOAD : SUB_STATE_IGNORE );
            gtk_tree_store_set( GTK_TREE_STORE( model ), &parent,
                                FC_SUB_SIZE, sub_size,
                                FC_SUB_HAVE, sub_have,
                                FC_SUB_STATE, sub_state,
                                -1 );
            child = parent;
        }
    }
    return FALSE; /* keep walking */
}

static void
refresh( FileData * data )
{
    tr_file_index_t fileCount;
    tr_torrent * tor = tr_torrent_handle( data->gtor );
    data->refresh_file_stat = tr_torrentFiles( tor, &fileCount );

    gtk_tree_model_foreach( data->model, refreshFilesForeach, data );
    gtk_tree_model_foreach( data->model, resetSubForeach, data );
    gtk_tree_model_foreach( data->model, addSubForeach, data );

    tr_torrentFilesFree( data->refresh_file_stat, fileCount );
    data->refresh_file_stat = NULL;
}

static gboolean
refreshModel( gpointer file_data )
{
    refresh( file_data );
    return TRUE;
}

/***
****
***/

struct ActiveData
{
    GtkTreeSelection * sel;
    GArray * array;
};

static gboolean
getActiveFilesForeach( GtkTreeModel  * model,
                       GtkTreePath   * path UNUSED,
                       GtkTreeIter   * iter,
                       gpointer        gdata )
{
    struct ActiveData * data = gdata;
    unsigned int i;
    gboolean is_file = FALSE;
    gboolean is_active = FALSE;

    /* active == if it's selected, or any ancestor is selected */
    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file, FC_INDEX, &i, -1 );
    if( is_file ) {
        is_active = gtk_tree_selection_iter_is_selected( data->sel, iter );
        if( !is_active ) {
            GtkTreeIter walk = *iter;
            GtkTreeIter parent;
            while( !is_active && gtk_tree_model_iter_parent( model, &parent, &walk )) {
                is_active = gtk_tree_selection_iter_is_selected( data->sel, &parent );
                walk = parent;
            }
        }
    }

    if( is_active )
        g_array_append_val( data->array, i );

    return FALSE; /* keep walking */
}

static GArray*
getActiveFiles( GtkTreeView * view )
{
    struct ActiveData data;
    data.sel = gtk_tree_view_get_selection( view );
    data.array  = g_array_new( FALSE, FALSE, sizeof( tr_file_index_t ) );
    gtk_tree_model_foreach( gtk_tree_view_get_model( view ), getActiveFilesForeach, &data );
    return data.array;
}

static void
setPriority( FileData * data, int priority )
{
    GtkTreeView * view = GTK_TREE_VIEW( data->view );
    GArray * a = getActiveFiles( view );
    tr_torrentSetFilePriorities( tr_torrent_handle( data->gtor ),
                                 (tr_file_index_t*)a->data,
                                 (tr_file_index_t)a->len,
                                 priority );
    refresh( data );
    g_array_free( a, TRUE );
}


static void
setDownload( FileData * data, int do_download )
{
    GtkTreeView * view = GTK_TREE_VIEW( data->view );
    GArray * a = getActiveFiles( view );
    tr_torrentSetFileDLs ( tr_torrent_handle( data->gtor ),
                           (tr_file_index_t*)a->data,
                           (tr_file_index_t)a->len,
                           do_download );
    refresh( data );
    g_array_free( a, TRUE );
}

static void
onHighClicked( GtkButton * button UNUSED, gpointer gdata )
{
    setPriority( gdata, TR_PRI_HIGH );
}
static void
onNormalClicked( GtkButton * button UNUSED, gpointer gdata )
{
    setPriority( gdata, TR_PRI_NORMAL );
}
static void
onLowClicked( GtkButton * button UNUSED, gpointer gdata )
{
    setPriority( gdata, TR_PRI_LOW );
}
static void
onDownloadClicked( GtkButton * button UNUSED, gpointer gdata )
{
    setDownload( gdata, TRUE );
}
static void
onIgnoreClicked( GtkButton * button UNUSED, gpointer gdata )
{
    setDownload( gdata, FALSE );
}

/***
****
***/

void
file_list_set_torrent( GtkWidget * w, TrTorrent * gtor )
{
    GtkTreeStore        * store;
    FileData            * data;

    data = g_object_get_data( G_OBJECT( w ), "file-data" );

    /* unset the old fields */
    clearData( data );

    /* instantiate the model */
    store = gtk_tree_store_new ( N_FILE_COLS,
                                 G_TYPE_STRING,    /* stock */
                                 G_TYPE_STRING,    /* label */
                                 G_TYPE_INT,       /* prog [0..100] */
                                 G_TYPE_STRING,    /* key */
                                 G_TYPE_UINT,      /* index */
                                 G_TYPE_UINT64,    /* size */
                                 G_TYPE_UINT64,    /* have */
                                 G_TYPE_INT,       /* priority */
                                 G_TYPE_BOOLEAN,   /* dl enabled */
                                 G_TYPE_BOOLEAN,   /* is file */
                                 G_TYPE_UINT64,    /* sub size */
                                 G_TYPE_UINT64,    /* sub have */
                                 G_TYPE_INT );     /* sub state */
    data->store = store;
    data->model = GTK_TREE_MODEL( store );
    data->gtor = gtor;


    /* populate the model */
    if( gtor )
    {
        tr_file_index_t i;
        const tr_info * inf = tr_torrent_info( gtor );
        tr_torrent * tor = tr_torrent_handle( gtor );

        for( i=0; inf && i<inf->fileCount; ++i )
        {
            const char * path = inf->files[i].name;
            const char * base = g_path_is_absolute( path ) ? g_path_skip_root( path ) : path;
            parsepath( tor, store, NULL, base, i, inf->files[i].length );
        }

        refresh( data );

        data->timeout_tag = g_timeout_add( 2500, refreshModel, data );
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW( data->view ), GTK_TREE_MODEL( store ) );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( data->view ) );
}

/***
****
***/

static void
renderProgress( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED)
{
    gboolean is_file;
    uint64_t size, have, subsize, subhave;
    double progress;
    gtk_tree_model_get( model, iter, FC_SIZE, &size,
                                     FC_HAVE, &have,
                                     FC_SUB_SIZE, &subsize,
                                     FC_SUB_HAVE, &subhave,
                                     FC_IS_FILE, &is_file,
                                     -1 );
    progress = is_file ? tr_getRatio( have, size )
                       : tr_getRatio( subhave, subsize );
    g_object_set( renderer, "value", (int)(progress*100), NULL );
}

static void
renderFilename( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED)
{
    char * filename;
    char * str;
    int64_t size;
    int64_t subsize;
    gboolean is_file;
    char buf[64];
    gtk_tree_model_get( model, iter, FC_LABEL, &filename,
                                     FC_SIZE, &size,
                                     FC_SUB_SIZE, &subsize,
                                     FC_IS_FILE, &is_file,
                                     -1 );
    tr_strlsize( buf, is_file ? size : subsize, sizeof( buf ) );
    str = g_markup_printf_escaped( "<small>%s (%s)</small>",
                                   filename, buf );
    g_object_set( renderer, "markup", str, NULL );
    g_free( str );
    g_free( filename );
}

static void
renderDownload( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED)
{
    int sub_state;
    gboolean enabled;
    gboolean is_file = FALSE;
    const char * text;

    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_ENABLED, &enabled,
                                     FC_SUB_STATE, &sub_state,
                                     -1 );
    if( is_file && enabled )
        text = _( "Yes" );
    else if( is_file )
        text = _( "No" );
    else switch( sub_state & SUB_STATE_DOWNLOAD_MASK ) {
        case SUB_STATE_DOWNLOAD: text = _( "Yes" );   break;
        case SUB_STATE_IGNORE:   text = _( "No" );    break;
        default:                 text = _( "Mixed" ); break;
    }

    g_object_set( renderer, "text", text,
                            "xalign", (gfloat)0.5,
                            "yalign", (gfloat)0.5,
                            NULL );
}

static void
renderPriority( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED)
{
    int priority;
    int sub_state;
    gboolean is_file = FALSE;
    const char * text = "";
    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_PRIORITY, &priority,
                                     FC_SUB_STATE, &sub_state,
                                     -1 );
    if( !is_file )
    {
        switch( sub_state & SUB_STATE_PRIORITY_MASK )
        {
            case SUB_STATE_HIGH:   priority = TR_PRI_HIGH;   break;
            case SUB_STATE_NORMAL: priority = TR_PRI_NORMAL; break;
            case SUB_STATE_LOW:    priority = TR_PRI_LOW;    break;
            default:               priority = 666;           break;
        }
    }

    switch( priority )
    {
        case TR_PRI_HIGH: text = _( "High" );
                          break;
        case TR_PRI_NORMAL: text = _( "Normal" );
                            break;
        case TR_PRI_LOW: text = _( "Low" );
                         break;
        default: text = _( "Mixed" );
                 break;
    }

    g_object_set( renderer, "text", text,
                            "xalign", (gfloat)0.5,
                            "yalign", (gfloat)0.5,
                            NULL );
}

GtkWidget *
file_list_new( TrTorrent * gtor )
{
    GtkWidget           * ret;
    FileData            * data;
    GtkWidget           * view, * scroll;
    GtkCellRenderer     * rend;
    GtkTreeViewColumn   * col;
    GtkTreeSelection    * sel;
    GtkWidget           * hbox;
    GtkWidget           * vbox;
    GtkWidget           * w;

    /* create the view */
    view = gtk_tree_view_new( );
    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( view ), TRUE );
    gtk_container_set_border_width( GTK_CONTAINER( view ), GUI_PAD_BIG );

    /* set up view */
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_MULTIPLE );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( view ) );
    gtk_tree_view_set_search_column( GTK_TREE_VIEW( view ), FC_LABEL );

    /* add file column */
    
    col = GTK_TREE_VIEW_COLUMN (g_object_new (GTK_TYPE_TREE_VIEW_COLUMN,
        "expand", TRUE,
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. */ 				      
        "title", Q_("filedetails|File"),
        NULL));
    rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "stock-id", FC_STOCK );
    /* add text renderer */
    rend = gtk_cell_renderer_text_new();
    g_object_set( rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    gtk_tree_view_column_pack_start( col, rend, TRUE );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderFilename, NULL, NULL);
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );


    rend = gtk_cell_renderer_progress_new( );
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. */ 
    col = gtk_tree_view_column_new_with_attributes( Q_("filedetails|Progress"), rend, NULL );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderProgress, NULL, NULL);
    gtk_tree_view_append_column ( GTK_TREE_VIEW( view ), col);

    /* add "enabled" column */
    rend = gtk_cell_renderer_text_new( );
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. 
       The items for this column are checkboxes (yes/no) */ 
    col = gtk_tree_view_column_new_with_attributes( Q_( "filedetails|Download" ), rend, NULL );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderDownload, NULL, NULL);
    gtk_tree_view_append_column ( GTK_TREE_VIEW( view ), col);

    /* add priority column */
    rend = gtk_cell_renderer_text_new( );
    col = gtk_tree_view_column_new_with_attributes( _( "Priority" ), rend, NULL );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderPriority, NULL, NULL);
    gtk_tree_view_append_column ( GTK_TREE_VIEW( view ), col);

    /* create the scrolled window and stick the view in it */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll),
                                         GTK_SHADOW_IN);
    gtk_container_add( GTK_CONTAINER( scroll ), view );
    gtk_widget_set_size_request (scroll, -1, 200 );

    data = g_new0( FileData, 1 );

    vbox = gtk_vbox_new( FALSE, GUI_PAD );
        w = gtk_button_new_with_mnemonic( _( "_High" ) );
        g_signal_connect( w, "clicked", G_CALLBACK(onHighClicked), data );
        gtk_box_pack_start( GTK_BOX( vbox ), w, FALSE, FALSE, 0 );
        w = gtk_button_new_with_mnemonic( _( "_Normal" ) );
        g_signal_connect( w, "clicked", G_CALLBACK(onNormalClicked), data );
        gtk_box_pack_start( GTK_BOX( vbox ), w, FALSE, FALSE, 0 );
        w = gtk_button_new_with_mnemonic( _( "_Low" ) );
        g_signal_connect( w, "clicked", G_CALLBACK(onLowClicked), data );
        gtk_box_pack_start( GTK_BOX( vbox ), w, FALSE, FALSE, 0 );
        w = gtk_button_new_with_mnemonic( _( "_Ignore" ) );
        g_signal_connect( w, "clicked", G_CALLBACK(onIgnoreClicked), data );
        gtk_box_pack_end( GTK_BOX( vbox ), w, FALSE, FALSE, 0 );
        w = gtk_button_new_with_mnemonic( _( "_Download" ) );
        g_signal_connect( w, "clicked", G_CALLBACK(onDownloadClicked), data );
        gtk_box_pack_end( GTK_BOX( vbox ), w, FALSE, FALSE, 0 );
    hbox = gtk_hbox_new( FALSE, GUI_PAD );
    gtk_box_pack_start_defaults( GTK_BOX( hbox ), scroll );
    gtk_box_pack_start( GTK_BOX( hbox ), vbox, 0, 0, 0 );

    ret = hbox;
    data->view = view;
    data->top = scroll;
    g_object_set_data_full( G_OBJECT( ret ), "file-data", data, freeData );
    file_list_set_torrent( ret, gtor );

    return ret;
}
