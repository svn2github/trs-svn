AM_CPPFLAGS = -I@top_srcdir@ $(LIBEVENT_CPPFLAGS)
AM_CFLAGS = $(PTHREAD_CFLAGS)

noinst_LIBRARIES = libdaemon.a

libdaemon_a_SOURCES = \
    errors.c \
    misc.c

noinst_HEADERS = \
    client.h \
    errors.h \
    misc.h \
    server.h \
    torrents.h

bin_PROGRAMS = \
    transmission-daemon \
    transmission-remote \
    transmission-proxy

COMMON_LDADD = \
    ./libdaemon.a \
    $(top_builddir)/libtransmission/libtransmission.a \
    $(top_builddir)/third-party/libevent/libevent.la \
    $(PTHREAD_LIBS) -lm

transmission_daemon_SOURCES = daemon.c server.c torrents.c
transmission_daemon_LDADD = $(COMMON_LDADD)
transmission_remote_SOURCES = client.c remote.c
transmission_remote_LDADD = $(COMMON_LDADD)
transmission_proxy_SOURCES = proxy.c
transmission_proxy_LDADD = $(COMMON_LDADD)
