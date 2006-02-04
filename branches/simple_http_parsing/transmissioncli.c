/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <transmission.h>
#ifdef SYS_BEOS
#include <kernel/OS.h>
#define usleep snooze
#endif

#define USAGE \
"Usage: %s [options] file.torrent [options]\n\n" \
"Options:\n" \
"  -h, --help           Print this help and exit\n" \
"  -i, --info           Print metainfo and exit\n" \
"  -s, --scrape         Print counts of seeders/leechers and exit\n" \
"  -v, --verbose <int>  Verbose level (0 to 2, default = 0)\n" \
"  -p, --port <int>     Port we should listen on (default = %d)\n" \
"  -u, --upload <int>   Maximum upload rate (-1 = no limit, default = 20)\n" \
"  -f, --finish <shell script> Command you wish to run on completion\n"

static int             showHelp     = 0;
static int             showInfo     = 0;
static int             showScrape   = 0;
static int             verboseLevel = 0;
static int             bindPort     = TR_DEFAULT_PORT;
static int             uploadLimit  = 20;
static char          * torrentPath  = NULL;
static volatile char   mustDie      = 0;

static char          * finishCall   = NULL;

static int  parseCommandLine ( int argc, char ** argv );
static void sigHandler       ( int signal );

int main( int argc, char ** argv )
{
    int           i, count;
    tr_handle_t * h;
    tr_stat_t   * s;

    printf( "Transmission %s - http://transmission.m0k.org/\n\n",
            VERSION_STRING );

    /* Get options */
    if( parseCommandLine( argc, argv ) )
    {
        printf( USAGE, argv[0], TR_DEFAULT_PORT );
        return 1;
    }

    if( showHelp )
    {
        printf( USAGE, argv[0], TR_DEFAULT_PORT );
        return 0;
    }

    if( verboseLevel < 0 )
    {
        verboseLevel = 0;
    }
    else if( verboseLevel > 9 )
    {
        verboseLevel = 9;
    }
    if( verboseLevel )
    {
        static char env[11];
        sprintf( env, "TR_DEBUG=%d", verboseLevel );
        putenv( env );
    }

    if( bindPort < 1 || bindPort > 65535 )
    {
        printf( "Invalid port '%d'\n", bindPort );
        return 1;
    }

    /* Initialize libtransmission */
    h = tr_init();

    /* Open and parse torrent file */
    if( tr_torrentInit( h, torrentPath ) )
    {
        printf( "Failed opening torrent file `%s'\n", torrentPath );
        goto failed;
    }

    if( showInfo )
    {
        tr_info_t * info;

        count = tr_torrentStat( h, &s );
        info  = &s[0].info;

        /* Print torrent info (quite � la btshowmetainfo) */
        printf( "hash:     " );
        for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
        {
            printf( "%02x", info->hash[i] );
        }
        printf( "\n" );
        printf( "tracker:  %s:%d\n",
                info->trackerAddress, info->trackerPort );
        printf( "announce: %s\n", info->trackerAnnounce );
        printf( "size:     %lld (%lld * %d + %lld)\n",
                info->totalSize, info->totalSize / info->pieceSize,
                info->pieceSize, info->totalSize % info->pieceSize );
        printf( "file(s):\n" );
        for( i = 0; i < info->fileCount; i++ )
        {
            printf( " %s (%lld)\n", info->files[i].name,
                    info->files[i].length );
        }

        free( s );
        goto cleanup;
    }

    if( showScrape )
    {
        int seeders, leechers;

        if( tr_torrentScrape( h, 0, &seeders, &leechers ) )
        {
            printf( "Scrape failed.\n" );
        }
        else
        {
            printf( "%d seeder(s), %d leecher(s).\n", seeders, leechers );
        }

        goto cleanup;
    }

    signal( SIGINT, sigHandler );

    tr_setBindPort( h, bindPort );
    tr_setUploadLimit( h, uploadLimit );
    
    tr_torrentSetFolder( h, 0, "." );
    tr_torrentStart( h, 0 );

    while( !mustDie )
    {
        char string[80];
        int  chars = 0;
        int result;

        sleep( 1 );

        count = tr_torrentStat( h, &s );

        if( s[0].status & TR_STATUS_CHECK )
        {
            chars = snprintf( string, 80,
                "Checking files... %.2f %%", 100.0 * s[0].progress );
        }
        else if( s[0].status & TR_STATUS_DOWNLOAD )
        {
            chars = snprintf( string, 80,
                "Progress: %.2f %%, %d peer%s, dl from %d (%.2f KB/s), "
                "ul to %d (%.2f KB/s)", 100.0 * s[0].progress,
                s[0].peersTotal, ( s[0].peersTotal == 1 ) ? "" : "s",
                s[0].peersUploading, s[0].rateDownload,
                s[0].peersDownloading, s[0].rateUpload );
        }
        else if( s[0].status & TR_STATUS_SEED )
        {
            chars = snprintf( string, 80,
                "Seeding, uploading to %d of %d peer(s), %.2f KB/s",
                s[0].peersDownloading, s[0].peersTotal,
                s[0].rateUpload );
        }
        memset( &string[chars], ' ', 79 - chars );
        string[79] = '\0';
        fprintf( stderr, "\r%s", string );

        if( s[0].status & TR_TRACKER_ERROR )
        {
            fprintf( stderr, "\n%s\n", s[0].error );
        }
        else if( verboseLevel > 0 )
        {
            fprintf( stderr, "\n" );
        }
        
        if( tr_getFinished( h, 0 ) )
        {
            tr_setFinished( h, 0, 0 );
            result = system(finishCall);
        }

        free( s );
    }
    fprintf( stderr, "\n" );

    /* Try for 5 seconds to notice the tracker that we are leaving */
    tr_torrentStop( h, 0 );
    for( i = 0; i < 10; i++ )
    {
        count = tr_torrentStat( h, &s );
        if( s[0].status & TR_STATUS_PAUSE )
        {
            /* The 'stopped' message was sent */
            free( s );
            break;
        }
        free( s );
        usleep( 500000 );
    }
    
cleanup:
    tr_torrentClose( h, 0 );

failed:
    tr_close( h );

    return 0;
}

static int parseCommandLine( int argc, char ** argv )
{
    for( ;; )
    {
        static struct option long_options[] =
          { { "help",    no_argument,       NULL, 'h' },
            { "info",    no_argument,       NULL, 'i' },
            { "scrape",  no_argument,       NULL, 's' },
            { "verbose", required_argument, NULL, 'v' },
            { "port",    required_argument, NULL, 'p' },
            { "upload",  required_argument, NULL, 'u' },
            { "finish",  required_argument, NULL, 'f' },
            { 0, 0, 0, 0} };

        int c, optind = 0;
        c = getopt_long( argc, argv, "hisv:p:u:f:", long_options, &optind );
        if( c < 0 )
        {
            break;
        }
        switch( c )
        {
            case 'h':
                showHelp = 1;
                break;
            case 'i':
                showInfo = 1;
                break;
            case 's':
                showScrape = 1;
                break;
            case 'v':
                verboseLevel = atoi( optarg );
                break;
            case 'p':
                bindPort = atoi( optarg );
                break;
            case 'u':
                uploadLimit = atoi( optarg );
                break;
            case 'f':
                finishCall = optarg;
                break;
            default:
                return 1;
        }
    }

    if( optind > argc - 1  )
    {
        return !showHelp;
    }

    torrentPath = argv[optind];

    return 0;
}

static void sigHandler( int signal )
{
    switch( signal )
    {
        case SIGINT:
            mustDie = 1;
            break;

        default:
            break;
    }
}
