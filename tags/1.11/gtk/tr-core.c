/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#include <string.h> /* strcmp, strlen */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#ifdef HAVE_GIO
#include <gio/gio.h>
#endif

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_free */

#include "conf.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "tr-torrent.h"
#include "util.h"

struct TrCorePrivate
{
#ifdef HAVE_GIO
    GFileMonitor     * monitor;
    gulong             monitor_tag;
    char             * monitor_path;
    GSList           * monitor_files;
    guint              monitor_idle_tag;
#endif
    GtkTreeModel     * model;
    tr_handle        * handle;
    int                nextid;
};

static void
tr_core_marshal_err( GClosure * closure, GValue * ret UNUSED,
                     guint count, const GValue * vals,
                     gpointer hint UNUSED, gpointer marshal )
{
    typedef void (*TRMarshalErr)
        ( gpointer, enum tr_core_err, const char *, gpointer );
    TRMarshalErr     callback;
    GCClosure      * cclosure = (GCClosure*) closure;
    enum tr_core_err errcode;
    const char     * errstr;
    gpointer         inst, gdata;

    g_return_if_fail( count == 3 );

    inst    = g_value_peek_pointer( vals );
    errcode = g_value_get_int( vals + 1 );
    errstr  = g_value_get_string( vals + 2 );
    gdata   = closure->data;

    callback = (TRMarshalErr)( marshal ? marshal : cclosure->callback );
    callback( inst, errcode, errstr, gdata );
}

static void
tr_core_marshal_prompt( GClosure * closure, GValue * ret UNUSED,
                        guint count, const GValue * vals,
                        gpointer hint UNUSED, gpointer marshal )
{
    typedef void (*TRMarshalPrompt)( gpointer, tr_ctor *, gpointer );
    TRMarshalPrompt        callback;
    GCClosure            * cclosure = (GCClosure*) closure;
    gpointer               ctor;
    gpointer               inst, gdata;

    g_return_if_fail( count == 2 );

    inst      = g_value_peek_pointer( vals );
    ctor      = g_value_peek_pointer( vals + 1 );
    gdata     = closure->data;

    callback = (TRMarshalPrompt)( marshal ? marshal : cclosure->callback );
    callback( inst, ctor, gdata );
}

static int
isDisposed( const TrCore * core )
{
    return !core || !core->priv;
}

static void
tr_core_dispose( GObject * obj )
{
    TrCore * core = TR_CORE( obj );

    if( !isDisposed( core ) )
    {
        GObjectClass * parent;

        pref_save( NULL );
        core->priv = NULL;

        parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );
        parent->dispose( obj );
    }
}

static void
tr_core_class_init( gpointer g_class, gpointer g_class_data UNUSED )
{
    GObjectClass * gobject_class;
    TrCoreClass  * core_class;

    g_type_class_add_private( g_class, sizeof(struct TrCorePrivate) );

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = tr_core_dispose;


    core_class = TR_CORE_CLASS( g_class );
    core_class->errsig = g_signal_new( "error", G_TYPE_FROM_CLASS( g_class ),
                                       G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                       tr_core_marshal_err, G_TYPE_NONE,
                                       2, G_TYPE_INT, G_TYPE_STRING );
    core_class->promptsig = g_signal_new( "add-torrent-prompt",
                                          G_TYPE_FROM_CLASS( g_class ),
                                          G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                          tr_core_marshal_prompt, G_TYPE_NONE,
                                          1, G_TYPE_POINTER );
    core_class->quitsig = g_signal_new( "quit", G_TYPE_FROM_CLASS( g_class ),
                                        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE, 0 );
    core_class->prefsig = g_signal_new( "prefs-changed",
                                        G_TYPE_FROM_CLASS( g_class ),
                                        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        g_cclosure_marshal_VOID__STRING,
                                        G_TYPE_NONE, 1, G_TYPE_STRING );
}

/***
****  SORTING
***/

static int
compareDouble( double a, double b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compareByActivity( GtkTreeModel * model,
                   GtkTreeIter  * a,
                   GtkTreeIter  * b,
                   gpointer       user_data UNUSED )
{
    int i;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );

    sa = tr_torrentStatCached( ta );
    sb = tr_torrentStatCached( tb );

    if(( i = compareDouble( sa->rateUpload + sa->rateDownload,
                            sb->rateUpload + sb->rateDownload ) ))
        return i;

    if( sa->uploadedEver != sb->uploadedEver )
        return sa->uploadedEver < sa->uploadedEver ? -1 : 1;

    return 0;
}

