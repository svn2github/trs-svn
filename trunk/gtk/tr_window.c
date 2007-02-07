/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "transmission.h"

#include "tr_cell_renderer_progress.h"
#include "tr_torrent.h"
#include "tr_window.h"
#include "util.h"

#define MENU_ITEM_ACTION        "tr-menu-item-action"

enum
{
    PROP_MODEL = 1,
    PROP_ELLIPSIZE,
    PROP_SELECTION,
    PROP_DOUBLECLICK,
    PROP_DRAG,
};

static void
tr_window_init( GTypeInstance * instance, gpointer g_class );
static void
tr_window_set_property( GObject * object, guint property_id,
                        const GValue * value, GParamSpec * pspec );
static void
tr_window_get_property( GObject * object, guint property_id,
                        GValue * value, GParamSpec * pspec);
static void
tr_window_class_init( gpointer g_class, gpointer g_class_data );
static void
tr_window_dispose( GObject * obj );
static void
tr_window_finalize( GObject * obj );
static GtkTreeView *
makeview( TrWindow * self );
static void
stylekludge( GObject * obj, GParamSpec * spec, gpointer data );
static void
toolclick( GtkWidget * item, gpointer data );
static void
fixbuttons( GtkTreeSelection *sel, TrWindow * self );
static void
formatname( GtkTreeViewColumn * col, GtkCellRenderer * rend,
            GtkTreeModel * model, GtkTreeIter * iter, gpointer data );
static void
formatprog( GtkTreeViewColumn * col, GtkCellRenderer * rend,
            GtkTreeModel * model, GtkTreeIter * iter, gpointer data );
static gboolean
listclick( GtkWidget * view, GdkEventButton * event, gpointer data );
static gboolean
listpopup( GtkWidget * view SHUTUP, gpointer data );
static void
popupmenu( TrWindow * self, GdkEventButton * event );
static void
menuclick( GtkWidget * item, gpointer data );
static void
doubleclick( GtkWidget * view, GtkTreePath * path,
             GtkTreeViewColumn * col SHUTUP, gpointer data );
static void
emitaction( TrWindow * self, int id );
static void
orstatus( GtkTreeModel * model, GtkTreePath * path SHUTUP, GtkTreeIter * iter,
          gpointer data );
static void
istorsel( GtkTreeModel * model, GtkTreePath * path SHUTUP, GtkTreeIter * iter,
          gpointer data );

GType
tr_window_get_type( void )
{
    static GType type = 0;

    if( 0 == type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrWindowClass ),
            NULL,                       /* base_init */
            NULL,                       /* base_finalize */
            tr_window_class_init,       /* class_init */
            NULL,                       /* class_finalize */
            NULL,                       /* class_data */
            sizeof( TrWindow ),
            0,                          /* n_preallocs */
            tr_window_init,             /* instance_init */
            NULL,
        };
        type = g_type_register_static( GTK_TYPE_WINDOW, "TrWindow", &info, 0 );
    }

    return type;
}

static void
tr_window_class_init( gpointer g_class, gpointer g_class_data SHUTUP )
{
    GObjectClass  * gobject_class;
    TrWindowClass * trwindow_class;
    GParamSpec   * pspec;

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->set_property = tr_window_set_property;
    gobject_class->get_property = tr_window_get_property;
    gobject_class->dispose      = tr_window_dispose;
    gobject_class->finalize     = tr_window_finalize;

    pspec = g_param_spec_object( "model", _("Model"),
                                 _("The GtkTreeModel for the list view."),
                                 GTK_TYPE_TREE_MODEL, G_PARAM_READWRITE );
    g_object_class_install_property( gobject_class, PROP_MODEL, pspec );

    pspec = g_param_spec_boolean( "ellipsize", _("Ellipsize"),
                                 _("Ellipsize torrent names."),
                                 FALSE, G_PARAM_READWRITE );
    g_object_class_install_property( gobject_class, PROP_ELLIPSIZE, pspec );

    pspec = g_param_spec_object( "selection", _("Selection"),
                                 _("The GtkTreeSelection for the list view."),
                                 GTK_TYPE_TREE_SELECTION, G_PARAM_READABLE );
    g_object_class_install_property( gobject_class, PROP_SELECTION, pspec );

    pspec = g_param_spec_int( "double-click-action", _("Double-click action"),
                              _("The action id to signal on a double click."),
                              G_MININT, G_MAXINT, -1, G_PARAM_READWRITE );
    g_object_class_install_property( gobject_class, PROP_DOUBLECLICK, pspec );

    pspec = g_param_spec_object( "drag-widget", _("Drag widget"),
                                 _("The GtkWidget used for drag-and-drop."),
                                 GTK_TYPE_WIDGET, G_PARAM_READABLE );
    g_object_class_install_property( gobject_class, PROP_DRAG, pspec );

    trwindow_class = TR_WINDOW_CLASS( g_class );
    trwindow_class->actionsig =
        g_signal_new( "action", G_TYPE_FROM_CLASS( g_class ),
                       G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                       g_cclosure_marshal_VOID__INT,
                       G_TYPE_NONE, 1, G_TYPE_INT );
}

