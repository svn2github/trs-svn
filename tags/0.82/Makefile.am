if WITH_GTK
  GTK_DIR = gtk
endif
if WITH_LIBEVENT
  DAEMON_DIR = daemon
endif
if HAVE_BEOS
  BEOS_DIR = beos
endif
if HAVE_DARWIN
  MAC_DIR = macosx
endif
if HAVE_WX
  WX_DIR = wx
endif

SUBDIRS = libtransmission $(DAEMON_DIR) cli $(GTK_DIR) $(BEOS_DIR) $(MAC_DIR) $(WX_DIR)

DISTCLEANFILES = \
 Transmission.desktop

EXTRA_DIST = \
 NEWS \
 AUTHORS \
 LICENSE \
 README
