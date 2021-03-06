#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "blocklist.h"
#include "net.h"
#include "utils.h"

#define VERBOSE 0

#define check(A) { \
    ++test; \
    if (A) { \
        if( VERBOSE ) \
            fprintf( stderr, "PASS test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
    } else { \
        if( VERBOSE ) \
            fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
        return test; \
    } \
}

static void
createTestBlocklist( const char * tmpfile )
{
    const char * lines[] = { "Austin Law Firm:216.16.1.144-216.16.1.151",
                             "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255",
                             "Corel Corporation:216.21.157.192-216.21.157.223",
                             "Fox Speed Channel:216.79.131.192-216.79.131.223" };
    FILE * out;
    int i;
    const int lineCount = sizeof(lines) / sizeof(lines[0]);

    /* create the ascii file to feed to libtransmission */
    out = fopen( tmpfile, "w+" );
    for( i=0; i<lineCount; ++i )
        fprintf( out, "%s\n", lines[i] );
    fclose( out );
}

int
main( void )
{
    char * tmpfile_txt = "/tmp/transmission-blocklist-test.txt";
    char * tmpfile_bin = "/tmp/transmission-blocklist-test.bin";
    struct in_addr addr;
    int test = 0;
    tr_blocklist * b;

    remove( tmpfile_txt );
    remove( tmpfile_bin );

    b = _tr_blocklistNew( tmpfile_bin, TRUE );
    createTestBlocklist( tmpfile_txt );
    _tr_blocklistSetContent( b, tmpfile_txt );

    /* now run some tests */
    check( !tr_netResolve( "216.16.1.143", &addr ) );
    check( !_tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.144", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.145", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.146", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.147", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.148", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.149", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.150", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.151", &addr ) );
    check(  _tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.152", &addr ) );
    check( !_tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "216.16.1.153", &addr ) );
    check( !_tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "217.0.0.1", &addr ) );
    check( !_tr_blocklistHasAddress( b, &addr ) );
    check( !tr_netResolve( "255.0.0.1", &addr ) );

    /* cleanup */
    _tr_blocklistFree( b );
    remove( tmpfile_txt );
    remove( tmpfile_bin );
    return 0;
}
