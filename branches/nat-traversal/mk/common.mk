# $Id$

TMPCFLAGS   = -g -Wall -W -O3 -funroll-loops -D_FILE_OFFSET_BITS=64 \
              -D_LARGEFILE_SOURCE -D_GNU_SOURCE \
              -DSYS_$(shell echo $(SYSTEM) | tr a-z A-Z)
TMPCXXFLAGS = $(TMPCFLAGS)
TMPLDFLAGS  =

ifeq ($(SYSTEM),BeOS)
TMPCXXFLAGS += -Wno-multichar
ifeq ($(BEOS_NETSERVER),yes)
TMPCFLAGS  += -DBEOS_NETSERVER
TMPLDFLAGS += -lnet
else
TMPLDFLAGS += -lbind -lsocket
endif
endif

ifeq ($(PTHREAD),yes)
ifneq ($(filter FreeBSD OpenBSD,$(SYSTEM)),)
TMPCFLAGS  += -pthread
TMPLDFLAGS += -pthread
else
TMPLDFLAGS += -lpthread
endif
endif

ifeq ($(OPENSSL),yes)
TMPCFLAGS  += -DHAVE_OPENSSL
TMPLDFLAGS += -lcrypto
endif

CFLAGS   := $(TMPCFLAGS) $(CFLAGS)
CXXFLAGS := $(TMPCXXFLAGS) $(CXXFLAGS)
LDFLAGS  := $(TMPLDFLAGS) $(LDFLAGS)

#
# Utils
#

define DEP_RULE
	@echo "Checking dependencies..."
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) -MM $(SRC) $(CFLAGS) >> .depend;)
endef

define CC_RULE
	@echo "Cc $@"
	@CMD="$(CC) $(CFLAGS) -o $@ -c $<"; $$CMD || \
	  ( echo "Compile line for $@ was:"; echo $$CMD; false )
endef

define LINK_RULE
	@echo "Link $@"
	@CMD="$(CC) -o $@ $(OBJS) $(LDFLAGS)"; $$CMD || \
	  ( echo "Compile line for $@ was:"; echo $$CMD; false )
endef

define MSGFMT_RULE
       @echo "Msgfmt $<"
       @msgfmt -f $< -o $@
endef

define INSTALL_BIN_RULE
       @echo "Install $<"
       @$(MKDIR) $(PREFIX)/bin
       @$(CP) $< $(PREFIX)/bin/
endef

define INSTALL_LOCALE_RULE
       @echo "Install $<"
       @$(MKDIR) $(LOCALEDIR)/$*/LC_MESSAGES
       @$(CP) $< $(LOCALEDIR)/$*/LC_MESSAGES/transmission-gtk.mo
endef

define INSTALL_MAN_RULE
	@echo "Install $<"
	@$(MKDIR) $(PREFIX)/man/man1
	@$(CP) $< $(PREFIX)/man/man1/
endef

RM    = rm -Rf
CP    = cp -f
MKDIR = mkdir -p
MAKE += --no-print-directory