static int
compareByName( GtkTreeModel   * model,
               GtkTreeIter    * a,
               GtkTreeIter    * b,
               gpointer         user_data UNUSED )
{
    int ret;
    char *ca, *cb;
    gtk_tree_model_get( model, a, MC_NAME_COLLATED, &ca, -1 );
    gtk_tree_model_get( model, b, MC_NAME_COLLATED, &cb, -1 );
    ret = strcmp( ca, cb );
    g_free( cb );
    g_free( ca );
    return ret;
}

static int
compareByProgress( GtkTreeModel   * model,
                   GtkTreeIter    * a,
                   GtkTreeIter    * b,
                   gpointer         user_data UNUSED )
{
    int ret;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;
    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );
    sa = tr_torrentStatCached( ta );
    sb = tr_torrentStatCached( tb );
    ret = compareDouble( sa->percentDone, sb->percentDone );
    if( !ret )
        ret = compareDouble( sa->ratio, sb->ratio );
    return ret;
}

static int
compareByState( GtkTreeModel   * model,
                GtkTreeIter    * a,
                GtkTreeIter    * b,
                gpointer         user_data )
{
    int sa, sb, ret;

    /* first by state */
    gtk_tree_model_get( model, a, MC_STATUS, &sa, -1 );
    gtk_tree_model_get( model, b, MC_STATUS, &sb, -1 );
    ret = sa - sb;

    /* second by progress */
    if( !ret )
        ret = compareByProgress( model, a, b, user_data );

    return ret;
}

static int
compareByTracker( GtkTreeModel   * model,
                  GtkTreeIter    * a,
                  GtkTreeIter    * b,
                  gpointer         user_data UNUSED )
{
    const tr_torrent *ta, *tb;
    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );
    return strcmp( tr_torrentInfo(ta)->primaryAddress,
                   tr_torrentInfo(tb)->primaryAddress );
}

static void
setSort( TrCore * core, const char * mode, gboolean isReversed  )
{
    int col = MC_TORRENT_RAW;
    GtkSortType type = isReversed ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
    GtkTreeModel * model = tr_core_model( core );
    GtkTreeSortable * sortable = GTK_TREE_SORTABLE( model );

    if( !strcmp( mode, "sort-by-activity" ) )
        gtk_tree_sortable_set_sort_func( sortable, col, compareByActivity, NULL, NULL );
    else if( !strcmp( mode, "sort-by-progress" ) )
        gtk_tree_sortable_set_sort_func( sortable, col, compareByProgress, NULL, NULL );
    else if( !strcmp( mode, "sort-by-state" ) )
        gtk_tree_sortable_set_sort_func( sortable, col, compareByState, NULL, NULL );
    else if( !strcmp( mode, "sort-by-tracker" ) )
        gtk_tree_sortable_set_sort_func( sortable, col, compareByTracker, NULL, NULL );
    else {
        type = isReversed ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
        gtk_tree_sortable_set_sort_func( sortable, col, compareByName, NULL, NULL );
    }

    gtk_tree_sortable_set_sort_column_id( sortable, col, type );
}

static void
tr_core_apply_defaults( tr_ctor * ctor )
{
    if( tr_ctorGetPaused( ctor, TR_FORCE, NULL ) )
        tr_ctorSetPaused( ctor, TR_FORCE, !pref_flag_get( PREF_KEY_START ) );

    if( tr_ctorGetDeleteSource( ctor, NULL ) ) 
        tr_ctorSetDeleteSource( ctor, pref_flag_get( PREF_KEY_TRASH_ORIGINAL ) ); 

    if( tr_ctorGetMaxConnectedPeers( ctor, TR_FORCE, NULL ) )
        tr_ctorSetMaxConnectedPeers( ctor, TR_FORCE,
                              pref_int_get( PREF_KEY_MAX_PEERS_PER_TORRENT ) );

    if( tr_ctorGetDestination( ctor, TR_FORCE, NULL ) ) {
        char * path = pref_string_get( PREF_KEY_DIR_DEFAULT );
        tr_ctorSetDestination( ctor, TR_FORCE, path );
        g_free( path );
    }
}

