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

#ifndef TG_PREFS_H
#define TG_PREFS_H

#include "tr_backend.h"
#include "tr_torrent.h"
#include "util.h"

GtkWidget *
makeprefwindow(GtkWindow *parent, TrBackend *back);

/* set various things based on saved prefs */
void
applyprefs(TrBackend *back);

/* show the "add a torrent" dialog */
void
makeaddwind(GtkWindow *parent, add_torrents_func_t addfunc, void *cbdata);

/* show the info window for a torrent */
void
makeinfowind( GtkWindow * parent, GtkTreeModel * model, GtkTreePath * path,
              TrTorrent * tor );

/* prompt for a download directory for torrents, then add them */
void
promptfordir( GtkWindow * parent, add_torrents_func_t addfunc, void *cbdata,
              GList * files, guint flags );

/* prompt if the user wants to quit, calls func with cbdata if they do */
void
askquit( GtkWindow * parent, callbackfunc_t func, void * cbdata );

#endif /* TG_PREFS_H */
