/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "transmission.h"

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static void strcatUTF8( char *, char * );

/***********************************************************************
 * tr_metainfoParse
 ***********************************************************************
 *
 **********************************************************************/
int tr_metainfoParse( tr_info_t * inf, const char * path,
                      const char * savedHash, int saveCopy )
{
    FILE       * file;
    char       * buf;
    benc_val_t   meta, * beInfo, * list, * val;
    char * s, * s2, * s3;
    int          i;
    struct stat sb;

    assert( NULL == path || NULL == savedHash );
    /* if savedHash isn't null, saveCopy should be false */
    assert( NULL == savedHash || !saveCopy );

    if ( NULL != savedHash )
    {
        snprintf( inf->torrent, MAX_PATH_LENGTH, "%s/%s",
                  tr_getTorrentsDirectory(), savedHash );
        path = inf->torrent;
    }

    if( stat( path, &sb ) )
    {
        fprintf( stderr, "Could not stat file (%s)\n", path );
        return 1;
    }
    if( ( sb.st_mode & S_IFMT ) != S_IFREG )
    {
        fprintf( stderr, "Not a regular file (%s)\n", path );
        return 1;
    }
    if( sb.st_size > 2097152 )
    {
        tr_err( "Torrent file is too big (%d bytes)", sb.st_size );
        return 1;
    }

    /* Load the torrent file into our buffer */
    file = fopen( path, "rb" );
    if( !file )
    {
        fprintf( stderr, "Could not open file (%s)\n", path );
        return 1;
    }
    buf = malloc( sb.st_size );
    fseek( file, 0, SEEK_SET );
    if( fread( buf, sb.st_size, 1, file ) != 1 )
    {
        fprintf( stderr, "Read error (%s)\n", path );
        free( buf );
        fclose( file );
        return 1;
    }
    fclose( file );

    /* Parse bencoded infos */
    if( tr_bencLoad( buf, sb.st_size, &meta, NULL ) )
    {
        fprintf( stderr, "Error while parsing bencoded data\n" );
        free( buf );
        return 1;
    }

    /* Get info hash */
    if( !( beInfo = tr_bencDictFind( &meta, "info" ) ) )
    {
        fprintf( stderr, "Could not find \"info\" dictionary\n" );
        tr_bencFree( &meta );
        free( buf );
        return 1;
    }
    SHA1( (uint8_t *) beInfo->begin,
          (long) beInfo->end - (long) beInfo->begin, inf->hash );
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        sprintf( inf->hashString + i * 2, "%02x", inf->hash[i] );
    }

    if( saveCopy )
    {
        /* Create the private torrents directory */
        if( mkdir( tr_getTorrentsDirectory(), 0777 ) && EEXIST != errno )
        {
            fprintf( stderr, "Could not create directory (%s)\n",
                     tr_getTorrentsDirectory() );
            tr_bencFree( &meta );
            free( buf );
            return 1;
        }

        /* Save a copy of the torrent file in the private torrent directory */
        snprintf( inf->torrent, MAX_PATH_LENGTH, "%s/%s",
                  tr_getTorrentsDirectory(), inf->hashString );
        file = fopen( inf->torrent, "wb" );
        if( !file )
        {
            fprintf( stderr, "Could not open file (%s)\n", inf->torrent );
            tr_bencFree( &meta );
            free( buf );
            return 1;
        }
        fseek( file, 0, SEEK_SET );
        if( fwrite( buf, sb.st_size, 1, file ) != 1 )
        {
            fprintf( stderr, "Write error (%s)\n", inf->torrent );
            tr_bencFree( &meta );
            free( buf );
            fclose( file );
            return 1;
        }
        fclose( file );
    }
    else
    {
        snprintf( inf->torrent, MAX_PATH_LENGTH, "%s", path );
    }

    /* We won't need this anymore */
    free( buf );

    if( !( val = tr_bencDictFind( &meta, "announce" ) ) )
    {
        fprintf( stderr, "No \"announce\" entry\n" );
        tr_bencFree( &meta );
        return 1;
    }
    
    /* Skip spaces */
    s3 = val->val.s.s;
    while( *s3 && *s3 == ' ' )
    {
        s3++;
    }

    /* Parse announce URL */
    if( strncmp( s3, "http://", 7 ) )
    {
        fprintf( stderr, "Invalid announce URL (%s)\n",
                 inf->trackerAddress );
        tr_bencFree( &meta );
        return 1;
    }
    s  = strchr( s3 + 7, ':' );
    s2 = strchr( s3 + 7, '/' );
    if( s && s < s2 )
    {
        memcpy( inf->trackerAddress, s3 + 7,
                (long) s - (long) s3 - 7 );
        inf->trackerPort = atoi( s + 1 );
    }
    else if( s2 )
    {
        memcpy( inf->trackerAddress, s3 + 7,
                (long) s2 - (long) s3 - 7 );
        inf->trackerPort = 80;
    }
    else
    {
        fprintf( stderr, "Invalid announce URL (%s)\n",
                 inf->trackerAddress );
        tr_bencFree( &meta );
        return 1;
    }
    snprintf( inf->trackerAnnounce, MAX_PATH_LENGTH, "%s", s2 );

    /* Piece length */
    if( !( val = tr_bencDictFind( beInfo, "piece length" ) ) )
    {
        fprintf( stderr, "No \"piece length\" entry\n" );
        tr_bencFree( &meta );
        return 1;
    }
    inf->pieceSize = val->val.i;

    /* Hashes */
    val = tr_bencDictFind( beInfo, "pieces" );
    if( val->val.s.i % SHA_DIGEST_LENGTH )
    {
        fprintf( stderr, "Invalid \"piece\" string (size is %d)\n",
                 val->val.s.i );
        return 1;
    }
    inf->pieceCount = val->val.s.i / SHA_DIGEST_LENGTH;
    inf->pieces = (uint8_t *) val->val.s.s; /* Ugly, but avoids a memcpy */
    val->val.s.s = NULL;

    /* TODO add more tests so we don't crash on weird files */

    inf->totalSize = 0;
    if( ( list = tr_bencDictFind( beInfo, "files" ) ) )
    {
        /* Multi-file mode */
        int j;

        val = tr_bencDictFind( beInfo, "name" );
        strcatUTF8( inf->name, val->val.s.s );

        inf->fileCount = list->val.l.count;
        inf->files     = calloc( inf->fileCount * sizeof( tr_file_t ), 1 );

        for( i = 0; i < list->val.l.count; i++ )
        {
            val = tr_bencDictFind( &list->val.l.vals[i], "path" );
            strcatUTF8( inf->files[i].name, inf->name );
            for( j = 0; j < val->val.l.count; j++ )
            {
                strcatUTF8( inf->files[i].name, "/" );
                strcatUTF8( inf->files[i].name,
                            val->val.l.vals[j].val.s.s );
            }
            val = tr_bencDictFind( &list->val.l.vals[i], "length" );
            inf->files[i].length  = val->val.i;
            inf->totalSize       += val->val.i;
        }

    }
    else
    {
        /* Single-file mode */
        inf->fileCount = 1;
        inf->files     = calloc( sizeof( tr_file_t ), 1 );

        val = tr_bencDictFind( beInfo, "name" );
        strcatUTF8( inf->files[0].name, val->val.s.s );
        strcatUTF8( inf->name, val->val.s.s );
        
        val = tr_bencDictFind( beInfo, "length" );
        inf->files[0].length  = val->val.i;
        inf->totalSize       += val->val.i;
    }

    if( (uint64_t) inf->pieceCount !=
        ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
    {
        fprintf( stderr, "Size of hashes and files don't match\n" );
        tr_bencFree( &meta );
        return 1;
    }

    tr_bencFree( &meta );
    return 0;
}