#ifdef HAVE_GIO
static gboolean
watchFolderIdle( gpointer gcore )
{
    TrCore * core = TR_CORE( gcore );
    tr_core_add_list_defaults( core, core->priv->monitor_files );

    /* cleanup */
    core->priv->monitor_files = NULL;
    core->priv->monitor_idle_tag = 0;
    return FALSE;
}

static void
maybeAddTorrent( TrCore * core, const char * filename )
{
    const gboolean isTorrent = g_str_has_suffix( filename, ".torrent" );

    if( isTorrent )
    {
        struct TrCorePrivate * p = core->priv;

        if( !g_slist_find_custom( p->monitor_files, filename, (GCompareFunc)strcmp ) )
            p->monitor_files = g_slist_append( p->monitor_files, g_strdup( filename ) );
        if( !p->monitor_idle_tag )
            p->monitor_idle_tag = g_timeout_add( 1000, watchFolderIdle, core );
    }
}

static void
watchFolderChanged( GFileMonitor       * monitor UNUSED,
                    GFile              * file,
                    GFile              * other_type UNUSED,
                    GFileMonitorEvent    event_type,
                    gpointer             core )
{
    if( event_type == G_FILE_MONITOR_EVENT_CREATED )
    {
        char * filename = g_file_get_path( file );
        maybeAddTorrent( core, filename );
        g_free( filename );
    }
}

static void
scanWatchDir( TrCore * core )
{
    const gboolean isEnabled = pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED );
    if( isEnabled )
    {
        char * dirname = pref_string_get( PREF_KEY_DIR_WATCH );
        GDir * dir = g_dir_open( dirname, 0, NULL );
        const char * basename;
        while(( basename = g_dir_read_name( dir ))) {
            char * filename = g_build_filename( dirname, basename, NULL );
            maybeAddTorrent( core, filename );
            g_free( filename );
        }
        g_free( dirname );
    }
}

static void
updateWatchDir( TrCore * core )
{
    char * filename = pref_string_get( PREF_KEY_DIR_WATCH );
    const gboolean isEnabled = pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED );
    struct TrCorePrivate * p = TR_CORE( core )->priv;

    if( p->monitor && ( !isEnabled || tr_strcmp( filename, p->monitor_path ) ) )
    {
        g_signal_handler_disconnect( p->monitor, p->monitor_tag );
        g_free( p->monitor_path );
        g_file_monitor_cancel( p->monitor );
        g_object_unref( G_OBJECT( p->monitor ) );
        p->monitor_path = NULL;
        p->monitor = NULL;
        p->monitor_tag = 0;
    }

    if( isEnabled && !p->monitor )
    {
        GFile * file = g_file_new_for_path( filename );
        GFileMonitor * m = g_file_monitor_directory( file, 0, NULL, NULL );
        scanWatchDir( core );
        p->monitor = m;
        p->monitor_path = g_strdup( filename );
        p->monitor_tag = g_signal_connect( m, "changed",
                                           G_CALLBACK( watchFolderChanged ), core );
    }

    g_free( filename );
}
#endif

static void
prefsChanged( TrCore * core, const char * key, gpointer data UNUSED )
{
    if( !strcmp( key, PREF_KEY_SORT_MODE ) ||
        !strcmp( key, PREF_KEY_SORT_REVERSED ) )
    {
        char * mode = pref_string_get( PREF_KEY_SORT_MODE );
        gboolean isReversed = pref_flag_get( PREF_KEY_SORT_REVERSED );
        setSort( core, mode, isReversed );
        g_free( mode );
    }
    else if( !strcmp( key, PREF_KEY_MAX_PEERS_GLOBAL ) )
    {
        const uint16_t val = pref_int_get( key );
        tr_setGlobalPeerLimit( tr_core_handle( core ), val );
    }
#ifdef HAVE_GIO
    else if( !strcmp( key, PREF_KEY_DIR_WATCH ) ||
             !strcmp( key, PREF_KEY_DIR_WATCH_ENABLED ) )
    {
        updateWatchDir( core );
    }
#endif
}

