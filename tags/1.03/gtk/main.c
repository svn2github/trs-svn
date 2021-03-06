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

#include <sys/param.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "actions.h"
#include "conf.h"
#include "dialogs.h"
#include "ipc.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "stats.h"
#include "torrent-inspector.h"
#include "tr_core.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "tr_window.h"
#include "util.h"
#include "ui.h"

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

/* interval in milliseconds to update the torrent list display */
#define UPDATE_INTERVAL         1000

/* interval in milliseconds to check for stopped torrents and update display */
#define EXIT_CHECK_INTERVAL     500

/* number of fatal signals required to cause an immediate exit */
#define SIGCOUNT_MAX            3

#if GTK_CHECK_VERSION(2,8,0)
#define SHOW_LICENSE
static const char * LICENSE = 
"The Transmission binaries and most of its source code is distributed "
"license. "
"\n\n"
"Some files are copyrighted by Charles Kerr and are covered by "
"the GPL version 2.  Works owned by the Transmission project "
"are granted a special exemption to clause 2(b) so that the bulk "
"of its code can remain under the MIT license.  This exemption does "
"not extend to original or derived works not owned by the "
"Transmission project. "
"\n\n"
"Permission is hereby granted, free of charge, to any person obtaining "
"a copy of this software and associated documentation files (the "
"'Software'), to deal in the Software without restriction, including "
"without limitation the rights to use, copy, modify, merge, publish, "
"distribute, sublicense, and/or sell copies of the Software, and to "
"permit persons to whom the Software is furnished to do so, subject to "
"the following conditions: "
"\n\n"
"The above copyright notice and this permission notice shall be included "
"in all copies or substantial portions of the Software. "
"\n\n"
"THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, "
"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. "
"IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY "
"CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
"TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
"SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";
#endif

struct cbdata
{
    GtkWindow    * wind;
    TrCore       * core;
    GtkWidget    * icon;
    GtkWidget    * msgwin;
    GtkWidget    * prefs;
    guint          timer;
    gboolean       closing;
    GList        * errqueue;
    GHashTable   * tor2details;
    GHashTable   * details2tor;
};

#define CBDATA_PTR "callback-data-pointer"

static GtkUIManager * myUIManager = NULL;

static sig_atomic_t global_sigcount = 0;

static gboolean
sendremote( GList * files, gboolean sendquit );
static void
appsetup( TrWindow * wind, GList * args,
          struct cbdata *,
          gboolean paused, gboolean minimized );
static void
winsetup( struct cbdata * cbdata, TrWindow * wind );
static void
makeicon( struct cbdata * cbdata );
static void
wannaquit( void * vdata );
static void
setupdrag(GtkWidget *widget, struct cbdata *data);
static void
gotdrag(GtkWidget *widget, GdkDragContext *dc, gint x, gint y,
        GtkSelectionData *sel, guint info, guint time, gpointer gdata);

static void
coreerr( TrCore * core, enum tr_core_err code, const char * msg,
         gpointer gdata );
static void
coreprompt( TrCore *, GList *, enum tr_torrent_action, gboolean, gpointer );
static void
corepromptdata( TrCore *, uint8_t *, size_t, gboolean, gpointer );
static void
initializeFromPrefs( struct cbdata * cbdata );
static void
prefschanged( TrCore * core, const char * key, gpointer data );
static gboolean
updatemodel(gpointer gdata);
static GList *
getselection( struct cbdata * cbdata );

static void
setupsighandlers(void);
static void
fatalsig(int sig);

struct counts_data
{
    int totalCount;
    int activeCount;
    int inactiveCount;
};

static void
accumulateStatusForeach( GtkTreeModel * model,
                         GtkTreePath  * path UNUSED,
                         GtkTreeIter  * iter,
                         gpointer       user_data )
{
    int status = 0;
    struct counts_data * counts = user_data;

    ++counts->totalCount;

    gtk_tree_model_get( model, iter, MC_STATUS, &status, -1 );

    if( TR_STATUS_IS_ACTIVE( status ) )
        ++counts->activeCount;
    else
        ++counts->inactiveCount;
}