static void
tr_window_init( GTypeInstance * instance, gpointer g_class SHUTUP )
{
    TrWindow * self = ( TrWindow * )instance;
    GtkWidget * vbox, * scroll, * status, * tools;

    vbox   = gtk_vbox_new( FALSE, 0 );
    scroll = gtk_scrolled_window_new( NULL, NULL );
    status = gtk_statusbar_new();
    tools  = gtk_toolbar_new();

    self->scroll          = GTK_SCROLLED_WINDOW( scroll );
    self->view            = makeview( self );
    self->status          = GTK_STATUSBAR( status );
    self->toolbar         = GTK_TOOLBAR( tools );
    /* this should have been set by makeview() */
    g_assert( NULL != self->namerend );
    self->doubleclick     = -1;
    self->actions         = NULL;
    self->count           = 0;
    self->stupidpopuphack = NULL;
    self->disposed        = FALSE;

    gtk_toolbar_set_tooltips( self->toolbar, TRUE );
    gtk_toolbar_set_show_arrow( self->toolbar, FALSE );
    gtk_scrolled_window_set_policy( self->scroll,
                                    GTK_POLICY_NEVER, GTK_POLICY_ALWAYS );
    gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( self->toolbar) ,
                        FALSE, FALSE, 0 );

    gtk_container_add( GTK_CONTAINER( scroll ), GTK_WIDGET( self->view ) );
    gtk_box_pack_start( GTK_BOX( vbox ), scroll, TRUE, TRUE, 0 );

    gtk_statusbar_push( self->status, 0, "" );
    gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET( self->status ),
                        FALSE, FALSE, 0 );

    gtk_container_set_focus_child( GTK_CONTAINER( vbox ), scroll );
    gtk_widget_show_all( vbox );
    gtk_container_add( GTK_CONTAINER( self ), vbox );
    gtk_window_set_title( GTK_WINDOW( self ), g_get_application_name());
}

static void
tr_window_set_property( GObject * object, guint property_id,
                        const GValue * value SHUTUP, GParamSpec * pspec)
{
    TrWindow         * self = ( TrWindow * )object;
    PangoEllipsizeMode elip;

    if( self->disposed )
    {
        return;
    }

    switch( property_id )
    {
        case PROP_MODEL:
            gtk_tree_view_set_model( self->view, g_value_get_object( value ) );
            break;
        case PROP_ELLIPSIZE:
            g_assert( NULL != self->namerend );
            elip = ( g_value_get_boolean( value ) ?
                     PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE );
            g_object_set( self->namerend, "ellipsize", elip, NULL );
            break;
        case PROP_DOUBLECLICK:
            self->doubleclick = g_value_get_int( value );
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
tr_window_get_property( GObject * object, guint property_id,
                        GValue * value SHUTUP, GParamSpec * pspec )
{
    TrWindow         * self = ( TrWindow * )object;
    PangoEllipsizeMode elip;

    if( self->disposed )
    {
        return;
    }

    switch( property_id )
    {
        case PROP_MODEL:
            g_value_set_object( value, gtk_tree_view_get_model( self->view ) );
            break;
        case PROP_ELLIPSIZE:
            g_assert( NULL != self->namerend );
            g_object_get( self->namerend, "ellipsize", &elip, NULL);
            g_value_set_boolean( value, ( PANGO_ELLIPSIZE_NONE != elip ) );
            break;
        case PROP_SELECTION:
            g_value_set_object( value,
                                gtk_tree_view_get_selection( self->view ) );
            break;
        case PROP_DOUBLECLICK:
            g_value_set_int( value, self->doubleclick );
            break;
        case PROP_DRAG:
            g_value_set_object( value, self->view );
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
tr_window_dispose( GObject * obj )
{
    TrWindow     * self = ( TrWindow * )obj;
    GObjectClass * parent;

    if( self->disposed )
    {
        return;
    }
    self->disposed = TRUE;

    if( NULL != self->stupidpopuphack )
    {
        gtk_widget_destroy( self->stupidpopuphack );
    }
    self->stupidpopuphack = NULL;

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_WINDOW_TYPE ) );
    parent->dispose( obj );
}

static void
tr_window_finalize( GObject * obj )
{
    TrWindow     * self = ( TrWindow * )obj;
    GObjectClass * parent;
    int            ii;

    for( ii = 0; ii < self->count; ii++ )
    {
        g_free( self->actions[ii].label );
    }
    g_free( self->actions );

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_WINDOW_TYPE ) );
    parent->finalize( obj );
}