static void
tr_core_init( GTypeInstance * instance, gpointer g_class UNUSED )
{
    tr_handle * h;
    TrCore * self = (TrCore *) instance;
    GtkListStore * store;
    struct TrCorePrivate * p;

    /* column types for the model used to store torrent information */
    /* keep this in sync with the enum near the bottom of tr_core.h */
    GType types[] = {
        G_TYPE_STRING,    /* name */
        G_TYPE_STRING,    /* collated name */
        G_TYPE_STRING,    /* hash string */
        TR_TORRENT_TYPE,  /* TrTorrent object */
        G_TYPE_POINTER,   /* tr_torrent* */
        G_TYPE_INT,       /* tr_stat()->status */
        G_TYPE_INT        /* ID for IPC */
    };

    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                                                  TR_CORE_TYPE,
                                                  struct TrCorePrivate );


    h = tr_initFull( "gtk",
                     pref_flag_get( PREF_KEY_PEX ),
                     pref_flag_get( PREF_KEY_NAT ),
                     pref_int_get( PREF_KEY_PORT ),
                     pref_flag_get( PREF_KEY_ENCRYPTED_ONLY )
                         ? TR_ENCRYPTION_REQUIRED
                         : TR_ENCRYPTION_PREFERRED,
                     pref_flag_get( PREF_KEY_UL_LIMIT_ENABLED ),
                     pref_int_get( PREF_KEY_UL_LIMIT ),
                     pref_flag_get( PREF_KEY_DL_LIMIT_ENABLED ),
                     pref_int_get( PREF_KEY_DL_LIMIT ),
                     pref_int_get( PREF_KEY_MAX_PEERS_GLOBAL ),
                     pref_int_get( PREF_KEY_MSGLEVEL ),
                     TRUE, /* message queueing */
                     pref_flag_get( PREF_KEY_BLOCKLIST_ENABLED ) );

    /* create the model used to store torrent data */
    g_assert( ALEN( types ) == MC_ROW_COUNT );
    store = gtk_list_store_newv( MC_ROW_COUNT, types );

    p->model    = GTK_TREE_MODEL( store );
    p->handle   = h;
    p->nextid   = 1;
}

GType
tr_core_get_type( void )
{
    static GType type = 0;

    if( !type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrCoreClass ),
            NULL,                       /* base_init */
            NULL,                       /* base_finalize */
            tr_core_class_init,         /* class_init */
            NULL,                       /* class_finalize */
            NULL,                       /* class_data */
            sizeof( TrCore ),
            0,                          /* n_preallocs */
            tr_core_init,               /* instance_init */
            NULL,
        };
        type = g_type_register_static( G_TYPE_OBJECT, "TrCore", &info, 0 );
    }

    return type;
}

/**
***
**/

TrCore *
tr_core_new( void )
{
    TrCore * core = TR_CORE( g_object_new( TR_CORE_TYPE, NULL ) );

    /* init from prefs & listen to pref changes */
    prefsChanged( core, PREF_KEY_SORT_MODE, NULL );
    prefsChanged( core, PREF_KEY_SORT_REVERSED, NULL );
    prefsChanged( core, PREF_KEY_DIR_WATCH_ENABLED, NULL );
    prefsChanged( core, PREF_KEY_MAX_PEERS_GLOBAL, NULL );
    g_signal_connect( core, "prefs-changed", G_CALLBACK(prefsChanged), NULL );

    return core;
}

GtkTreeModel *
tr_core_model( TrCore * core )
{
    return isDisposed( core ) ? NULL : core->priv->model;
}

tr_handle *
tr_core_handle( TrCore * core )
{
    return isDisposed( core ) ? NULL : core->priv->handle;
}

static gboolean
statsForeach( GtkTreeModel * model,
              GtkTreePath  * path UNUSED,
              GtkTreeIter  * iter,
              gpointer       gstats )
{
    tr_torrent * tor;
    struct core_stats * stats = gstats;
    int status;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    status = tr_torrentGetStatus( tor );

    if( status == TR_STATUS_DOWNLOAD )
        ++stats->downloadCount;
    else if( status == TR_STATUS_SEED )
        ++stats->seedingCount;

    return FALSE;
}

void
tr_core_get_stats( const TrCore       * core,
                   struct core_stats  * setme )
{
    memset( setme, 0, sizeof( struct core_stats ) );

    if( !isDisposed( core ) )
    {
        tr_torrentRates( core->priv->handle,
                         &setme->clientDownloadSpeed,
                         &setme->clientUploadSpeed );
        gtk_tree_model_foreach( core->priv->model,
                                statsForeach,
                                setme );
    }
}

