/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#ifndef TR_PREFS_H
#define TR_PREFS_H

#include <gtk/gtk.h>

GtkWidget * tr_prefs_dialog_new( GObject * core, GtkWindow * parent );

/* if you add a key here,  you /must/ add its
 * default in tr_prefs_init_global( void ) */

#define PREF_KEY_DL_LIMIT_ENABLED       "download-limit-enabled"
#define PREF_KEY_DL_LIMIT               "download-limit"
#define PREF_KEY_UL_LIMIT_ENABLED       "upload-limit-enabled"
#define PREF_KEY_UL_LIMIT               "upload-limit"
#define PREF_KEY_OPTIONS_PROMPT         "show-options-window"
#define PREF_KEY_DIR_DEFAULT            "default-download-directory"
#define PREF_KEY_OPEN_DIALOG_FOLDER     "open-dialog-folder"
#define PREF_KEY_INHIBIT_HIBERNATION    "inhibit-hibernation"
#define PREF_KEY_DIR_WATCH              "watch-folder"
#define PREF_KEY_DIR_WATCH_ENABLED      "watch-folder-enabled"
#define PREF_KEY_START                  "start-added-torrents"
#define PREF_KEY_TRASH_ORIGINAL         "trash-original-torrent-files" 
#define PREF_KEY_PEER_SOCKET_TOS        "peer-socket-tos"
#define PREF_KEY_PORT                   "listening-port"
#define PREF_KEY_NAT                    "nat-traversal-enabled"
#define PREF_KEY_PEX                    "pex-enabled"
#define PREF_KEY_ASKQUIT                "prompt-before-exit"
#define PREF_KEY_ENCRYPTED_ONLY         "encrypted-connections-only"
#define PREF_KEY_MSGLEVEL               "debug-message-level"
#define PREF_KEY_SORT_MODE              "sort-mode"
#define PREF_KEY_SORT_REVERSED          "sort-reversed"
#define PREF_KEY_MINIMAL_VIEW           "minimal-view"
#define PREF_KEY_FILTERBAR              "show-filterbar"
#define PREF_KEY_STATUSBAR              "show-statusbar"
#define PREF_KEY_STATUSBAR_STATS        "statusbar-stats"
#define PREF_KEY_TOOLBAR                "show-toolbar"
#define PREF_KEY_MAX_PEERS_GLOBAL       "max-peers-global"
#define PREF_KEY_MAX_PEERS_PER_TORRENT  "max-peers-per-torrent"
#define PREF_KEY_BLOCKLIST_ENABLED      "blocklist-enabled"
#define PREF_KEY_MAIN_WINDOW_HEIGHT     "main-window-height"
#define PREF_KEY_MAIN_WINDOW_WIDTH      "main-window-width"
#define PREF_KEY_MAIN_WINDOW_X          "main-window-x"
#define PREF_KEY_MAIN_WINDOW_Y          "main-window-y"


void tr_prefs_init_global( void );

#endif