GtkWidget *
tr_window_new( void )
{
    return g_object_new( TR_WINDOW_TYPE, NULL );
}

void
tr_window_action_add( TrWindow * self, int id, int flags, const char * name,
                      const char * icon, const char * description )
{
    GtkWidget   * widget;
    GtkToolItem * item;

    TR_IS_WINDOW( self );
    if( self->disposed )
    {
        return;
    }

    widget = NULL;
    if( ACTF_TOOL & flags )
    {
        item = gtk_tool_button_new_from_stock( icon );
        gtk_tool_button_set_label( GTK_TOOL_BUTTON( item ), name );
        gtk_tool_item_set_tooltip( item, self->toolbar->tooltips,
                                   description, "" );
        g_signal_connect( item, "clicked", G_CALLBACK( toolclick ), self );
        gtk_toolbar_insert( self->toolbar, item, -1 );
        widget = GTK_WIDGET( item );
        gtk_widget_show( widget );
    }

    self->count++;
    self->actions = g_realloc( self->actions,
                               sizeof( self->actions[0] ) * self->count );
    self->actions[self->count-1].label = g_strdup( name );
    self->actions[self->count-1].tool  = widget;
    self->actions[self->count-1].id    = id;
    self->actions[self->count-1].flags = flags;
}

void
tr_window_update( TrWindow * self, float downspeed, float upspeed )
{
    char * downstr, * upstr, * str;

    TR_IS_WINDOW( self );
    if( self->disposed )
    {
        return;
    }

    /* update the status bar */
    downstr = readablesize( downspeed * 1024.0 );
    upstr   = readablesize( upspeed * 1024.0 );
    str     = g_strdup_printf( _("     Total DL: %s/s     Total UL: %s/s"),
                               downstr, upstr );
    g_free( downstr );
    g_free( upstr );
    gtk_statusbar_pop( self->status, 0 );
    gtk_statusbar_push( self->status, 0, str );
    g_free( str );

    /* the selection's status may have changed so update the buttons */
    fixbuttons( NULL, self );
}

void
tr_window_size_hack( TrWindow * self )
{
    GtkRequisition req;
    gint           width, height;
    GdkScreen    * screen;

    TR_IS_WINDOW( self );

    gtk_widget_realize( GTK_WIDGET( self ) );
    gtk_widget_size_request( GTK_WIDGET( self->view ), &req );
    height = req.height;
    gtk_widget_size_request( GTK_WIDGET( self->scroll ), &req );
    height -= req.height;
    gtk_widget_size_request( GTK_WIDGET( self ), &req );
    height += req.height;
    screen  = gtk_widget_get_screen( GTK_WIDGET( self ) );
    width   = MIN( req.width, gdk_screen_get_width( screen  ) / 2 );
    height  = MIN( height,    gdk_screen_get_height( screen ) / 5 * 4 );
    if( height > req.width )
    {
        height = MIN( height, width * 8 / 5 );
    }
    else
    {
        height = MAX( height, width * 5 / 8 );
    }
    if( height > req.width )
    {
        height = MIN( height, width * 8 / 5 );
    }
    else
    {
        height = MAX( height, width * 5 / 8 );
    }
    g_object_set( self, "ellipsize", TRUE, NULL );
    gtk_widget_show_now( GTK_WIDGET( self ) );
    gtk_window_resize( GTK_WINDOW( self ), width, height );
}