static char*
doCollate( const char * in )
{
    const char * end = in + strlen( in );
    char * casefold;
    char * ret;

    while( in < end ) {
        const gunichar ch = g_utf8_get_char( in );
        if (!g_unichar_isalnum (ch)) /* eat everything before the first alnum */
            in += g_unichar_to_utf8( ch, NULL );
        else
            break;
    }

    if ( in == end )
        return g_strdup ("");

    casefold = g_utf8_casefold( in, end-in );
    ret = g_utf8_collate_key( casefold, -1 );
    g_free( casefold );

    return ret;
}

void
tr_core_add_torrent( TrCore * self, TrTorrent * tor )
{
    const tr_info * inf = tr_torrent_info( tor );
    const tr_stat * torStat = tr_torrent_stat( tor );
    char * collated = doCollate( inf->name );
    GtkListStore * store = GTK_LIST_STORE( tr_core_model( self ) );
    GtkTreeIter unused;

    gtk_list_store_insert_with_values( store, &unused, 0, 
                                       MC_NAME,          inf->name,
                                       MC_NAME_COLLATED, collated,
                                       MC_HASH,          inf->hashString,
                                       MC_TORRENT,       tor,
                                       MC_TORRENT_RAW,   tr_torrent_handle( tor ),
                                       MC_STATUS,        torStat->status,
                                       MC_ID,            self->priv->nextid,
                                       -1);
    ++self->priv->nextid;

    /* cleanup */
    g_object_unref( G_OBJECT( tor ) );
    g_free( collated );
}

int
tr_core_load( TrCore * self, gboolean forcePaused )
{
    int i;
    int count = 0;
    tr_torrent ** torrents;
    char * path;
    tr_ctor * ctor;

    path = getdownloaddir( );

    ctor = tr_ctorNew( tr_core_handle( self ) );
    if( forcePaused )
        tr_ctorSetPaused( ctor, TR_FORCE, TRUE );
    tr_ctorSetDestination( ctor, TR_FALLBACK, path );
    tr_ctorSetMaxConnectedPeers( ctor, TR_FALLBACK,
                                 pref_int_get( PREF_KEY_MAX_PEERS_PER_TORRENT ) );

    torrents = tr_loadTorrents ( tr_core_handle( self ), ctor, &count );
    for( i=0; i<count; ++i )
        tr_core_add_torrent( self, tr_torrent_new_preexisting( torrents[i] ) );

    tr_free( torrents );
    tr_ctorFree( ctor );
    g_free( path );

    return count;
}

static void
tr_core_errsig( TrCore * core, enum tr_core_err type, const char * msg )
{
    g_signal_emit( core, TR_CORE_GET_CLASS(core)->errsig, 0, type, msg );
}

void
tr_core_add_ctor( TrCore * self, tr_ctor * ctor )
{
    TrTorrent * tor;
    char      * errstr = NULL;

    tr_core_apply_defaults( ctor );

    if(( tor = tr_torrent_new_ctor( tr_core_handle( self ), ctor, &errstr )))
        tr_core_add_torrent( self, tor );
    else{ 
        tr_core_errsig( self, TR_CORE_ERR_ADD_TORRENT, errstr );
        g_free( errstr );
    }

    /* cleanup */
    tr_ctorFree( ctor );
}

void
tr_core_add_list( TrCore      * core,
                  GSList      * torrentFiles,
                  pref_flag_t   start,
                  pref_flag_t   prompt )
{
    const gboolean doStart = pref_flag_eval( start, PREF_KEY_START );
    const gboolean doPrompt = pref_flag_eval( prompt, PREF_KEY_OPTIONS_PROMPT );

    if( torrentFiles && !isDisposed( core ) )
    {
        GSList * l;
        tr_handle * handle = core->priv->handle;

        for( l=torrentFiles; l!=NULL; l=l->next )
        {
            tr_ctor * ctor = tr_ctorNew( handle );
            tr_core_apply_defaults( ctor );
            tr_ctorSetPaused( ctor, TR_FORCE, !doStart );
            if( tr_ctorSetMetainfoFromFile( ctor, l->data ) )
                tr_ctorFree( ctor );
            else if( tr_torrentParse( handle, ctor, NULL ) )
                tr_ctorFree( ctor );
            else if( doPrompt )
                g_signal_emit( core, TR_CORE_GET_CLASS(core)->promptsig, 0, ctor );
            else
                tr_core_add_ctor( core, ctor );
        }
    }

    freestrlist( torrentFiles );
}

void
tr_core_torrents_added( TrCore * self )
{
    tr_core_update( self );
    tr_core_errsig( self, TR_CORE_ERR_NO_MORE_TORRENTS, NULL );
}