static void
accumulateCanUpdateForeach (GtkTreeModel * model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter  * iter,
                            gpointer       accumulated_status)
{
    tr_torrent * tor;
    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    *(int*)accumulated_status |= tr_torrentCanManualUpdate( tor );
}

static void
refreshTorrentActions( GtkTreeSelection * s )
{
    int canUpdate;
    struct counts_data counts;

    counts.activeCount = 0;
    counts.inactiveCount = 0;
    counts.totalCount = 0;
    gtk_tree_selection_selected_foreach( s, accumulateStatusForeach, &counts );
    action_sensitize( "pause-torrent", counts.activeCount!=0 );
    action_sensitize( "start-torrent", counts.inactiveCount!=0 );
    action_sensitize( "remove-torrent", counts.totalCount!=0 );
    action_sensitize( "verify-torrent", counts.totalCount!=0 );
    action_sensitize( "show-torrent-details", counts.totalCount==1 );

    canUpdate = 0;
    gtk_tree_selection_selected_foreach( s, accumulateCanUpdateForeach, &canUpdate );
    action_sensitize( "update-tracker", canUpdate!=0 );
}

static void
selectionChangedCB( GtkTreeSelection * s, gpointer unused UNUSED )
{
    refreshTorrentActions( s );
}

int
main( int argc, char ** argv )
{
    char * err;
    struct cbdata * cbdata;
    GList * argfiles;
    GError * gerr;
    gboolean didinit = FALSE;
    gboolean didlock = FALSE;
    gboolean sendquit = FALSE;
    gboolean startpaused = FALSE;
    gboolean startminimized = FALSE;
    char * domain = "transmission";
    GOptionEntry entries[] = {
        { "paused", 'p', 0, G_OPTION_ARG_NONE, &startpaused,
          _("Start with all torrents paused"), NULL },
        { "quit", 'q', 0, G_OPTION_ARG_NONE, &sendquit,
          _( "Request that the running instance quit"), NULL },
#ifdef STATUS_ICON_SUPPORTED
        { "minimized", 'm', 0, G_OPTION_ARG_NONE, &startminimized,
          _( "Start minimized in system tray"), NULL },
#endif
        { NULL, 0, 0, 0, NULL, NULL, NULL }
    };

    cbdata = g_new0( struct cbdata, 1 );
    cbdata->tor2details = g_hash_table_new( g_str_hash, g_str_equal );
    cbdata->details2tor = g_hash_table_new( g_direct_hash, g_direct_equal );

    /* bind the gettext domain */
    bindtextdomain( domain, TRANSMISSIONLOCALEDIR );
    bind_textdomain_codeset( domain, "UTF-8" );
    textdomain( domain );
    g_set_application_name( _( "Transmission" ) );

    /* initialize gtk */
    g_thread_init( NULL );
    gerr = NULL;
    if( !gtk_init_with_args( &argc, &argv, _("[torrent files]"), entries, domain, &gerr ) ) {
        g_message( "%s", gerr->message );
        g_clear_error( &gerr );
        return 0;
    }

    didinit = cf_init( tr_getPrefsDirectory(), NULL ); /* must come before actions_init */
    tr_prefs_init_global( );
    myUIManager = gtk_ui_manager_new ();
    actions_init ( myUIManager, cbdata );
    gtk_ui_manager_add_ui_from_string (myUIManager, fallback_ui_file, -1, NULL);
    gtk_ui_manager_ensure_update (myUIManager);
    gtk_window_set_default_icon_name ( "transmission-logo" );

    argfiles = checkfilenames( argc-1, argv+1 );
    didlock = didinit && sendremote( argfiles, sendquit );
    setupsighandlers( ); /* set up handlers for fatal signals */

    if( ( didinit || cf_init( tr_getPrefsDirectory(), &err ) ) &&
        ( didlock || cf_lock( &err ) ) )
    {
        cbdata->core = tr_core_new( );

        /* create main window now to be a parent to any error dialogs */
        GtkWindow * mainwind = GTK_WINDOW( tr_window_new( myUIManager, cbdata->core ) );

        /* set message level here before tr_init() */
        msgwin_loadpref( );

        appsetup( mainwind, argfiles, cbdata, startpaused, startminimized );
    }
    else
    {
        gtk_widget_show( errmsg_full( NULL, (callbackfunc_t)gtk_main_quit,
                                      NULL, "%s", err ) );
        g_free( err );
    }

    freestrlist(argfiles);

    gtk_main();

    return 0;
}