static GtkTreeView *
makeview( TrWindow * self )
{
    GtkWidget         * view;
    GtkTreeViewColumn * col;
    GtkTreeSelection  * sel;
    GtkCellRenderer   * namerend, * progrend;
    char              * str;

    TR_IS_WINDOW( self );

    view     = gtk_tree_view_new();
    namerend = gtk_cell_renderer_text_new();
    self->namerend = G_OBJECT( namerend );
    /* note that this renderer is set to ellipsize, just not here */
    col = gtk_tree_view_column_new_with_attributes( _("Name"), namerend,
                                                    NULL );
    gtk_tree_view_column_set_cell_data_func( col, namerend, formatname,
                                             NULL, NULL );
    gtk_tree_view_column_set_expand( col, TRUE );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_AUTOSIZE );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    progrend = tr_cell_renderer_progress_new();
    /* this string is only used to determing the size of the progress bar */
    str = g_markup_printf_escaped( "<big>%s</big>", _("  fnord    fnord  ") );
    g_object_set( progrend, "bar-sizing", str, NULL );
    g_free(str);
    col = gtk_tree_view_column_new_with_attributes( _("Progress"), progrend,
                                                    NULL);
    gtk_tree_view_column_set_cell_data_func( col, progrend, formatprog,
                                             NULL, NULL );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_AUTOSIZE );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* XXX this shouldn't be necessary */
    g_signal_connect( view, "notify::style",
                      G_CALLBACK( stylekludge ), progrend );

    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( view ), TRUE );
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( GTK_TREE_SELECTION( sel ),
                                 GTK_SELECTION_MULTIPLE );

    g_signal_connect( G_OBJECT( sel ), "changed",
                      G_CALLBACK( fixbuttons ), self );
    g_signal_connect( G_OBJECT( view ), "button-press-event",
                      G_CALLBACK( listclick ), self );
    g_signal_connect( G_OBJECT( view ), "popup-menu",
                      G_CALLBACK( listpopup ), self );
    g_signal_connect( G_OBJECT( view ), "row-activated",
                      G_CALLBACK( doubleclick ), self );

    return GTK_TREE_VIEW( view );
}

/* kludge to have the progress bars notice theme changes */
static void
stylekludge( GObject * obj, GParamSpec * spec, gpointer data )
{
    if( 0 == strcmp( "style", spec->name ) )
    {
        tr_cell_renderer_progress_reset_style(
            TR_CELL_RENDERER_PROGRESS( data ) );
        gtk_widget_queue_draw( GTK_WIDGET( obj ) );
    }
}

static void
toolclick( GtkWidget * item, gpointer data )
{
    TrWindow * self;
    int        ii;

    TR_IS_WINDOW( data );
    self = TR_WINDOW( data );
    if( self->disposed )
    {
        return;
    }

    for( ii = 0; ii < self->count; ii++ )
    {
        if( item == self->actions[ii].tool )
        {
            goto foundit;
        }
    }
    g_assert_not_reached();
  foundit:

    emitaction( self, self->actions[ii].id );
}

/* disable buttons the user shouldn't be able to click on */
static void
fixbuttons( GtkTreeSelection *sel, TrWindow * self ) {
    gboolean selected, avail;
    int      ii, status;

    TR_IS_WINDOW( self );
    if( self->disposed )
    {
        return;
    }

    if( NULL == sel )
    {
        sel = gtk_tree_view_get_selection( self->view );
    }
    status = 0;
    gtk_tree_selection_selected_foreach( sel, orstatus, &status );
    selected = ( 0 < gtk_tree_selection_count_selected_rows( sel ) );

    for( ii = 0; ii < self->count; ii++ )
    {
        if( !( ACTF_ALWAYS & self->actions[ii].flags ) &&
               ACTF_TOOL   & self->actions[ii].flags )
        {
            g_assert( NULL != self->actions[ii].tool );
            avail = ACT_ISAVAIL( self->actions[ii].flags, status );
            gtk_widget_set_sensitive( self->actions[ii].tool,
                                      selected && avail );
        }
    }
}

