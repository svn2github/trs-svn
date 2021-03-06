/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* bsearch(), qsort() */
#include <string.h>

#include <unistd.h> /* unlink() */

#ifdef WIN32
 #include <w32api.h>
 #define WINVER  WindowsXP
 #include <windows.h>
 #define PROT_READ      PAGE_READONLY
 #define MAP_PRIVATE    FILE_MAP_COPY
#endif

#ifndef WIN32
 #include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "transmission.h"
#include "blocklist.h"
#include "net.h"
#include "utils.h"

#ifndef O_BINARY
 #define O_BINARY 0
#endif


/***
****  PRIVATE
***/

struct tr_ipv4_range
{
    uint32_t    begin;
    uint32_t    end;
};

struct tr_blocklist
{
    bool                   isEnabled;
    int                    fd;
    size_t                 ruleCount;
    size_t                 byteCount;
    char *                 filename;
    struct tr_ipv4_range * rules;
};

static void
blocklistClose( tr_blocklist * b )
{
    if( b->rules )
    {
        munmap( b->rules, b->byteCount );
        close( b->fd );
        b->rules = NULL;
        b->ruleCount = 0;
        b->byteCount = 0;
        b->fd = -1;
    }
}

static void
blocklistLoad( tr_blocklist * b )
{
    int fd;
    size_t byteCount;
    struct stat st;
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    blocklistClose( b );

    if( stat( b->filename, &st ) == -1 )
        return;

    fd = open( b->filename, O_RDONLY | O_BINARY );
    if( fd == -1 )
    {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        return;
    }

    byteCount = (size_t) st.st_size;
    b->rules = mmap( NULL, byteCount, PROT_READ, MAP_PRIVATE, fd, 0 );
    if( !b->rules )
    {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        close( fd );
        return;
    }

    b->fd = fd;
    b->byteCount = byteCount;
    b->ruleCount = byteCount / sizeof( struct tr_ipv4_range );

    {
        char * base = tr_basename( b->filename );
        tr_inf( _( "Blocklist \"%s\" contains %zu entries" ), base, b->ruleCount );
        tr_free( base );
    }
}

static void
blocklistEnsureLoaded( tr_blocklist * b )
{
    if( !b->rules )
        blocklistLoad( b );
}

static int
compareAddressToRange( const void * va,
                       const void * vb )
{
    const uint32_t *             a = va;
    const struct tr_ipv4_range * b = vb;

    if( *a < b->begin ) return -1;
    if( *a > b->end ) return 1;
    return 0;
}

static void
blocklistDelete( tr_blocklist * b )
{
    blocklistClose( b );
    unlink( b->filename );
}

/***
****  PACKAGE-VISIBLE
***/

tr_blocklist *
_tr_blocklistNew( const char * filename, bool isEnabled )
{
    tr_blocklist * b;

    b = tr_new0( tr_blocklist, 1 );
    b->fd = -1;
    b->filename = tr_strdup( filename );
    b->isEnabled = isEnabled;

    return b;
}

const char*
_tr_blocklistGetFilename( const tr_blocklist * b )
{
    return b->filename;
}

void
_tr_blocklistFree( tr_blocklist * b )
{
    blocklistClose( b );
    tr_free( b->filename );
    tr_free( b );
}

int
_tr_blocklistExists( const tr_blocklist * b )
{
    struct stat st;

    return !stat( b->filename, &st );
}

int
_tr_blocklistGetRuleCount( const tr_blocklist * b )
{
    blocklistEnsureLoaded( (tr_blocklist*)b );

    return b->ruleCount;
}

int
_tr_blocklistIsEnabled( tr_blocklist * b )
{
    return b->isEnabled;
}

void
_tr_blocklistSetEnabled( tr_blocklist * b, bool isEnabled )
{
    b->isEnabled = isEnabled ? 1 : 0;
}

int
_tr_blocklistHasAddress( tr_blocklist * b, const tr_address * addr )
{
    uint32_t                     needle;
    const struct tr_ipv4_range * range;

    assert( tr_address_is_valid( addr ) );

    if( !b->isEnabled || addr->type == TR_AF_INET6 )
        return 0;

    blocklistEnsureLoaded( b );

    if( !b->rules || !b->ruleCount )
        return 0;

    needle = ntohl( addr->addr.addr4.s_addr );

    range = bsearch( &needle,
                     b->rules,
                     b->ruleCount,
                     sizeof( struct tr_ipv4_range ),
                     compareAddressToRange );

    return range != NULL;
}

/*
 * P2P plaintext format: "comment:x.x.x.x-y.y.y.y"
 * http://wiki.phoenixlabs.org/wiki/P2P_Format
 * http://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
 */
