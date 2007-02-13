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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "conf.h"
#include "dialogs.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "util.h"

#include "transmission.h"

#define PREFNAME                "transmission-dialog-pref-name"

struct addcb {
  add_torrents_func_t addfunc;
  void *data;
  gboolean autostart;
  gboolean usingaltdir;
  GtkFileChooser *altdir;
  GtkButtonBox *altbox;
};

struct dirdata
{
    add_torrents_func_t addfunc;
    void              * cbdata;
    GList             * files;
    guint               flags;
};

struct quitdata
{
    callbackfunc_t func;
    void         * cbdata;
};

static void
autoclick(GtkWidget *widget, gpointer gdata);
static void
dirclick(GtkWidget *widget, gpointer gdata);
static void
addresp(GtkWidget *widget, gint resp, gpointer gdata);
static void
promptresp( GtkWidget * widget, gint resp, gpointer data );
static void
quitresp( GtkWidget * widget, gint resp, gpointer data );

void
makeaddwind(GtkWindow *parent, add_torrents_func_t addfunc, void *cbdata) {
  GtkWidget *wind = gtk_file_chooser_dialog_new(_("Add a Torrent"), parent,
    GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
  struct addcb *data = g_new(struct addcb, 1);
  GtkWidget *vbox = gtk_vbox_new(FALSE, 3);
  GtkWidget *bbox = gtk_hbutton_box_new();
  GtkWidget *autocheck = gtk_check_button_new_with_mnemonic(
    _("Automatically _start torrent"));
  GtkWidget *dircheck = gtk_check_button_new_with_mnemonic(
    _("Use alternate _download directory"));
  GtkFileFilter *filter = gtk_file_filter_new();
  GtkFileFilter *unfilter = gtk_file_filter_new();
  GtkWidget *getdir = gtk_file_chooser_button_new(
    _("Choose a download directory"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

  data->addfunc = addfunc;
  data->data = cbdata;
  data->autostart = TRUE;
  data->usingaltdir = FALSE;
  data->altdir = GTK_FILE_CHOOSER(getdir);
  data->altbox = GTK_BUTTON_BOX(bbox);

  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_EDGE);
  gtk_box_pack_start_defaults(GTK_BOX(bbox), dircheck);
  gtk_box_pack_start_defaults(GTK_BOX(bbox), getdir);

  gtk_box_pack_start_defaults(GTK_BOX(vbox), autocheck);
  gtk_box_pack_start_defaults(GTK_BOX(vbox), bbox);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autocheck), TRUE);
  gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( getdir ),
                                       getdownloaddir() );

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dircheck), FALSE);
  gtk_widget_set_sensitive(getdir, FALSE);

  gtk_file_filter_set_name(filter, _("Torrent files"));
  gtk_file_filter_add_pattern(filter, "*.torrent");
  gtk_file_filter_set_name(unfilter, _("All files"));
  gtk_file_filter_add_pattern(unfilter, "*");

  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(wind), filter);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(wind), unfilter);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(wind), TRUE);
  gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(wind), vbox);

  g_signal_connect(G_OBJECT(autocheck), "clicked", G_CALLBACK(autoclick),data);
  g_signal_connect(G_OBJECT(dircheck), "clicked", G_CALLBACK(dirclick), data);
  g_signal_connect(G_OBJECT(wind), "response", G_CALLBACK(addresp), data);

  gtk_widget_show_all(wind);
}

static void
autoclick(GtkWidget *widget, gpointer gdata) {
  struct addcb *data = gdata;

  data->autostart = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void
dirclick(GtkWidget *widget, gpointer gdata) {
  struct addcb *data = gdata;

  data->usingaltdir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  gtk_widget_set_sensitive(GTK_WIDGET(data->altdir), data->usingaltdir);
}

static void
addresp(GtkWidget *widget, gint resp, gpointer gdata) {
  struct addcb *data = gdata;
  GSList *files, *ii;
  GList *stupidgtk;
  int flags;
  char *dir;

  if(GTK_RESPONSE_ACCEPT == resp) {
    dir = NULL;
    if(data->usingaltdir)
      dir = gtk_file_chooser_get_filename(data->altdir);
    files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(widget));
    stupidgtk = NULL;
    for(ii = files; NULL != ii; ii = ii->next)
      stupidgtk = g_list_append(stupidgtk, ii->data);
    flags = ( data->autostart ? TR_TORNEW_RUNNING : TR_TORNEW_PAUSED );
    flags |= addactionflag( tr_prefs_get( PREF_ID_ADDSTD ) );
    data->addfunc( data->data, NULL, stupidgtk, dir, flags );
    if(NULL != dir)
      g_free(dir);
    g_slist_free(files);
    freestrlist(stupidgtk);
  }

  g_free( data );
  gtk_widget_destroy(widget);
}

#define INFOLINE(tab, ii, nam, val) \
  do { \
    char *txt = g_markup_printf_escaped("<b>%s</b>", nam); \
    GtkWidget *wid = gtk_label_new(NULL); \
    gtk_misc_set_alignment(GTK_MISC(wid), 0, .5); \
    gtk_label_set_markup(GTK_LABEL(wid), txt); \
    gtk_table_attach_defaults(GTK_TABLE(tab), wid, 0, 1, ii, ii + 1); \
    wid = gtk_label_new(val); \
    gtk_label_set_selectable(GTK_LABEL(wid), TRUE); \
    gtk_misc_set_alignment(GTK_MISC(wid), 0, .5); \
    gtk_table_attach_defaults(GTK_TABLE(tab), wid, 1, 2, ii, ii + 1); \
    ii++; \
    g_free(txt); \
  } while(0)