static void
formatname( GtkTreeViewColumn * col SHUTUP, GtkCellRenderer * rend,
            GtkTreeModel * model, GtkTreeIter * iter, gpointer data SHUTUP )
{
    char  * name, * mb, * terr, * str, * top, * bottom, * timestr;
    guint64 size;
    gfloat  prog;
    int     status, err, eta, tpeers, upeers, dpeers;

    gtk_tree_model_get( model, iter, MC_NAME, &name, MC_STAT, &status,
                        MC_ERR, &err, MC_SIZE, &size, MC_PROG, &prog,
                        MC_ETA, &eta, MC_PEERS, &tpeers, MC_UPEERS, &upeers,
                        MC_DPEERS, &dpeers, -1 );

    tpeers = MAX( tpeers, 0 );
    upeers = MAX( upeers, 0 );
    dpeers = MAX( dpeers, 0 );
    mb = readablesize(size);
    prog *= 100;

    if( TR_STATUS_CHECK & status )
    {
        top = g_strdup_printf( _("Checking existing files (%.1f%%)"), prog );
    }
    else if( TR_STATUS_DOWNLOAD & status )
    {
        if( 0 > eta )
        {
            top = g_strdup_printf( _("Stalled (%.1f%%)"), prog );
        }
        else
        {
            timestr = readabletime(eta);
            top = g_strdup_printf( _("Finishing in %s (%.1f%%)"),
                                   timestr, prog );
            g_free(timestr);
        }
    }
    else if(TR_STATUS_SEED & status)
    {
        top = g_strdup_printf(
            ngettext( "Seeding, uploading to %d of %d peer",
                      "Seeding, uploading to %d of %d peers", tpeers ),
            dpeers, tpeers );
    }
    else if( TR_STATUS_STOPPING & status )
    {
        top = g_strdup( _("Stopping...") );
    }
    else if( TR_STATUS_PAUSE & status )
    {
        top = g_strdup_printf( _("Stopped (%.1f%%)"), prog );
    }
    else
    {
        top = g_strdup( "" );
        g_assert_not_reached();
    }

    if( TR_OK != err )
    {
        gtk_tree_model_get( model, iter, MC_TERR, &terr, -1 );
        bottom = g_strconcat( _("Error: "), terr, NULL );
        g_free( terr );
    }
    else if( TR_STATUS_DOWNLOAD & status )
    {
        bottom = g_strdup_printf( ngettext( "Downloading from %i of %i peer",
                                            "Downloading from %i of %i peers",
                                            tpeers ), upeers, tpeers );
    }
    else
    {
        bottom = NULL;
    }

    str = g_markup_printf_escaped( "<big>%s (%s)</big>\n<small>%s\n%s</small>",
                                   name, mb, top,
                                   ( NULL == bottom ? "" : bottom ) );
    g_object_set( rend, "markup", str, NULL );
    g_free( name );
    g_free( mb );
    g_free( str );
    g_free( top );
    g_free( bottom );
}

static void
formatprog( GtkTreeViewColumn * col SHUTUP, GtkCellRenderer * rend,
            GtkTreeModel * model, GtkTreeIter * iter, gpointer data SHUTUP )
{
    char  * dlstr, * ulstr, * str, * marked;
    gfloat  prog, dl, ul;
    guint64 down, up;

    gtk_tree_model_get( model, iter, MC_PROG, &prog, MC_DRATE, &dl,
                        MC_URATE, &ul, MC_DOWN, &down, MC_UP, &up, -1 );
    prog = MAX( prog, 0.0 );
    prog = MIN( prog, 1.0 );

    ulstr = readablesize( ul * 1024.0 );
    if( 1.0 == prog )
    {
        dlstr = ratiostr( down, up );
        str = g_strdup_printf( _("Ratio: %s\nUL: %s/s"), dlstr, ulstr );
    }
    else
    {
        dlstr = readablesize( dl * 1024.0 );
        str = g_strdup_printf( _("DL: %s/s\nUL: %s/s"), dlstr, ulstr );
    }
    marked = g_markup_printf_escaped( "<small>%s</small>", str );
    g_object_set( rend, "markup", str, "progress", prog, NULL );
    g_free( dlstr );
    g_free( ulstr );
    g_free( str );
    g_free( marked );
}

/* show a popup menu for a right-click on the list */
static gboolean
listclick( GtkWidget * view, GdkEventButton * event, gpointer data )
{
    TrWindow         * self;
    GtkTreeSelection * sel;
    GtkTreePath      * path;
    GtkTreeModel     * model;
    GtkTreeIter        iter;
    int                status;
    TrTorrent        * tor, * issel;

    if( GDK_BUTTON_PRESS != event->type || 3 != event->button )
    {
        return FALSE;
    }

    TR_IS_WINDOW( data );
    self = TR_WINDOW( data );
    if( self->disposed )
    {
        return FALSE;
    }

    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    model = gtk_tree_view_get_model( GTK_TREE_VIEW( view ) );

    /* find what row, if any, the user clicked on */
    if( gtk_tree_view_get_path_at_pos( GTK_TREE_VIEW( view ),
                                        event->x, event->y, &path,
                                        NULL, NULL, NULL ) )
    {
        if( gtk_tree_model_get_iter( model, &iter, path ) )
        {
            /* get torrent and status for the right-clicked row */
            gtk_tree_model_get( model, &iter, MC_TORRENT, &tor,
                                MC_STAT, &status, -1 );
            issel = tor;
            gtk_tree_selection_selected_foreach( sel, istorsel, &issel );
            g_object_unref( tor );
            /* if the clicked row isn't selected, select only it */
            if( NULL != issel )
            {
                gtk_tree_selection_unselect_all( sel );
                gtk_tree_selection_select_iter( sel, &iter );
            }
        }
        gtk_tree_path_free( path );
    }
    else
    {
        gtk_tree_selection_unselect_all( sel );
    }

    popupmenu( self, event );

    return TRUE;
}

