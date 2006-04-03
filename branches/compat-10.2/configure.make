#! /bin/sh

#
# Default settings
#
SYSTEM=
BEOS_NETSERVER=no
MATH=no
PTHREAD=no
OPENSSL=
GTK=
PREFIX=/usr/local
CC="${CC-cc}"
CFLAGS="${CFLAGS}"
LDFLAGS="${LDFLAGS}"


#
# Functions
#
usage()
{
  cat << EOF

Options:
  --disable-openssl      Disable OpenSSL, use built-in SHA1 implementation
  --disable-gtk          Don't build the GTK+ GUI
  --prefix=PATH          Installation path

Some influential environment variables:
  CC          C compiler command
  CFLAGS      C compiler flags
  LDFLAGS     linker flags

EOF
}

openssl_test()
{
  cat > testconf.c << EOF
  #include <stdio.h>
  #include <openssl/sha.h>
  int main()
  {
      SHA1( 0, 0, 0 );
  }
EOF
  if $CC $CFLAGS $LDFLAGS -o testconf testconf.c -lcrypto > /dev/null 2>&1
  then
    echo "yes"
    OPENSSL=yes
  else
    echo "missing, using built-in SHA1 implementation"
    OPENSSL=no
  fi
  rm -f testconf.c testconf
}

lm_test()
{
  cat > testconf.c << EOF
  int main()
  {
    return cos( 42 );
  }
EOF
  if ! $CC -o testconf testconf.c > /dev/null 2>&1
  then
    if $CC -o testconf testconf.c -lm > /dev/null 2>&1
    then
      LDFLAGS="$LDFLAGS -lm"
    fi
  fi
  rm -f testconf.c testconf
}

lrintf_test()
{
  cat > testconf.c << EOF
  int main()
  {
    return ( lrintf( 3.14 ) != 3 );
  }
EOF
  if ( $CC -o testconf testconf.c $LINKLIBS && ./testconf ) > /dev/null 2>&1
  then
    CFLAGS="$CFLAGS -DHAVE_LRINTF"
  fi
  rm -f testconf.c testconf
}

gettext_test()
{
  cat > testconf.c <<EOF
  #include <libintl.h>
  int main()
  {
    gettext("");
  }
EOF

  if $CC $CFLAGS_GTK $LDFLAGS_GTK -o testconf testconf.c > /dev/null 2>&1
  then
    rm -f testconf.c testconf
    return 0
  fi

  for intl_testdir in $PREFIX/include \
      /usr/local/include /usr/X11R6/include /usr/pkg/include
  do
    if $CC $CFLAGS_GTK -I$intl_testdir $LDFLAGS_GTK -o testconf testconf.c > /dev/null 2>&1
    then
      CFLAGS_GTK="CFLAGS_GTK -I$intl_testdir"
      rm -f testconf.c testconf
      return 0
    fi
  done
  rm -f testconf.c testconf
  return 1
}

gtk_test()
{
  if pkg-config gtk+-2.0 > /dev/null 2>&1
  then
    if expr `pkg-config --modversion gtk+-2.0` '>=' 2.6.0 > /dev/null 2>&1
    then
      cat > testconf.c << EOF
      #include <gtk/gtk.h>
      int main()
      {
        gtk_main();
      }
EOF
      if $CC `pkg-config gtk+-2.0 --cflags --libs` -o testconf testconf.c > /dev/null 2>&1
      then
        CFLAGS_GTK=`pkg-config gtk+-2.0 --cflags`
        LDFLAGS_GTK=`pkg-config gtk+-2.0 --libs`
        if gettext_test
        then
          echo "yes"
          GTK=yes
          GTKLOCALEDIR="$PREFIX/share/locale"
        else
          echo "no (could not find gettext libintl.h)"
          GTK=no
        fi
      else
        echo "no"
        GTK=no
      fi
      rm -f testconf.c testconf
    else
      echo "no (2.6.0 or later is required)"
      GTK=no
    fi
  else
    echo "no"
    GTK=no
  fi
}

#
# Parse options
#
while [ $# -ne 0 ]; do
  param=`expr "opt$1" : 'opt[^=]*=\(.*\)'`

  case "x$1" in
    x--disable-openssl)
      OPENSSL=no
      ;;
    x--disable-gtk)
      GTK=no
      ;;
    x--help)
      usage
      exit 0
      ;;
  esac
  shift
done

#
# System-specific flags
#
SYSTEM=`uname -s`
case $SYSTEM in
  BeOS)
    RELEASE=`uname -r`
    case $RELEASE in
      6.0*|5.0.4) # Zeta or R5 / BONE beta 7
        ;;
      5.0*)       # R5 / net_server
        BEOS_NETSERVER=yes
        ;;
      *)
        echo "Unsupported BeOS version"
        exit 1 
        ;;
    esac
    ;;

  Darwin)
    # Make sure the Universal SDK is installed
    if [ ! -d /Developer/SDKs/MacOSX10.4u.sdk ]; then
      cat << EOF
You need to install the Universal SDK in order to build Transmission:
  Get your Xcode CD or package
  Restart the install
  When it gets to "Installation Type", select "Customize"
  Select "Mac OS X 10.4 (Universal) SDL" under "Cross Development"
  Finish the install.
EOF
      exit 1
    fi
    PTHREAD=yes
    ;;

  FreeBSD|NetBSD|OpenBSD|Linux)
    PTHREAD=yes
    ;;

  *)
    echo "Unsupported operating system"
    exit 1 ;;
esac
echo "System:  $SYSTEM"

#
# OpenSSL settings
#
echo -n "OpenSSL: "
if [ "$OPENSSL" = no ]; then
  echo "disabled, using built-in SHA1 implementation"
else
  openssl_test
fi 

#
# GTK+ settings
#
echo -n "GTK+:    "
if [ "$GTK" = no ]; then
  echo "disabled"
else
  gtk_test
fi
if [ "$GTK" = yes ]; then
  rm -f gtk/defines.h
  cat > gtk/defines.h << EOF
#ifndef TG_DEFINES_H
#define TG_DEFINES_H
#define LOCALEDIR               "$GTKLOCALEDIR"
#endif
EOF
fi

#
# Math functions
#
lm_test
lrintf_test

#
# Generate Makefile.config
#
rm -f Makefile.config
cat > Makefile.config << EOF
SYSTEM         = $SYSTEM
BEOS_NETSERVER = $BEOS_NETSERVER
PTHREAD        = $PTHREAD
OPENSSL        = $OPENSSL
GTK            = $GTK
CC             = $CC
CFLAGS         = $CFLAGS
LDFLAGS        = $LDFLAGS
CFLAGS_GTK     = $CFLAGS_GTK
LDFLAGS_GTK    = $LDFLAGS_GTK
EOF

echo
echo "To build Transmission, run 'make'."