static gboolean
sendremote( GList * files, gboolean sendquit )
{
    const gboolean didlock = cf_lock( NULL );

    /* send files if there's another instance, otherwise start normally */
    if( !didlock && files )
        exit( ipc_sendfiles_blocking( files ) ? 0 : 1 );

    /* either send a quit message or exit if no other instance */
    if( sendquit )
        exit( didlock ? 0 : !ipc_sendquit_blocking() );

    return didlock;
}

static void
appsetup( TrWindow * wind, GList * args,
          struct cbdata * cbdata,
          gboolean paused, gboolean minimized )
{
    enum tr_torrent_action action;

    /* fill out cbdata */
    cbdata->wind       = NULL;
    cbdata->icon       = NULL;
    cbdata->msgwin     = NULL;
    cbdata->prefs      = NULL;
    cbdata->timer      = 0;
    cbdata->closing    = FALSE;
    cbdata->errqueue   = NULL;

    actions_set_core( cbdata->core );

    /* set up core handlers */
    g_signal_connect( cbdata->core, "error", G_CALLBACK( coreerr ), cbdata );
    g_signal_connect( cbdata->core, "directory-prompt",
                      G_CALLBACK( coreprompt ), cbdata );
    g_signal_connect( cbdata->core, "directory-prompt-data",
                      G_CALLBACK( corepromptdata ), cbdata );
    g_signal_connect_swapped( cbdata->core, "quit",
                              G_CALLBACK( wannaquit ), cbdata );
    g_signal_connect( cbdata->core, "prefs-changed",
                      G_CALLBACK( prefschanged ), cbdata );

    /* apply a few prefs */

    if( minimized )
        tr_core_set_pref_bool( cbdata->core, PREF_KEY_SYSTRAY, TRUE );
    initializeFromPrefs( cbdata );

    /* add torrents from command-line and saved state */
    tr_core_load( cbdata->core, paused );

    if( NULL != args )
    {
        action = tr_prefs_get_action( PREF_KEY_ADDIPC );
        tr_core_add_list( cbdata->core, args, action, paused );
    }
    tr_core_torrents_added( cbdata->core );

    /* set up the ipc socket */
    ipc_socket_setup( GTK_WINDOW( wind ), cbdata->core );

    /* set up main window */
    winsetup( cbdata, wind );

    /* start model update timer */
    cbdata->timer = g_timeout_add( UPDATE_INTERVAL, updatemodel, cbdata );
    updatemodel( cbdata );

    /* show the window */
    if( !minimized )
        gtk_widget_show( GTK_WIDGET(wind) );
}

static gboolean
winclose( GtkWidget * w UNUSED, GdkEvent * event UNUSED, gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->icon != NULL )
        gtk_widget_hide( GTK_WIDGET( cbdata->wind ) );
    else
        askquit( cbdata->core, cbdata->wind, wannaquit, cbdata );

    return TRUE; /* don't propagate event further */
}

static void
rowChangedCB( GtkTreeModel  * model UNUSED,
              GtkTreePath   * path,
              GtkTreeIter   * iter UNUSED,
              gpointer        sel)
{
    if( gtk_tree_selection_path_is_selected ( sel, path ) )
        refreshTorrentActions( GTK_TREE_SELECTION(sel) );
}

static void
winsetup( struct cbdata * cbdata, TrWindow * wind )
{
    GtkTreeModel * model;
    GtkTreeSelection * sel;

    g_assert( NULL == cbdata->wind );
    cbdata->wind = GTK_WINDOW( wind );

    sel = tr_window_get_selection( cbdata->wind );
    g_signal_connect( sel, "changed", G_CALLBACK(selectionChangedCB), NULL );
    selectionChangedCB( sel, NULL );
    model = tr_core_model( cbdata->core );
    g_signal_connect( model, "row-changed", G_CALLBACK(rowChangedCB), sel );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    refreshTorrentActions( sel );
    
    setupdrag( GTK_WIDGET(wind), cbdata );
}