#define INFOLINEF(tab, ii, fmt, nam, val) \
  do { \
    char *buf = g_strdup_printf(fmt, val); \
    INFOLINE(tab, ii, nam, buf); \
    g_free(buf); \
  } while(0)

#define INFOLINEA(tab, ii, nam, val) \
  do { \
    char *buf = val; \
    INFOLINE(tab, ii, nam, buf); \
    g_free(buf); \
  } while(0)

#define INFOSEP(tab, ii) \
  do { \
    GtkWidget *wid = gtk_hseparator_new(); \
    gtk_table_attach_defaults(GTK_TABLE(tab), wid, 0, 2, ii, ii + 1); \
    ii++; \
  } while(0)

void
makeinfowind(GtkWindow *parent, TrTorrent *tor) {
  tr_stat_t *sb;
  tr_info_t *in;
  GtkWidget *wind, *label;
  int ii;
  char *str;
  const int rowcount = 15;
  GtkWidget *table = gtk_table_new(rowcount, 2, FALSE);

  /* XXX should use model and update this window regularly */

  sb = tr_torrent_stat(tor);
  in = tr_torrent_info(tor);
  str = g_strdup_printf(_("%s Properties"), in->name);
  wind = gtk_dialog_new_with_buttons(str, parent,
    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
    GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
  g_free(str);

  gtk_widget_set_name(wind, "TransmissionDialog");
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_table_set_row_spacings(GTK_TABLE(table), 12);
  gtk_dialog_set_default_response(GTK_DIALOG(wind), GTK_RESPONSE_ACCEPT);
  gtk_container_set_border_width(GTK_CONTAINER(table), 6);
  gtk_window_set_resizable(GTK_WINDOW(wind), FALSE);

  label = gtk_label_new(NULL);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  str = g_markup_printf_escaped("<big>%s</big>", in->name);
  gtk_label_set_markup(GTK_LABEL(label), str);
  g_free(str);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

  ii = 1;

  INFOSEP(table, ii);

  if(80 == sb->trackerPort)
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s",
              sb->trackerAddress));
  else
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s:%i",
              sb->trackerAddress, sb->trackerPort));
  INFOLINE(table, ii, _("Announce:"), sb->trackerAnnounce);
  INFOLINEA(table, ii, _("Piece Size:"), readablesize(in->pieceSize));
  INFOLINEF(table, ii, "%i", _("Pieces:"), in->pieceCount);
  INFOLINEA(table, ii, _("Total Size:"), readablesize(in->totalSize));
  if(0 > sb->seeders)
    INFOLINE(table, ii, _("Seeders:"), _("?"));
  else
    INFOLINEF(table, ii, "%i", _("Seeders:"), sb->seeders);
  if(0 > sb->leechers)
    INFOLINE(table, ii, _("Leechers:"), _("?"));
  else
    INFOLINEF(table, ii, "%i", _("Leechers:"), sb->leechers);
  if(0 > sb->completedFromTracker)
    INFOLINE(table, ii, _("Completed:"), _("?"));
  else
    INFOLINEF(table, ii, "%i", _("Completed:"), sb->completedFromTracker);

  INFOSEP(table, ii);

  INFOLINE(table, ii, _("Directory:"), tr_torrentGetFolder(tr_torrent_handle(tor)));
  INFOLINEA(table, ii, _("Downloaded:"), readablesize(sb->downloaded));
  INFOLINEA(table, ii, _("Uploaded:"), readablesize(sb->uploaded));

  INFOSEP(table, ii);

  g_assert(rowcount == ii);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect(G_OBJECT(wind), "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_show_all(wind);
}

void
promptfordir( GtkWindow * parent, add_torrents_func_t addfunc, void *cbdata,
              GList * files, guint flags )
{
    struct dirdata * stuff;
    GtkWidget      * wind;

    stuff = g_new( struct dirdata, 1 );
    stuff->addfunc = addfunc;
    stuff->cbdata  = cbdata;
    stuff->files   = dupstrlist( files );
    stuff->flags   = flags;

    wind =  gtk_file_chooser_dialog_new( _("Choose a directory"), parent,
                                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                         NULL );
    gtk_file_chooser_set_local_only( GTK_FILE_CHOOSER( wind ), TRUE );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER( wind ), FALSE );
    gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( wind ),
                                   getdownloaddir() );

    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( promptresp ), stuff );

    gtk_widget_show_all(wind);
}

static void
promptresp( GtkWidget * widget, gint resp, gpointer data )
{
    struct dirdata * stuff;
    char           * dir;

    stuff = data;

    if( GTK_RESPONSE_ACCEPT == resp )
    {
        dir = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( widget ) );
        /* it seems that we will always get a directory */
        g_assert( NULL != dir );
        stuff->addfunc( stuff->cbdata, NULL, stuff->files, dir, stuff->flags );
        g_free( dir );
    }

    freestrlist( stuff->files );
    g_free( stuff );
    gtk_widget_destroy( widget );
}

void
askquit( GtkWindow * parent, callbackfunc_t func, void * cbdata )
{
    struct quitdata * stuff;
    GtkWidget * wind;

    stuff          = g_new( struct quitdata, 1 );
    stuff->func    = func;
    stuff->cbdata  = cbdata;

    wind = gtk_message_dialog_new( parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   _("Are you sure you want to quit %s?"),
                                   g_get_application_name() );
    gtk_dialog_set_default_response( GTK_DIALOG( wind ), GTK_RESPONSE_YES );
    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( quitresp ), stuff );

    gtk_widget_show_all( wind );
}

static void
quitresp( GtkWidget * widget, gint resp, gpointer data )
{
    struct quitdata * stuff;

    stuff = data;

    if( GTK_RESPONSE_YES == resp )
    {
        stuff->func( stuff->cbdata );
    }

    g_free( stuff );
    gtk_widget_destroy( widget );
}
