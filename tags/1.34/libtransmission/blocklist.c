/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h> /* free */
#include <string.h>

#include <libgen.h> /* basename */
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "ggets.h"

#include "transmission.h"
#include "blocklist.h"
#include "net.h" /* tr_netResolve() */
#include "utils.h"

/***
****  PRIVATE
***/

struct tr_ip_range
{
    uint32_t begin;
    uint32_t end;
};

struct tr_blocklist
{
    unsigned int isEnabled : 1;
    int fd;
    size_t ruleCount;
    size_t byteCount;
    char * filename;
    struct tr_ip_range * rules;
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
    struct stat st;
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    blocklistClose( b );

    if( stat( b->filename, &st ) == -1 )
        return;

    fd = open( b->filename, O_RDONLY );
    if( fd == -1 ) {
        tr_err( err_fmt, b->filename, tr_strerror(errno) );
        return;
    }

    b->rules = mmap( NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    if( !b->rules ) {
        tr_err( err_fmt, b->filename, tr_strerror(errno) );
        close( fd );
        return;
    }

    b->byteCount = st.st_size;
    b->ruleCount = st.st_size / sizeof( struct tr_ip_range );
    b->fd = fd;

    {
        char * name;
        char buf[MAX_PATH_LENGTH];
        tr_strlcpy( buf, b->filename, sizeof( buf ) );
        name = basename( buf );
        tr_inf( _( "Blocklist \"%s\" contains %'u entries" ), name, (unsigned int)b->ruleCount );
    }
}

static void
blocklistEnsureLoaded( tr_blocklist * b )
{
    if( !b->rules )
        blocklistLoad( b );
}

static int
compareAddressToRange( const void * va, const void * vb )
{
    const uint32_t * a = va;
    const struct tr_ip_range * b = vb;
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
_tr_blocklistNew( const char * filename, int isEnabled )
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
_tr_blocklistSetEnabled( tr_blocklist * b, int isEnabled )
{
    b->isEnabled = isEnabled ? 1 : 0;
}

int
_tr_blocklistHasAddress( tr_blocklist * b, const struct in_addr * addr )
{
    uint32_t needle;
    const struct tr_ip_range * range;

    if( !b->isEnabled )
        return 0;

    blocklistEnsureLoaded( b );
    if( !b->rules )
        return 0;

    needle = ntohl( addr->s_addr );

    range = bsearch( &needle,
                     b->rules,
                     b->ruleCount,
                     sizeof( struct tr_ip_range ), 
                     compareAddressToRange );

    return range != NULL;
}

int
_tr_blocklistSetContent( tr_blocklist * b,
                         const char   * filename )
{
    FILE * in;
    FILE * out;
    char * line;
    int lineCount = 0;
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    if( !filename ) {
        blocklistDelete( b );
        return 0;
    }

    in = fopen( filename, "r" );
    if( !in ) {
        tr_err( err_fmt, filename, tr_strerror(errno) );
        return 0;
    }

    blocklistClose( b );
  
    out = fopen( b->filename, "wb+" );
    if( !out ) {
        tr_err( err_fmt, b->filename, tr_strerror( errno ) );
        fclose( in );
        return 0;
    }

    while( !fggets( &line, in ) )
    {
        char * rangeBegin;
        char * rangeEnd;
        struct in_addr in_addr;
        struct tr_ip_range range;

        rangeBegin = strrchr( line, ':' );
        if( !rangeBegin ) { free(line); continue; }
        ++rangeBegin;

        rangeEnd = strchr( rangeBegin, '-' );
        if( !rangeEnd ) { free(line); continue; }
        *rangeEnd++ = '\0';

        if( tr_netResolve( rangeBegin, &in_addr ) )
            tr_err( "blocklist skipped invalid address [%s]\n", rangeBegin );
        range.begin = ntohl( in_addr.s_addr );

        if( tr_netResolve( rangeEnd, &in_addr ) )
            tr_err( "blocklist skipped invalid address [%s]\n", rangeEnd );
        range.end = ntohl( in_addr.s_addr );

        free( line );

        if( fwrite( &range, sizeof(struct tr_ip_range), 1, out ) != 1 ) {
          tr_err( _( "Couldn't save file \"%1$s\": %2$s" ), b->filename, tr_strerror( errno ) );
          break;
        }

        ++lineCount;
    }

    {
        char * name;
        char buf[MAX_PATH_LENGTH];
        tr_strlcpy( buf, b->filename, sizeof( buf ) );
        name = basename( buf );
        tr_inf( _( "Blocklist \"%1$s\" updated with %2$'d entries" ), name, lineCount );
    }


    fclose( out );
    fclose( in );

    blocklistLoad( b );

    return lineCount;
}