static bool
parseLine1( const char * line, struct tr_ipv4_range * range )
{
    char * walk;
    int b[4];
    int e[4];
    char str[64];
    tr_address addr;

    walk = strrchr( line, ':' );
    if( !walk )
        return false;
    ++walk; /* walk past the colon */

    if( sscanf( walk, "%d.%d.%d.%d-%d.%d.%d.%d",
                &b[0], &b[1], &b[2], &b[3],
                &e[0], &e[1], &e[2], &e[3] ) != 8 )
        return false;

    tr_snprintf( str, sizeof( str ), "%d.%d.%d.%d", b[0], b[1], b[2], b[3] );
    if( !tr_address_from_string( &addr, str ) )
        return false;
    range->begin = ntohl( addr.addr.addr4.s_addr );

    tr_snprintf( str, sizeof( str ), "%d.%d.%d.%d", e[0], e[1], e[2], e[3] );
    if( !tr_address_from_string( &addr, str ) )
        return false;
    range->end = ntohl( addr.addr.addr4.s_addr );

    return true;
}

/*
 * DAT format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"
 * http://wiki.phoenixlabs.org/wiki/DAT_Format
 */
static bool
parseLine2( const char * line, struct tr_ipv4_range * range )
{
    int unk;
    int a[4];
    int b[4];
    char str[32];
    tr_address addr;

    if( sscanf( line, "%3d.%3d.%3d.%3d - %3d.%3d.%3d.%3d , %3d , ",
                &a[0], &a[1], &a[2], &a[3],
                &b[0], &b[1], &b[2], &b[3],
                &unk ) != 9 )
        return false;

    tr_snprintf( str, sizeof(str), "%d.%d.%d.%d", a[0], a[1], a[2], a[3] );
    if( !tr_address_from_string( &addr, str ) )
        return false;
    range->begin = ntohl( addr.addr.addr4.s_addr );

    tr_snprintf( str, sizeof(str), "%d.%d.%d.%d", b[0], b[1], b[2], b[3] );
    if( !tr_address_from_string( &addr, str ) )
        return false;
    range->end = ntohl( addr.addr.addr4.s_addr );

    return true;
}

static int
parseLine( const char * line, struct tr_ipv4_range * range )
{
    return parseLine1( line, range )
        || parseLine2( line, range );
}

static int
compareAddressRangesByFirstAddress( const void * va, const void * vb )
{
    const struct tr_ipv4_range * a = va;
    const struct tr_ipv4_range * b = vb;
    if( a->begin != b->begin )
        return a->begin < b->begin ? -1 : 1;
    return 0;
}

int
_tr_blocklistSetContent( tr_blocklist * b, const char * filename )
{
    FILE * in;
    FILE * out;
    int inCount = 0;
    char line[2048];
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );
    struct tr_ipv4_range * ranges = NULL;
    size_t ranges_alloc = 0;
    size_t ranges_count = 0;

    if( !filename )
    {
        blocklistDelete( b );
        return 0;
    }

    in = fopen( filename, "rb" );
    if( !in )
    {
        tr_err( err_fmt, filename, tr_strerror( errno ) );
        return 0;
    }

    blocklistClose( b );

    out = fopen( b->filename, "wb+" );
    if( !out )
    {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        fclose( in );
        return 0;
    }

    /* load the rules into memory */
    while( fgets( line, sizeof( line ), in ) != NULL )
    {
        char * walk;
        struct tr_ipv4_range range;

        ++inCount;

        /* zap the linefeed */
        if(( walk = strchr( line, '\r' ))) *walk = '\0';
        if(( walk = strchr( line, '\n' ))) *walk = '\0';

        if( !parseLine( line, &range ) )
        {
            /* don't try to display the actual lines - it causes issues */
            tr_err( _( "blocklist skipped invalid address at line %d" ), inCount );
            continue;
        }

        if( ranges_alloc == ranges_count )
        {
            ranges_alloc += 4096; /* arbitrary */
            ranges = tr_renew( struct tr_ipv4_range, ranges, ranges_alloc );
        }

        ranges[ranges_count++] = range;
    }

    if( ranges_count > 0 ) /* sort and merge */
    {
        struct tr_ipv4_range * r;
        struct tr_ipv4_range * keep = ranges;
        const struct tr_ipv4_range * end;

        /* sort */
        qsort( ranges, ranges_count, sizeof( struct tr_ipv4_range ),
               compareAddressRangesByFirstAddress );

        /* merge */
        for( r=ranges+1, end=ranges+ranges_count; r!=end; ++r ) {
            if( keep->end < r->begin )
                *++keep = *r;
            else if( keep->end < r->end )
                keep->end = r->end;
        }

        ranges_count = keep + 1 - ranges;

#ifndef NDEBUG
        /* sanity checks: make sure the rules are sorted
         * in ascending order and don't overlap */
        {
            size_t i;

            for( i=0; i<ranges_count; ++i )
                assert( ranges[i].begin <= ranges[i].end );

            for( i=1; i<ranges_count; ++i )
                assert( ranges[i-1].end < ranges[i].begin );
        }
#endif
    }

    if( fwrite( ranges, sizeof( struct tr_ipv4_range ), ranges_count, out ) != ranges_count )
        tr_err( _( "Couldn't save file \"%1$s\": %2$s" ), b->filename, tr_strerror( errno ) );
    else {
        char * base = tr_basename( b->filename );
        tr_inf( _( "Blocklist \"%s\" updated with %zu entries" ), base, ranges_count );
        tr_free( base );
    }

    tr_free( ranges );
    fclose( out );
    fclose( in );

    blocklistLoad( b );

    return ranges_count;
}
