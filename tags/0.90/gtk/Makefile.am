AM_CPPFLAGS = \
    -I$(top_srcdir) \
    $(LIBEVENT_CPPFLAGS) \
    $(GTK_CFLAGS) \
    -DTRANSMISSIONLOCALEDIR=\""$(transmissionlocaledir)"\"

AM_CFLAGS = $(PTHREAD_CFLAGS)

noinst_HEADERS = \
    actions.h \
    conf.h \
    dialogs.h \
    hig.h \
    io.h \
    ipc.h \
    lock.h \
    logo.h \
    makemeta-ui.h \
    msgwin.h \
    torrent-inspector.h \
    tr_cell_renderer_progress.h \
    tr_core.h \
    tr_icon.h \
    tr_prefs.h \
    tr_torrent.h \
    tr_window.h \
    ui.h \
    util.h

bin_PROGRAMS = transmission

transmission_SOURCES = \
    actions.c \
    conf.c \
    dialogs.c \
    hig.c \
    io.c \
    ipc.c \
    main.c \
    makemeta-ui.c \
    msgwin.c \
    torrent-inspector.c \
    tr_cell_renderer_progress.c \
    tr_core.c \
    tr_icon.c \
    tr_prefs.c \
    tr_torrent.c \
    tr_window.c \
    util.c

transmission_LDADD = \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(GTK_LIBS) \
    $(PTHREAD_LIBS) -lm

DESKTOP_IN_FILES=transmission.desktop.in
DESKTOP_FILES=$(DESKTOP_IN_FILES:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

Productivitydir = $(datadir)/applications
Productivity_DATA = $(DESKTOP_FILES)

icon_DATA = transmission.png
icondir = $(datadir)/pixmaps

EXTRA_DIST = \
    $(DESKTOP_IN_FILES) \
    transmission.png

DISTCLEANFILES = \
    transmission.desktop