static void
makeicon( struct cbdata * cbdata )
{
    if( cbdata->icon == NULL )
        cbdata->icon = tr_icon_new( );
}

static gpointer
quitThreadFunc( gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    tr_close( tr_core_handle( cbdata->core ) );

    /* shutdown the gui */
    if( cbdata->prefs )
        gtk_widget_destroy( GTK_WIDGET( cbdata->prefs ) );
    if( cbdata->wind )
        gtk_widget_destroy( GTK_WIDGET( cbdata->wind ) );
    g_object_unref( cbdata->core );
    if( cbdata->icon )
        g_object_unref( cbdata->icon );
    if( cbdata->errqueue ) {
        g_list_foreach( cbdata->errqueue, (GFunc)g_free, NULL );
        g_list_free( cbdata->errqueue );
    }

    g_hash_table_destroy( cbdata->details2tor );
    g_hash_table_destroy( cbdata->tor2details );
    g_free( cbdata );

    /* exit the gtk main loop */
    gtk_main_quit( );
    return NULL;
}

/* since there are no buttons in the dialog, gtk tries to
 * select one of the labels, which looks ugly... so force
 * the dialog's primary and secondary labels to be unselectable */
static void
deselectLabels( GtkWidget * w, gpointer unused UNUSED )
{
    if( GTK_IS_LABEL( w ) )
        gtk_label_set_selectable( GTK_LABEL(w), FALSE );
    else if( GTK_IS_CONTAINER( w ) )
        gtk_container_foreach( GTK_CONTAINER(w), deselectLabels, NULL );
}

static void
do_exit_cb( GtkWidget *w UNUSED, gpointer data UNUSED )
{
    exit( 0 );
}

static void
wannaquit( void * vdata )
{
    char * str;
    GtkWidget * r, * p, * b, * w, *c;
    struct cbdata * cbdata = vdata;

    /* stop the update timer */
    if( cbdata->timer ) {
        g_source_remove( cbdata->timer );
        cbdata->timer = 0;
    }

    c = GTK_WIDGET( cbdata->wind );
    gtk_container_remove( GTK_CONTAINER( c ), gtk_bin_get_child( GTK_BIN( c ) ) );

    r = gtk_alignment_new(0.5, 0.5, 0.01, 0.01);
    gtk_container_add(GTK_CONTAINER(c), r);

    p = gtk_table_new(2, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(r), p);

    w = gtk_image_new_from_stock( GTK_STOCK_NETWORK, GTK_ICON_SIZE_DIALOG );
    gtk_table_attach(GTK_TABLE(p), w, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 2, 2);

    w = gtk_label_new("");
    str = g_strdup_printf( "<b>%s</b>\n%s", _("Closing Connections"), _("Sending upload/download totals to tracker...") );
    gtk_label_set_markup(GTK_LABEL(w), str );
    gtk_table_attach(GTK_TABLE(p), w, 1, 2, 0, 1, GTK_FILL, GTK_FILL, 10, 0);
    g_free( str );

    b = gtk_alignment_new(0.0, 1.0, 0.01, 0.01);
    w = gtk_button_new_with_label( _( "_Quit Immediately" ) );
    gtk_button_set_image( GTK_BUTTON(w), gtk_image_new_from_stock( GTK_STOCK_QUIT, GTK_ICON_SIZE_BUTTON ) );
    g_signal_connect(w, "clicked", G_CALLBACK(do_exit_cb), NULL);
    gtk_container_add(GTK_CONTAINER(b), w);
    gtk_table_attach(GTK_TABLE(p), b, 1, 2, 1, 2, GTK_FILL, GTK_FILL, 10, 0);

    gtk_widget_show_all(r);

    /* clear the UI */
    gtk_list_store_clear( GTK_LIST_STORE( tr_core_model( cbdata->core ) ) );

    /* shut down libT */
    g_thread_create( quitThreadFunc, vdata, TRUE, NULL );
}