static gboolean
listpopup( GtkWidget * view SHUTUP, gpointer data )
{
    popupmenu( TR_WINDOW( data ), NULL );
    return TRUE;
}

static void
popupmenu( TrWindow * self, GdkEventButton * event )
{
    GtkTreeSelection * sel;
    int count, ii, status;
    GtkWidget * menu, * item;

    TR_IS_WINDOW( self );
    if( self->disposed )
    {
        return;
    }

    sel   = gtk_tree_view_get_selection( self->view );
    count = gtk_tree_selection_count_selected_rows( sel );
    menu  = gtk_menu_new();

    if( NULL != self->stupidpopuphack )
    {
        gtk_widget_destroy( self->stupidpopuphack );
    }
    self->stupidpopuphack = menu;

    status = 0;
    gtk_tree_selection_selected_foreach( sel, orstatus, &status );

    for( ii = 0; ii < self->count; ii++ )
    {
        if( !( ACTF_MENU & self->actions[ii].flags ) ||
            !ACT_ISAVAIL( self->actions[ii].flags, status ) )
        {
            continue;
        }
        item = gtk_menu_item_new_with_label( self->actions[ii].label );
        /* set the action for the menu item */
        g_object_set_data( G_OBJECT( item ), MENU_ITEM_ACTION,
                           GINT_TO_POINTER( self->actions[ii].id ) );
        g_signal_connect( item, "activate", G_CALLBACK( menuclick ), self );
        gtk_menu_shell_append( GTK_MENU_SHELL( menu ), item );
    }

    gtk_widget_show_all( menu );

    gtk_menu_popup( GTK_MENU( menu ), NULL, NULL, NULL, NULL,
                    ( NULL == event ? 0 : event->button ),
                    gdk_event_get_time( (GdkEvent*)event ) );
}

static void
menuclick( GtkWidget * item, gpointer data )
{
    gpointer action;

    action = g_object_get_data( G_OBJECT( item ), MENU_ITEM_ACTION );
    emitaction( data, GPOINTER_TO_INT( action ) );
}

static void
doubleclick( GtkWidget * view SHUTUP, GtkTreePath * path,
             GtkTreeViewColumn * col SHUTUP, gpointer data )
{
    TrWindow         * self;
    GtkTreeSelection * sel;

    TR_IS_WINDOW( data );
    self = TR_WINDOW( data );
    if( self->disposed || 0 > self->doubleclick )
    {
        return;
    }

    sel = gtk_tree_view_get_selection( self->view );
    gtk_tree_selection_select_path( sel, path );

    emitaction( self, self->doubleclick );
}

static void
emitaction( TrWindow * self, int id )
{
    TrWindowClass * class;

    TR_IS_WINDOW( self );
    if( self->disposed )
    {
        return;
    }

    class = g_type_class_peek( TR_WINDOW_TYPE );
    g_signal_emit( self, class->actionsig, 0, id );
}

/* use with gtk_tree_selection_selected_foreach to | status of selected rows */
static void
orstatus( GtkTreeModel * model, GtkTreePath * path SHUTUP, GtkTreeIter * iter,
          gpointer data )
{
    int * allstatus, thisstatus;

  allstatus = data;
  gtk_tree_model_get( model, iter, MC_STAT, &thisstatus, -1 );
  *allstatus |= thisstatus;
}

/* data should be a TrTorrent**, will set torrent to NULL if it's selected */
static void
istorsel( GtkTreeModel * model, GtkTreePath * path SHUTUP, GtkTreeIter * iter,
          gpointer data )
{
    TrTorrent ** torref, * tor;

    torref = data;
    if( NULL != *torref )
    {
        gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
        if( tor == *torref )
        {
            *torref = NULL;
        }
        g_object_unref( tor );
    }
}
