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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/metainfo.h> /* tr_metainfoFree */
#include <libtransmission/utils.h> /* tr_wait */


/* macro to shut up "unused parameter" warnings */
#ifdef __GNUC__
#define UNUSED                  __attribute__((unused))
#else
#define UNUSED
#endif

const char * USAGE =
"Usage: %s [-car[-m]] [-dfginpsuv] [-h] file.torrent [output-dir]\n\n"
"Options:\n"
"  -h, --help                Print this help and exit\n" 
"  -i, --info                Print metainfo and exit\n"
"  -s, --scrape              Print counts of seeders/leechers and exit\n"
"  -V, --version             Print the version number and exit\n"
"  -c, --create-from <file>  Create torrent from the specified source file.\n"
"  -a, --announce <url>      Used in conjunction with -c.\n"
"  -g, --config-dir <path>   Where to look for configuration files\n"
"  -r, --private             Used in conjunction with -c.\n"
"  -m, --comment <text>      Adds an optional comment when creating a torrent.\n"
"  -d, --download <int>      Max download rate (-1 = no limit, default = -1)\n"
"  -f, --finish <script>     Command you wish to run on completion\n" 
"  -n  --nat-traversal       Attempt NAT traversal using NAT-PMP or UPnP IGD\n"
"  -p, --port <int>          Port we should listen on (default = %d)\n"
"  -u, --upload <int>        Maximum upload rate (-1 = no limit, default = 20)\n"
"  -v, --verbose <int>       Verbose level (0 to 2, default = 0)\n"
"  -y, --recheck             Force a recheck of the torrent data\n";

static int           showHelp      = 0;
static int           showInfo      = 0;
static int           showScrape    = 0;
static int           showVersion   = 0;
static int           isPrivate     = 0;
static int           verboseLevel  = 0;
static int           bindPort      = TR_DEFAULT_PORT;
static int           uploadLimit   = 20;
static int           downloadLimit = -1;
static char        * torrentPath   = NULL;
static char        * savePath      = ".";
static int           natTraversal  = 0;
static int           recheckData   = 0;
static sig_atomic_t  gotsig        = 0;
static sig_atomic_t  manualUpdate  = 0;

static char          * finishCall   = NULL;
static char          * announce     = NULL;
static char          * configdir    = NULL;
static char          * sourceFile   = NULL;
static char          * comment      = NULL;

static int  parseCommandLine ( int argc, char ** argv );
static void sigHandler       ( int signal );

static char *
getStringRatio( float ratio )
{
    static char string[20];

    if( ratio == TR_RATIO_NA )
        return "n/a";
    snprintf( string, sizeof string, "%.3f", ratio );
    return string;
}

#define LINEWIDTH 80

static void
torrentStateChanged( tr_torrent   * torrent UNUSED,
                     cp_status_t    status UNUSED,
                     void         * user_data UNUSED )
{
    system( finishCall );
}