static void
gotdrag( GtkWidget         * widget UNUSED,
         GdkDragContext    * dc,
         gint                x UNUSED,
         gint                y UNUSED,
         GtkSelectionData  * sel,
         guint               info UNUSED,
         guint               time,
         gpointer            gdata )
{
    struct cbdata * data = gdata;
    GList * paths = NULL;
    GList * freeme = NULL;

#ifdef DND_DEBUG
    char *sele = gdk_atom_name(sel->selection);
    char *targ = gdk_atom_name(sel->target);
    char *type = gdk_atom_name(sel->type);

    g_message( "dropped file: sel=%s targ=%s type=%s fmt=%i len=%i",
               sele, targ, type, sel->format, sel->length );
    g_free(sele);
    g_free(targ);
    g_free(type);
    if( sel->format == 8 ) {
        for( i=0; i<sel->length; ++i )
            fprintf(stderr, "%02X ", sel->data[i]);
        fprintf(stderr, "\n");
    }
#endif

    if( ( sel->format == 8 ) &&
        ( sel->selection == gdk_atom_intern( "XdndSelection", FALSE ) ) )
    {
        int i;
        char * str = g_strndup( (char*)sel->data, sel->length );
        gchar ** files = g_strsplit_set( str, "\r\n", -1 );
        for( i=0; files && files[i]; ++i )
        {
            char * filename;
            if( !*files[i] ) /* empty filename... */
                continue;

            /* decode the filename */
            filename = urldecode( files[i], -1 );
            freeme = g_list_prepend( freeme, filename );
            if( !g_utf8_validate( filename, -1, NULL ) )
                continue;

            /* walk past "file://", if present */
            if( g_str_has_prefix( filename, "file:" ) ) {
                filename += 5;
                while( g_str_has_prefix( filename, "//" ) )
                    ++filename;
            }

            /* if the file doesn't exist, the first part
               might be a hostname ... walk past it. */
            if( !g_file_test( filename, G_FILE_TEST_EXISTS ) ) {
                char * pch = strchr( filename + 1, '/' );
                if( pch != NULL )
                    filename = pch;
            }

            /* finally, add it to the list of torrents to try adding */
            if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
                paths = g_list_prepend( paths, filename );
        }

        /* try to add any torrents we found */
        if( paths != NULL )
        {
            enum tr_torrent_action action = tr_prefs_get_action( PREF_KEY_ADDSTD );
            paths = g_list_reverse( paths );
            tr_core_add_list( data->core, paths, action, FALSE );
            tr_core_torrents_added( data->core );
            g_list_free( paths );
        }

        freestrlist( freeme );
        g_strfreev( files );
        g_free( str );
    }

    gtk_drag_finish(dc, (NULL != paths), FALSE, time);
}

static void
setupdrag(GtkWidget *widget, struct cbdata *data) {
  GtkTargetEntry targets[] = {
    { "STRING",     0, 0 },
    { "text/plain", 0, 0 },
    { "text/uri-list", 0, 0 },
  };

  g_signal_connect(widget, "drag_data_received", G_CALLBACK(gotdrag), data);

  gtk_drag_dest_set(widget, GTK_DEST_DEFAULT_ALL, targets,
                    ALEN(targets), GDK_ACTION_COPY | GDK_ACTION_MOVE);
}

static void
coreerr( TrCore * core UNUSED, enum tr_core_err code, const char * msg,
         gpointer gdata )
{
    struct cbdata * cbdata = gdata;
    char          * joined;

    switch( code )
    {
        case TR_CORE_ERR_ADD_TORRENT:
            cbdata->errqueue = g_list_append( cbdata->errqueue,
                                              g_strdup( msg ) );
            return;
        case TR_CORE_ERR_NO_MORE_TORRENTS:
            if( NULL != cbdata->errqueue )
            {
                joined = joinstrlist( cbdata->errqueue, "\n" );
                errmsg( cbdata->wind,
                        ngettext( "Failed to load torrent file:\n%s",
                                  "Failed to load torrent files:\n%s",
                                  g_list_length( cbdata->errqueue ) ),
                        joined );
                g_list_foreach( cbdata->errqueue, (GFunc) g_free, NULL );
                g_list_free( cbdata->errqueue );
                cbdata->errqueue = NULL;
                g_free( joined );
            }
            return;
        case TR_CORE_ERR_SAVE_STATE:
            errmsg( cbdata->wind, "%s", msg );
            return;
    }

    g_assert_not_reached();
}