void tr_metainfoRemoveSaved( const char * hashString )
{
    char file[MAX_PATH_LENGTH];

    snprintf( file, MAX_PATH_LENGTH, "%s/%s",
              tr_getTorrentsDirectory(), hashString );
    unlink(file);
}

/***********************************************************************
 * strcatUTF8
 ***********************************************************************
 * According to the official specification, all strings in the torrent
 * file are supposed to be UTF-8 encoded. However, there are
 * non-compliant torrents around... If we encounter an invalid UTF-8
 * character, we assume it is ISO 8859-1 and convert it to UTF-8.
 **********************************************************************/
static void strcatUTF8( char * s, char * append )
{
    char * p;

    /* Go to the end of the destination string */
    while( s[0] )
    {
        s++;
    }

    /* Now start appending, converting on the fly if necessary */
    for( p = append; p[0]; )
    {
        if( !( p[0] & 0x80 ) )
        {
            /* ASCII character */
            *(s++) = *(p++);
            continue;
        }

        if( ( p[0] & 0xE0 ) == 0xC0 && ( p[1] & 0xC0 ) == 0x80 )
        {
            /* 2-bytes UTF-8 character */
            *(s++) = *(p++); *(s++) = *(p++);
            continue;
        }

        if( ( p[0] & 0xF0 ) == 0xE0 && ( p[1] & 0xC0 ) == 0x80 &&
            ( p[2] & 0xC0 ) == 0x80 )
        {
            /* 3-bytes UTF-8 character */
            *(s++) = *(p++); *(s++) = *(p++);
            *(s++) = *(p++);
            continue;
        }

        if( ( p[0] & 0xF8 ) == 0xF0 && ( p[1] & 0xC0 ) == 0x80 &&
            ( p[2] & 0xC0 ) == 0x80 && ( p[3] & 0xC0 ) == 0x80 )
        {
            /* 4-bytes UTF-8 character */
            *(s++) = *(p++); *(s++) = *(p++);
            *(s++) = *(p++); *(s++) = *(p++);
            continue;
        }

        /* ISO 8859-1 -> UTF-8 conversion */
        *(s++) = 0xC0 | ( ( *p & 0xFF ) >> 6 );
        *(s++) = 0x80 | ( *(p++) & 0x3F );
    }
}