int
main( int argc, char ** argv )
{
    int i, error;
    tr_handle  * h;
    tr_ctor * ctor;
    tr_torrent * tor = NULL;

    printf( "Transmission %s - http://www.transmissionbt.com/\n",
            LONG_VERSION_STRING );

    /* Get options */
    if( parseCommandLine( argc, argv ) )
    {
        printf( USAGE, argv[0], TR_DEFAULT_PORT );
        return EXIT_FAILURE;
    }

    if( showVersion )
        return EXIT_SUCCESS;

    if( showHelp )
    {
        printf( USAGE, argv[0], TR_DEFAULT_PORT );
        return EXIT_SUCCESS;
    }

    if( bindPort < 1 || bindPort > 65535 )
    {
        printf( "Invalid port '%d'\n", bindPort );
        return EXIT_FAILURE;
    }

    /* don't bind the port if we're just running the CLI 
     * to get metainfo or to create a torrent */
    if( showInfo || showScrape || ( sourceFile != NULL ) )
        bindPort = -1;

    if( configdir == NULL )
        configdir = strdup( tr_getDefaultConfigDir( ) );

    /* Initialize libtransmission */
    h = tr_initFull( configdir,
                     "cli",                         /* tag */
                     1,                             /* pex enabled */
                     natTraversal,                  /* nat enabled */
                     bindPort,                      /* public port */
                     TR_ENCRYPTION_PREFERRED,       /* encryption mode */
                     uploadLimit >= 0,              /* use upload speed limit? */
                     uploadLimit,                   /* upload speed limit */
                     downloadLimit >= 0,            /* use download speed limit? */
                     downloadLimit,                 /* download speed limit */
                     TR_DEFAULT_GLOBAL_PEER_LIMIT,
                     verboseLevel + 1,              /* messageLevel */
                     0,                             /* is message queueing enabled? */
                     0,                             /* use the blocklist? */
                     TR_DEFAULT_PEER_SOCKET_TOS );

    if( sourceFile && *sourceFile ) /* creating a torrent */
    {
        int err;
        tr_metainfo_builder * builder = tr_metaInfoBuilderCreate( h, sourceFile );
        tr_makeMetaInfo( builder, torrentPath, announce, comment, isPrivate );
        while( !builder->isDone ) {
            tr_wait( 1000 );
            printf( "." );
        }
        err = builder->result;
        tr_metaInfoBuilderFree( builder );
        return err;
    }

    ctor = tr_ctorNew( h );
    tr_ctorSetMetainfoFromFile( ctor, torrentPath );
    tr_ctorSetPaused( ctor, TR_FORCE, 0 );
    tr_ctorSetDestination( ctor, TR_FORCE, savePath );

    if( showInfo )
    {
        tr_info info;

        if( !tr_torrentParse( h, ctor, &info ) )
        {
            int prevTier = -1;
            tr_file_index_t ff;

            printf( "hash:\t" );
            for( i=0; i<SHA_DIGEST_LENGTH; ++i )
                printf( "%02x", info.hash[i] );
            printf( "\n" );

            printf( "name:\t%s\n", info.name );

            for( i=0; i<info.trackerCount; ++i ) {
                if( prevTier != info.trackers[i].tier ) {
                    prevTier = info.trackers[i].tier;
                    printf( "\ntracker tier #%d:\n", (prevTier+1) );
                }
                printf( "\tannounce:\t%s\n", info.trackers[i].announce );
            }

            printf( "size:\t%"PRIu64" (%"PRIu64" * %d + %"PRIu64")\n",
                    info.totalSize, info.totalSize / info.pieceSize,
                    info.pieceSize, info.totalSize % info.pieceSize );

            if( info.comment[0] )
                printf( "comment:\t%s\n", info.comment );
            if( info.creator[0] )
                printf( "creator:\t%s\n", info.creator );
            if( info.isPrivate )
                printf( "private flag set\n" );

            printf( "file(s):\n" );
            for( ff=0; ff<info.fileCount; ++ff )
                printf( "\t%s (%"PRIu64")\n", info.files[ff].name, info.files[ff].length );

            tr_metainfoFree( &info );
        }

        tr_ctorFree( ctor );
        goto cleanup;
    }


    tor = tr_torrentNew( h, ctor, &error );
    tr_ctorFree( ctor );
    if( tor == NULL )
    {
        printf( "Failed opening torrent file `%s'\n", torrentPath );
        tr_close( h );
        return EXIT_FAILURE;
    }

    if( showScrape )
    {
        const struct tr_stat * stats;
        const uint64_t start = tr_date( );
        printf( "Scraping, Please wait...\n" );
        
        do
        {
            stats = tr_torrentStat( tor );
            if( !stats || tr_date() - start > 20000 )
            {
                printf( "Scrape failed.\n" );
                goto cleanup;
            }
            tr_wait( 2000 );
        }
        while( stats->completedFromTracker == -1 || stats->leechers == -1 || stats->seeders == -1 );
        
        printf( "%d seeder(s), %d leecher(s), %d download(s).\n",
            stats->seeders, stats->leechers, stats->completedFromTracker );

        goto cleanup;
    }

    signal( SIGINT, sigHandler );
    signal( SIGHUP, sigHandler );

    tr_torrentSetStatusCallback( tor, torrentStateChanged, NULL );
    tr_torrentStart( tor );

    for( ;; )
    {
        char string[LINEWIDTH];
        int  chars = 0;
        const struct tr_stat * s;

        tr_wait( 1000 );

        if( gotsig )
        {
            gotsig = 0;
            tr_torrentStop( tor );
            tr_natTraversalEnable( h, 0 );
        }
        
        if( manualUpdate )
        {
            manualUpdate = 0;
            if ( !tr_torrentCanManualUpdate( tor ) )
                fprintf( stderr, "\rReceived SIGHUP, but can't send a manual update now\n" );
            else {
                fprintf( stderr, "\rReceived SIGHUP: manual update scheduled\n" );
                tr_manualUpdate( tor );
            }
        }
        
        if( recheckData )
        {
            recheckData = 0;
            tr_torrentVerify( tor );
        }

        s = tr_torrentStat( tor );

        if( s->status & TR_STATUS_CHECK_WAIT )
        {
            chars = snprintf( string, sizeof string,
                "Waiting to verify local files..." );
        }
        else if( s->status & TR_STATUS_CHECK )
        {
            chars = snprintf( string, sizeof string,
                "Verifying local files... %.2f%%, found %.2f%% valid", 100 * s->recheckProgress, 100.0 * s->percentDone );
        }
        else if( s->status & TR_STATUS_DOWNLOAD )
        {
            chars = snprintf( string, sizeof string,
                "Progress: %.2f %%, %d peer%s, dl from %d (%.2f KB/s), "
                "ul to %d (%.2f KB/s) [%s]", 100.0 * s->percentDone,
                s->peersConnected, ( s->peersConnected == 1 ) ? "" : "s",
                s->peersSendingToUs, s->rateDownload,
                s->peersGettingFromUs, s->rateUpload,
                getStringRatio(s->ratio) );
        }
        else if( s->status & TR_STATUS_SEED )
        {
            chars = snprintf( string, sizeof string,
                "Seeding, uploading to %d of %d peer(s), %.2f KB/s [%s]",
                s->peersGettingFromUs, s->peersConnected,
                s->rateUpload, getStringRatio(s->ratio) );
        }
        else if( s->status & TR_STATUS_STOPPED )
        {
            break;
        }
        if( ( signed )sizeof string > chars )
        {
            memset( &string[chars], ' ', sizeof string - 1 - chars );
        }
        string[sizeof string - 1] = '\0';
        fprintf( stderr, "\r%s", string );

        if( s->error )
        {
            fprintf( stderr, "\n%s\n", s->errorString );
        }
        else if( verboseLevel > 0 )
        {
            fprintf( stderr, "\n" );
        }
    }
    fprintf( stderr, "\n" );

    /* Try for 5 seconds to delete any port mappings for nat traversal */
    tr_natTraversalEnable( h, 0 );
    for( i = 0; i < 10; i++ )
    {
        const tr_handle_status * hstat = tr_handleStatus( h );
        if( TR_NAT_TRAVERSAL_UNMAPPED == hstat->natTraversalStatus )
        {
            /* Port mappings were deleted */
            break;
        }
        tr_wait( 500 );
    }
    
cleanup:
    tr_torrentClose( tor );
    tr_close( h );

    return EXIT_SUCCESS;
}