void
coreprompt( TrCore * core, GList * paths, enum tr_torrent_action act,
            gboolean paused, gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    promptfordir( cbdata->wind, core, paths, NULL, 0, act, paused );
}

void
corepromptdata( TrCore * core, uint8_t * data, size_t size,
                gboolean paused, gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    promptfordir( cbdata->wind, core, NULL, data, size, TR_TOR_LEAVE, paused );
}

static void
initializeFromPrefs( struct cbdata * cbdata )
{
    prefschanged( NULL, PREF_KEY_SYSTRAY, cbdata );
}

static void
prefschanged( TrCore * core UNUSED, const char * key, gpointer data )
{
    struct cbdata * cbdata = data;
    tr_handle     * tr     = tr_core_handle( cbdata->core );

    if( !strcmp( key, PREF_KEY_ENCRYPTED_ONLY ) )
    {
        const gboolean crypto_only = pref_flag_get( key );
        tr_setEncryptionMode( tr, crypto_only ? TR_ENCRYPTION_REQUIRED
                                              : TR_ENCRYPTION_PREFERRED );
    }
    else if( !strcmp( key, PREF_KEY_PORT ) )
    {
        const int port = pref_int_get( key );
        tr_setBindPort( tr, port );
    }
    else if( !strcmp( key, PREF_KEY_DL_LIMIT_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_setUseGlobalSpeedLimit( tr, TR_DOWN, b );
    }
    else if( !strcmp( key, PREF_KEY_DL_LIMIT ) )
    {
        const int limit = pref_int_get( key );
        tr_setGlobalSpeedLimit( tr, TR_DOWN, limit );
    }
    else if( !strcmp( key, PREF_KEY_UL_LIMIT_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_setUseGlobalSpeedLimit( tr, TR_UP, b );
    }
    else if( !strcmp( key, PREF_KEY_UL_LIMIT ) )
    {
        const int limit = pref_int_get( key );
        tr_setGlobalSpeedLimit( tr, TR_UP, limit );
    }
    else if( !strcmp( key, PREF_KEY_NAT ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_natTraversalEnable( tr, enabled );
    }
    else if( !strcmp( key, PREF_KEY_SYSTRAY ) )
    {
        if( pref_flag_get( key ) )
        {
            makeicon( cbdata );
        }
        else if( cbdata->icon )
        {
            g_object_unref( cbdata->icon );
            cbdata->icon = NULL;
        }
    }
    else if( !strcmp( key, PREF_KEY_PEX ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_setPexEnabled( tr_core_handle( cbdata->core ), enabled );
    }
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;

  if( !data->closing && 0 < global_sigcount )
  {
      wannaquit( data );
      return FALSE;
  }

  /* update the torrent data in the model */
  tr_core_update( data->core );

  /* update the main window's statusbar and toolbar buttons */
  if( data->wind )
      tr_window_update( data->wind );

  /* update the message window */
  msgwin_update();

  return TRUE;
}

/* returns a GList of GtkTreeRowReferences to each selected row */
static GList *
getselection( struct cbdata * cbdata )
{
    GList * rows = NULL;

    if( NULL != cbdata->wind )
    {
        GList * l;
        GtkTreeSelection *s = tr_window_get_selection(cbdata->wind);
        GtkTreeModel * filter_model;
        GtkTreeModel * store_model;
        rows = gtk_tree_selection_get_selected_rows( s, &filter_model );
        store_model = gtk_tree_model_filter_get_model(
                                                 GTK_TREE_MODEL_FILTER( filter_model ) );
        for( l=rows; l!=NULL; l=l->next )
        {
            GtkTreePath * path = gtk_tree_model_filter_convert_path_to_child_path(
                                        GTK_TREE_MODEL_FILTER( filter_model ), l->data );
            GtkTreeRowReference * ref = gtk_tree_row_reference_new( store_model, path );
            gtk_tree_path_free( path );
            gtk_tree_path_free( l->data );
            l->data = ref;
        }
    }

    return rows;
}

static void
about ( void )
{
  GtkWidget * w = gtk_about_dialog_new ();
  GtkAboutDialog * a = GTK_ABOUT_DIALOG (w);
  const char *authors[] = { "Charles Kerr (Backend; GTK+)",
                            "Mitchell Livingston (Backend; OS X)",
                            "Eric Petit (Backend; OS X)",
                            "Josh Elsasser (Daemon; Backend; GTK+)",
                            "Bryan Varner (BeOS)", 
                            NULL };
  gtk_about_dialog_set_version (a, LONG_VERSION_STRING );
#ifdef SHOW_LICENSE
  gtk_about_dialog_set_license (a, LICENSE);
  gtk_about_dialog_set_wrap_license (a, TRUE);
#endif
  gtk_about_dialog_set_logo_icon_name( a, "transmission-logo" );
  gtk_about_dialog_set_comments( a, _("A fast and easy BitTorrent client") );
  gtk_about_dialog_set_website( a, "http://www.transmissionbt.com/" );
  gtk_about_dialog_set_copyright( a, _("Copyright 2005-2008 The Transmission Project") );
  gtk_about_dialog_set_authors( a, authors );
  /* note to translators: put yourself here for credit in the "About" dialog */
  gtk_about_dialog_set_translator_credits( a, _("translator-credits") );
  g_signal_connect_swapped( w, "response", G_CALLBACK (gtk_widget_destroy), w );
  gtk_widget_show_all( w );
}

static void
startTorrentForeach (GtkTreeModel * model,
                     GtkTreePath  * path UNUSED,
                     GtkTreeIter  * iter,
                     gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    tr_torrent_start( tor );
    g_object_unref( G_OBJECT( tor ) );
}

static void
stopTorrentForeach (GtkTreeModel * model,
                    GtkTreePath  * path UNUSED,
                    GtkTreeIter  * iter,
                    gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    tr_torrent_stop( tor );
    g_object_unref( G_OBJECT( tor ) );
}

static void
updateTrackerForeach (GtkTreeModel * model,
                      GtkTreePath  * path UNUSED,
                      GtkTreeIter  * iter,
                      gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    tr_manualUpdate( tr_torrent_handle( tor ) );
    g_object_unref( G_OBJECT( tor ) );
}

static void
detailsClosed( gpointer user_data, GObject * details )
{
    struct cbdata * data = user_data;
    gpointer hashString = g_hash_table_lookup( data->details2tor, details );
    g_hash_table_remove( data->details2tor, details );
    g_hash_table_remove( data->tor2details, hashString );
}

static void
showInfoForeach (GtkTreeModel * model,
                 GtkTreePath  * path UNUSED,
                 GtkTreeIter  * iter,
                 gpointer       user_data )
{
    const char * hashString;
    struct cbdata * data = user_data;
    TrTorrent * tor = NULL;
    GtkWidget * w;

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    hashString = tr_torrent_info(tor)->hashString;
    w = g_hash_table_lookup( data->tor2details, hashString );
    if( w != NULL )
        gtk_window_present( GTK_WINDOW( w ) );
    else {
        w = torrent_inspector_new( GTK_WINDOW( data->wind ), tor );
        gtk_widget_show( w );
        g_hash_table_insert( data->tor2details, (gpointer)hashString, w );
        g_hash_table_insert( data->details2tor, w, (gpointer)hashString );
        g_object_weak_ref( G_OBJECT( w ), detailsClosed, data );
    }

    g_object_unref( G_OBJECT( tor ) );
}

static void
recheckTorrentForeach (GtkTreeModel * model,
                       GtkTreePath  * path UNUSED,
                       GtkTreeIter  * iter,
                       gpointer       data UNUSED)
{
    TrTorrent * gtor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &gtor, -1 );
    tr_torrentRecheck( tr_torrent_handle( gtor ) );
    g_object_unref( G_OBJECT( gtor ) );
}

static gboolean 
msgwinclosed()
{
  action_toggle( "toggle-message-log", FALSE );
  return FALSE;
}

static void
toggleMainWindow( struct cbdata * data )
{
    static int x=0, y=0;
    GtkWidget * w = GTK_WIDGET( data->wind );
    GtkWindow * window = GTK_WINDOW( w );

    if( GTK_WIDGET_VISIBLE( w ) )
    {
        gtk_window_get_position( window, &x, &y );
        gtk_widget_hide( w );
    }
    else
    {
        gtk_window_move( window, x, y );
        gtk_window_present( window );
    }
}

void
doAction ( const char * action_name, gpointer user_data )
{
    struct cbdata * data = user_data;
    gboolean changed = FALSE;

    if (!strcmp (action_name, "add-torrent"))
    {
        makeaddwind( data->wind, data->core );
    }
    else if (!strcmp (action_name, "show-stats"))
    {
        GtkWidget * dialog = stats_dialog_create( data->wind,
                                                  data->core );
        gtk_widget_show( dialog );
    }
    else if (!strcmp (action_name, "start-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, startTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "pause-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, stopTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "verify-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, recheckTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "show-torrent-details"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, showInfoForeach, data );
    }
    else if (!strcmp( action_name, "update-tracker"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, updateTrackerForeach, data->wind );
    }
    else if (!strcmp (action_name, "create-torrent"))
    {
        GtkWidget * w = make_meta_ui( GTK_WINDOW( data->wind ),
                                      tr_core_handle( data->core ) );
        gtk_widget_show_all( w );
    }
    else if (!strcmp (action_name, "remove-torrent"))
    {
        /* this modifies the model's contents, so can't use foreach */
        GList *l, *sel = getselection( data );
        GtkTreeModel *model = tr_core_model( data->core );
        gtk_tree_selection_unselect_all( tr_window_get_selection( data->wind) );
        for( l=sel; l!=NULL; l=l->next )
        {
            GtkTreeIter iter;
            GtkTreeRowReference * reference = l->data;
            GtkTreePath * path = gtk_tree_row_reference_get_path( reference );
            gtk_tree_model_get_iter( model, &iter, path );
            tr_core_delete_torrent( data->core, &iter );
            gtk_tree_row_reference_free( reference );
            changed = TRUE;
        }
        g_list_free( sel );
    }
    else if (!strcmp (action_name, "close"))
    {
        if( data->wind != NULL )
            winclose( NULL, NULL, data );
    }
    else if (!strcmp (action_name, "quit"))
    {
        askquit( data->core, data->wind, wannaquit, data );
    }
    else if (!strcmp (action_name, "select-all"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_select_all( s );
    }
    else if (!strcmp (action_name, "unselect-all"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_unselect_all( s );
    }
    else if (!strcmp (action_name, "edit-preferences"))
    {
        if( NULL == data->prefs )
        {
            data->prefs = tr_prefs_dialog_new( G_OBJECT(data->core), data->wind );
            g_signal_connect( data->prefs, "destroy",
                             G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
            gtk_widget_show( GTK_WIDGET( data->prefs ) );
        }
    }
    else if (!strcmp (action_name, "toggle-message-log"))
    {
        if( !data->msgwin )
        {
            GtkWidget * win = msgwin_create( data->core );
            g_signal_connect( win, "destroy", G_CALLBACK( msgwinclosed ), 
                             NULL );
            data->msgwin = win;
        }
        else
        {
            action_toggle("toggle-message-log", FALSE);
            gtk_widget_destroy( data->msgwin );
            data->msgwin = NULL;
        }
    }
    else if (!strcmp (action_name, "show-about-dialog"))
    {
        about();
    }
    else if (!strcmp (action_name, "toggle-main-window"))
    {
        toggleMainWindow( data );
    }
    else g_error ("Unhandled action: %s", action_name );

    if( changed )
        updatemodel( data );
}


static void
setupsighandlers(void) {
  int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM};
  struct sigaction sa;
  int ii;

  memset(&sa, 0,  sizeof(sa));
  sa.sa_handler = fatalsig;
  for(ii = 0; ii < ALEN(sigs); ii++)
    sigaction(sigs[ii], &sa, NULL);
}

static void
fatalsig(int sig) {
  struct sigaction sa;

  if(SIGCOUNT_MAX <= ++global_sigcount) {
    memset(&sa, 0,  sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
  }
}