void
tr_core_delete_torrent( TrCore * self, GtkTreeIter * iter )
{
    TrTorrent * tor;
    GtkTreeModel * model = tr_core_model( self );

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    gtk_list_store_remove( GTK_LIST_STORE( model ), iter );
    tr_torrentRemoveSaved( tr_torrent_handle( tor ) );

    g_object_unref( G_OBJECT( tor ) );
}

static gboolean
findTorrentInModel( TrCore * core, const TrTorrent * gtor, GtkTreeIter * setme )
{
    int match = 0;
    GtkTreeIter iter;
    GtkTreeModel * model = tr_core_model( core );

    if( gtk_tree_model_iter_children( model, &iter, NULL ) ) do
    {
        TrTorrent * tmp;
        gtk_tree_model_get( model, &iter, MC_TORRENT, &tmp, -1 );
        match = tmp == gtor;
        g_object_unref( G_OBJECT( tmp ) );
    }
    while( !match && gtk_tree_model_iter_next( model, &iter ) );

    if( match )
        *setme = iter;

    return match;
}

void
tr_core_remove_torrent( TrCore * self, TrTorrent * gtor, int deleteFiles )
{
    GtkTreeIter iter;
    GtkTreeModel * model = tr_core_model( self );

    /* remove from the gui */
    if( findTorrentInModel( self, gtor, &iter ) )
        gtk_list_store_remove( GTK_LIST_STORE( model ), &iter );

    /* maybe delete the downloaded files */
    if( deleteFiles )
        tr_torrent_delete_files( gtor );

    /* delete the torrent */
    tr_torrent_set_delete_flag( gtor, TRUE );
    g_object_unref( G_OBJECT( gtor ) );
}


/***
****
***/

static gboolean
update_foreach( GtkTreeModel * model,
                GtkTreePath  * path UNUSED,
                GtkTreeIter  * iter,
                gpointer       data UNUSED )
{
    int oldStatus;
    int newStatus;
    TrTorrent * gtor;

    /* maybe update the status column in the model */
    gtk_tree_model_get( model, iter,
                        MC_TORRENT, &gtor,
                        MC_STATUS, &oldStatus,
                        -1 );
    newStatus = tr_torrentGetStatus( tr_torrent_handle( gtor ) );
    if( newStatus != oldStatus )
        gtk_list_store_set( GTK_LIST_STORE( model ), iter,
                            MC_STATUS, newStatus,
                            -1 );

    /* check the seeding cap */
    tr_torrent_check_seeding_cap ( gtor );

    /* cleanup */
    g_object_unref( gtor );
    return FALSE;
}

void
tr_core_update( TrCore * self )
{
    int column;
    GtkSortType order;
    GtkTreeSortable * sortable;
    GtkTreeModel * model = tr_core_model( self );

    /* pause sorting */
    sortable = GTK_TREE_SORTABLE( model );
    gtk_tree_sortable_get_sort_column_id( sortable, &column, &order );
    gtk_tree_sortable_set_sort_column_id( sortable, GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order );

    /* refresh the model */
    gtk_tree_model_foreach( model, update_foreach, NULL );

    /* resume sorting */
    gtk_tree_sortable_set_sort_column_id( sortable, column, order );
}

void
tr_core_quit( TrCore * core )
{
    g_signal_emit( core, TR_CORE_GET_CLASS(core)->quitsig, 0 );
}

/**
***  Prefs
**/

static void
commitPrefsChange( TrCore * core, const char * key )
{
    pref_save( NULL );
    g_signal_emit( core, TR_CORE_GET_CLASS(core)->prefsig, 0, key );
}

void
tr_core_set_pref( TrCore * self, const char * key, const char * newval )
{
    char * oldval = pref_string_get( key );
    if( tr_strcmp( oldval, newval ) )
    {
        pref_string_set( key, newval );
        commitPrefsChange( self, key );
    }
    g_free( oldval );
}

void
tr_core_set_pref_bool( TrCore * self, const char * key, gboolean newval )
{
    const gboolean oldval = pref_flag_get( key );
    if( oldval != newval )
    {
        pref_flag_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_int( TrCore * self, const char * key, int newval )
{
    const int oldval = pref_int_get( key );
    if( oldval != newval )
    {
        pref_int_set( key, newval );
        commitPrefsChange( self, key );
    }
}
