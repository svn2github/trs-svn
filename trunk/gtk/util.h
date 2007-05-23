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

#ifndef TG_UTIL_H
#define TG_UTIL_H

#include <sys/types.h>
#include <stdarg.h>

/* macro to shut up "unused parameter" warnings */
#ifdef __GNUC__
#define SHUTUP                  __attribute__((unused))
#else
#define SHUTUP
#endif

/* XXX this shouldn't be here */
enum tr_torrent_action { TR_TOR_LEAVE, TR_TOR_COPY, TR_TOR_MOVE };

typedef void (*add_torrents_func_t)(void*,void*,GList*,const char*,
                                    enum tr_torrent_action,gboolean);

/* return number of items in array */
#define ALEN( a )               ( ( signed )( sizeof(a) / sizeof( (a)[0] ) ) )

/* used for a callback function with a data parameter */
typedef void (*callbackfunc_t)(void*);

/* flags indicating where and when an action is valid */
#define ACTF_TOOL       ( 1 << 0 ) /* appear in the toolbar */
#define ACTF_MENU       ( 1 << 1 ) /* appear in the popup menu */
#define ACTF_ALWAYS     ( 1 << 2 ) /* available regardless of selection */
#define ACTF_ACTIVE     ( 1 << 3 ) /* available for active torrent */
#define ACTF_INACTIVE   ( 1 << 4 ) /* available for inactive torrent */
#define ACTF_SEPARATOR  ( 1 << 5 ) /* dummy action to create menu separator */
/* appear in the toolbar and the popup menu */
#define ACTF_WHEREVER   ( ACTF_TOOL | ACTF_MENU )
/* available if there is something selected */
#define ACTF_WHATEVER   ( ACTF_ACTIVE | ACTF_INACTIVE )

/* checking action flags against torrent status */
#define ACT_ISAVAIL( flags, status ) \
    ( ( ACTF_ACTIVE   & (flags) && TR_STATUS_ACTIVE   & (status) ) || \
      ( ACTF_INACTIVE & (flags) && TR_STATUS_INACTIVE & (status) ) || \
        ACTF_ALWAYS   & (flags) )

/* try to interpret a string as a textual representation of a boolean */
/* note that this isn't localized */
gboolean
strbool(const char *str);

/* return a human-readable string for the size given in bytes.
   the string must be g_free()d */
char *
readablesize(guint64 size);

/* return a human-readable string for the time given in seconds.
   the string must be g_free()d */
char *
readabletime(int secs);

/* returns a string representing the download ratio.
   the string must be g_free()d */
char *
ratiostr(guint64 down, guint64 up);

/* create a directory and any missing parent directories */
gboolean
mkdir_p(const char *name, mode_t mode);

/* create a copy of a GList of strings, this dups the actual strings too */
GList *
dupstrlist( GList * list );

/* joins a GList of strings into one string using an optional separator */
char *
joinstrlist(GList *list, char *sep);

/* free a GList of strings */
void
freestrlist(GList *list);

/* decodes a string that has been urlencoded */
char *
urldecode(const char *str, int len);

/* return a list of cleaned-up paths, with invalid directories removed */
GList *
checkfilenames(int argc, char **argv);

/* returns the flag for an action string */
enum tr_torrent_action
toraddaction( const char * action );

/* returns the action string for a flag */
const char *
toractionname( enum tr_torrent_action action );

/* retrieve the global download directory */
const char *
getdownloaddir( void );

#ifdef GTK_MAJOR_VERSION

/* action handling */
struct action
{
    int            id;
    int            flags;
    char         * label;
    char         * stock;
    GtkWidget    * tool;
    GtkWidget    * menu;
    callbackfunc_t func;
    gpointer       data;
};
struct action *
action_new( int id, int flags, const char * label, const char * stock );
void
action_free( struct action * act );
GtkWidget *
action_maketool( struct action * act, const char * key,
                 GCallback func, gpointer data );
GtkWidget *
action_makemenu( struct action * act, const char * actkey,
                 GtkAccelGroup * accel, const char * path, guint keyval,
                 GCallback func, gpointer data );

/* here there be dragons */
void
sizingmagic( GtkWindow * wind, GtkScrolledWindow * scroll,
             GtkPolicyType hscroll, GtkPolicyType vscroll );

/* create an error dialog, if wind is NULL or mapped then show dialog now,
   otherwise show it when wind becomes mapped */
void
errmsg( GtkWindow * wind, const char * format, ... )
#ifdef __GNUC__
    __attribute__ (( format ( printf, 2, 3 ) ))
#endif
    ;

/* create an error dialog but do not gtk_widget_show() it,
   calls func( data ) when the dialog is closed */
GtkWidget *
errmsg_full( GtkWindow * wind, callbackfunc_t func, void * data,
             const char * format, ... )
#ifdef __GNUC__
    __attribute__ (( format ( printf, 4, 5 ) ))
#endif
    ;

/* varargs version of errmsg_full() */
GtkWidget *
verrmsg_full( GtkWindow * wind, callbackfunc_t func, void * data,
              const char * format, va_list ap );

#endif /* GTK_MAJOR_VERSION */

#endif /* TG_UTIL_H */