static int
parseCommandLine( int argc, char ** argv )
{
    for( ;; )
    {
        static const struct option long_options[] = {
            { "announce",      required_argument, NULL, 'a' },
            { "create-from",   required_argument, NULL, 'c' },
            { "download",      required_argument, NULL, 'd' },
            { "finish",        required_argument, NULL, 'f' },
            { "config-dir",    required_argument, NULL, 'g' },
            { "help",          no_argument,       NULL, 'h' },
            { "info",          no_argument,       NULL, 'i' },
            { "comment",       required_argument, NULL, 'm' },
            { "nat-traversal", no_argument,       NULL, 'n' },
            { "output-dir",    required_argument, NULL, 'o' },
            { "port",          required_argument, NULL, 'p' },
            { "private",       no_argument,       NULL, 'r' },
            { "scrape",        no_argument,       NULL, 's' },
            { "upload",        required_argument, NULL, 'u' },
            { "verbose",       required_argument, NULL, 'v' },
            { "version",       no_argument,       NULL, 'V' },
            { "recheck",       no_argument,       NULL, 'y' },
            { 0, 0, 0, 0} };
        int optind = 0;
        int c = getopt_long( argc, argv,
                             "a:c:d:f:g:him:no:p:rsu:v:Vy",
                             long_options, &optind );
        if( c < 0 )
        {
            break;
        }
        switch( c )
        {
            case 'a': announce = optarg; break;
            case 'c': sourceFile = optarg; break;
            case 'd': downloadLimit = atoi( optarg ); break;
            case 'f': finishCall = optarg; break;
            case 'g': configdir = strdup( optarg ); break;
            case 'h': showHelp = 1; break;
            case 'i': showInfo = 1; break;
            case 'm': comment = optarg; break;
            case 'n': natTraversal = 1; break;
            case 'o': savePath = optarg;
            case 'p': bindPort = atoi( optarg ); break;
            case 'r': isPrivate = 1; break;
            case 's': showScrape = 1; break;
            case 'u': uploadLimit = atoi( optarg ); break;
            case 'v': verboseLevel = atoi( optarg ); break;
            case 'V': showVersion = 1; break;
            case 'y': recheckData = 1; break;
            default: return 1;
        }
    }

    if( showHelp || showVersion )
        return 0;

    if( optind >= argc )
        return 1;

    torrentPath = argv[optind];
    return 0;
}

static void sigHandler( int signal )
{
    switch( signal )
    {
        case SIGINT:
            gotsig = 1;
            break;
            
        case SIGHUP:
            manualUpdate = 1;
            break;

        default:
            break;
    }
}
