#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "bencode.h"
#include "rpcimpl.h"
#include "utils.h"

#undef VERBOSE
#include "libtransmission-test.h"

static int
test_list( void )
{
    int64_t      i;
    const char * str;
    tr_benc      top;

    tr_rpc_parse_list_str( &top, "12", -1 );
    check( tr_bencIsInt( &top ) );
    check( tr_bencGetInt( &top, &i ) );
    check( i == 12 );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "12", 1 );
    check( tr_bencIsInt( &top ) );
    check( tr_bencGetInt( &top, &i ) );
    check( i == 1 );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "6,7", -1 );
    check( tr_bencIsList( &top ) );
    check( tr_bencListSize( &top ) == 2 );
    check( tr_bencGetInt( tr_bencListChild( &top, 0 ), &i ) );
    check( i == 6 );
    check( tr_bencGetInt( tr_bencListChild( &top, 1 ), &i ) );
    check( i == 7 );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "asdf", -1 );
    check( tr_bencIsString( &top ) );
    check( tr_bencGetStr( &top, &str ) );
    check( !strcmp( str, "asdf" ) );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "1,3-5", -1 );
    check( tr_bencIsList( &top ) );
    check( tr_bencListSize( &top ) == 4 );
    check( tr_bencGetInt( tr_bencListChild( &top, 0 ), &i ) );
    check( i == 1 );
    check( tr_bencGetInt( tr_bencListChild( &top, 1 ), &i ) );
    check( i == 3 );
    check( tr_bencGetInt( tr_bencListChild( &top, 2 ), &i ) );
    check( i == 4 );
    check( tr_bencGetInt( tr_bencListChild( &top, 3 ), &i ) );
    check( i == 5 );
    tr_bencFree( &top );

    return 0;
}

MAIN_SINGLE_TEST(test_list)
