/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strchr() */

#include "transmission.h"
#include "bencode.h"
#include "magnet.h"
#include "utils.h"
#include "web.h"

/***
****
***/

/* this base32 code converted from code by Robert Kaye and Gordon Mohr
 * and is public domain.  see http://bitzi.com/publicdomain for more info */

static const int base32Lookup[] =
{
    0xFF,0xFF,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F, /* '0', '1', '2', '3', '4', '5', '6', '7' */
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, /* '8', '9', ':', ';', '<', '=', '>', '?' */
    0xFF,0x00,0x01,0x02,0x03,0x04,0x05,0x06, /* '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G' */
    0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E, /* 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O' */
    0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16, /* 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W' */
    0x17,0x18,0x19,0xFF,0xFF,0xFF,0xFF,0xFF, /* 'X', 'Y', 'Z', '[', '\', ']', '^', '_' */
    0xFF,0x00,0x01,0x02,0x03,0x04,0x05,0x06, /* '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g' */
    0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E, /* 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o' */
    0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16, /* 'p', 'q', 'r', 's', 't', 'u', 'v', 'w' */
    0x17,0x18,0x19,0xFF,0xFF,0xFF,0xFF,0xFF  /* 'x', 'y', 'z', '{', '|', '}', '~', 'DEL' */
};

static const int base32LookupLen = sizeof( base32Lookup ) / sizeof( base32Lookup[0] );

static void
base32_to_sha1( uint8_t * out, const char * in, const int inlen )
{
    const int outlen = 20;
    int i, index, offset;

    memset( out, 0, 20 );

    assert( inlen == 32 );

    for( i=0, index=0, offset=0; i<inlen; ++i )
    {
        int digit;
        int lookup = in[i] - '0';

        /* Skip chars outside the lookup table */
        if( lookup<0 || lookup>=base32LookupLen )
            continue;

        /* If this digit is not in the table, ignore it */
        digit = base32Lookup[lookup];
        if( digit == 0xFF )
            continue;

        if( index <= 3 ) {
            index = (index + 5) % 8;
            if( index == 0 ) {
                out[offset] |= digit;
                offset++;
                if( offset >= outlen )
                    break;
            } else {
                out[offset] |= digit << (8 - index);
            }
        } else {
            index = (index + 5) % 8;
            out[offset] |= (digit >> index);
            offset++;

            if( offset >= outlen )
                break;
            out[offset] |= digit << (8 - index);
       }
   }
}

/***
****
***/

#define MAX_TRACKERS 64
#define MAX_WEBSEEDS 64

tr_magnet_info *
tr_magnetParse( const char * uri )
{
    tr_bool got_checksum = FALSE;
    int trCount = 0;
    int wsCount = 0;
    char * tr[MAX_TRACKERS];
    char * ws[MAX_WEBSEEDS];
    char * displayName = NULL;
    uint8_t sha1[SHA_DIGEST_LENGTH];
    tr_magnet_info * info = NULL;

    if( ( uri != NULL ) && !memcmp( uri, "magnet:?", 8 ) )
    {
        const char * walk;

        for( walk=uri+8; walk && *walk; )
        {
            const char * key = walk;
            const char * delim = strchr( key, '=' );
            const char * val = delim == NULL ? NULL : delim + 1;
            const char * next = strchr( delim == NULL ? key : val, '&' );
            int keylen, vallen;

            if( delim != NULL )
                keylen = delim - key;
            else if( next != NULL )
                keylen = next - key;
            else
                keylen = strlen( key );

            if( val == NULL )
                vallen = 0;
            else if( next != NULL )
                vallen = next - val;
            else
                vallen = strlen( val );

            if( ( keylen==2 ) && !memcmp( key, "xt", 2 ) && !memcmp( val, "urn:btih:", 9 ) )
            {
                const char * hash = val + 9;
                const int hashlen = vallen - 9;

                if( hashlen == 40 ) {
                    tr_hex_to_sha1( sha1, hash );
                    got_checksum = TRUE;
                }
                else if( hashlen == 32 ) {
                    base32_to_sha1( sha1, hash, hashlen );
                    got_checksum = TRUE;
                }
            }

            if( ( keylen==2 ) && !memcmp( key, "dn", 2 ) )
                displayName = tr_http_unescape( val, vallen );

            if( trCount < MAX_TRACKERS ) {
                int i;
                if( ( keylen==2 ) && !memcmp( key, "tr", 2 ) )
                    tr[trCount++] = tr_http_unescape( val, vallen );
                else if( ( sscanf( key, "tr.%d=", &i ) == 1 ) && ( i > 0 ) ) /* ticket #3341 */
                    tr[trCount++] = tr_http_unescape( val, vallen );
            }

            if( ( keylen==2 ) && !memcmp( key, "ws", 2 ) && ( wsCount < MAX_TRACKERS ) )
                ws[wsCount++] = tr_http_unescape( val, vallen );

            walk = next != NULL ? next + 1 : NULL;
        }
    }

    if( got_checksum )
    {
        info = tr_new0( tr_magnet_info, 1 );
        info->displayName = displayName;
        info->trackerCount = trCount;
        info->trackers = tr_memdup( tr, sizeof(char*) * trCount );
        info->webseedCount = wsCount;
        info->webseeds = tr_memdup( ws, sizeof(char*) * wsCount );
        memcpy( info->hash, sha1, sizeof(uint8_t) * SHA_DIGEST_LENGTH );
    }

    return info;
}

void
tr_magnetFree( tr_magnet_info * info )
{
    if( info != NULL )
    {
        int i;

        for( i=0; i<info->trackerCount; ++i )
            tr_free( info->trackers[i] );
        tr_free( info->trackers );

        for( i=0; i<info->webseedCount; ++i )
            tr_free( info->webseeds[i] );
        tr_free( info->webseeds );

        tr_free( info->displayName );
        tr_free( info );
    }
}

void
tr_magnetCreateMetainfo( const tr_magnet_info * info, tr_benc * top )
{
    int i;
    tr_benc * d;
    tr_bencInitDict( top, 4 );

    /* announce list */
    if( info->trackerCount == 1 )
        tr_bencDictAddStr( top, "announce", info->trackers[0] );
    else {
        tr_benc * trackers = tr_bencDictAddList( top, "announce-list", info->trackerCount );
        for( i=0; i<info->trackerCount; ++i )
            tr_bencListAddStr( tr_bencListAddList( trackers, 1 ), info->trackers[i] );
    }

    /* webseeds */
    if( info->webseedCount > 0 ) {
        tr_benc * urls = tr_bencDictAddList( top, "url-list", info->webseedCount );
        for( i=0; i<info->webseedCount; ++i )
            tr_bencListAddStr( urls, info->webseeds[i] );
    }

    /* nonstandard keys */
    d = tr_bencDictAddDict( top, "magnet-info", 2 );
    tr_bencDictAddRaw( d, "info_hash", info->hash, 20 );
    if( info->displayName != NULL )
        tr_bencDictAddStr( d, "display-name", info->displayName );
}


