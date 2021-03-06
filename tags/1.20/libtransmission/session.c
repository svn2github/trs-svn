/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#include <assert.h>
#include <stdlib.h>
#include <string.h> /* memcpy */

#include <signal.h>
#include <sys/types.h> /* stat */
#include <sys/stat.h> /* stat */
#include <unistd.h> /* stat */
#include <dirent.h> /* opendir */

#include "transmission.h"
#include "blocklist.h"
#include "fdlimit.h"
#include "list.h"
#include "metainfo.h" /* tr_metainfoFree */
#include "net.h"
#include "peer-mgr.h"
#include "platform.h" /* tr_lock */
#include "port-forwarding.h"
#include "ratecontrol.h"
#include "stats.h"
#include "torrent.h"
#include "tracker.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
uint8_t*
tr_peerIdNew( void )
{
    int i;
    int val;
    int total = 0;
    uint8_t * buf = tr_new( uint8_t, 21 );
    const char * pool = "0123456789abcdefghijklmnopqrstuvwxyz";
    const int base = 36;

    memcpy( buf, PEERID_PREFIX, 8 );

    for( i=8; i<19; ++i ) {
        val = tr_rand( base );
        total += val;
        buf[i] = pool[val];
    }

    val = total % base ? base - (total % base) : 0;
    total += val;
    buf[19] = pool[val];
    buf[20] = '\0';

    return buf;
}

const uint8_t*
tr_getPeerId( void )
{
    static uint8_t * id = NULL;
    if( id == NULL )
        id = tr_peerIdNew( );
    return id;
}

/***
****
***/

tr_encryption_mode
tr_getEncryptionMode( tr_session * session )
{
    assert( session != NULL );

    return session->encryptionMode;
}

void
tr_setEncryptionMode( tr_session * session, tr_encryption_mode mode )
{
    assert( session != NULL );
    assert( mode==TR_ENCRYPTION_PREFERRED
         || mode==TR_ENCRYPTION_REQUIRED
         || mode==TR_PLAINTEXT_PREFERRED );

    session->encryptionMode = mode;
}

/***
****
***/

static void metainfoLookupRescan( tr_handle * h );

tr_handle *
tr_initFull( const char * configDir,
             const char * tag,
             int          isPexEnabled,
             int          isPortForwardingEnabled,
             int          publicPort,
             int          encryptionMode,
             int          isUploadLimitEnabled,
             int          uploadLimit,
             int          isDownloadLimitEnabled,
             int          downloadLimit,
             int          globalPeerLimit,
             int          messageLevel,
             int          isMessageQueueingEnabled,
             int          isBlocklistEnabled,
             int          peerSocketTOS )
{
    tr_handle * h;
    char filename[MAX_PATH_LENGTH];

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    if( configDir == NULL )
        configDir = tr_getDefaultConfigDir( );

    tr_msgInit( );
    tr_setMessageLevel( messageLevel );
    tr_setMessageQueuing( isMessageQueueingEnabled );

    h = tr_new0( tr_handle, 1 );
    h->lock = tr_lockNew( );
    h->isPexEnabled = isPexEnabled ? 1 : 0;
    h->encryptionMode = encryptionMode;
    h->peerSocketTOS = peerSocketTOS;

    tr_setConfigDir( h, configDir );

    tr_netInit(); /* must go before tr_eventInit */

    tr_eventInit( h );
    while( !h->events )
        tr_wait( 50 );

    h->tag = tr_strdup( tag );
    h->peerMgr = tr_peerMgrNew( h );

    /* Initialize rate and file descripts controls */

    h->upload = tr_rcInit();
    tr_rcSetLimit( h->upload, uploadLimit );
    h->useUploadLimit = isUploadLimitEnabled;

    h->download = tr_rcInit();
    tr_rcSetLimit( h->download, downloadLimit );
    h->useDownloadLimit = isDownloadLimitEnabled;

    tr_fdInit( globalPeerLimit );
    h->shared = tr_sharedInit( h, isPortForwardingEnabled, publicPort );
    h->isPortSet = publicPort >= 0;

    /* first %s is the application name
       second %s is the version number */
    tr_inf( _( "%s %s started" ), TR_NAME, LONG_VERSION_STRING );

    /* initialize the blocklist */
    tr_buildPath( filename, sizeof( filename ), h->configDir, "blocklists", NULL );
    tr_mkdirp( filename, 0777 );
    tr_buildPath( filename, sizeof( filename ), h->configDir, "blocklists", "level1.bin", NULL );
    h->blocklist = _tr_blocklistNew( filename, isBlocklistEnabled );

    tr_statsInit( h );

    h->web = tr_webInit( h );

    metainfoLookupRescan( h );

    return h;
}

tr_handle *
tr_init( const char * configDir,
         const char * tag )
{
    return tr_initFull( configDir,
                        tag,
                        TR_DEFAULT_PEX_ENABLED,
                        TR_DEFAULT_PORT_FORWARDING_ENABLED,
                        -1, /* public port */
                        TR_ENCRYPTION_PREFERRED, /* encryption mode */
                        FALSE, /* use upload speed limit? */ 
                        -1, /* upload speed limit */
                        FALSE, /* use download speed limit? */
                        -1, /* download speed limit */
                        TR_DEFAULT_GLOBAL_PEER_LIMIT,
                        TR_MSG_INF, /* message level */
                        FALSE, /* is message queueing enabled? */
                        FALSE, /* is the blocklist enabled? */
                        TR_DEFAULT_PEER_SOCKET_TOS );
}

/***
****
***/

void
tr_globalLock( struct tr_handle * handle )
{
    tr_lockLock( handle->lock );
}

void
tr_globalUnlock( struct tr_handle * handle )
{
    tr_lockUnlock( handle->lock );
}

int
tr_globalIsLocked( const struct tr_handle * handle )
{
    return handle && tr_lockHave( handle->lock );
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * 
 **********************************************************************/

struct bind_port_data
{
    tr_handle * handle;
    int port;
};

static void
tr_setBindPortImpl( void * vdata )
{
    struct bind_port_data * data = vdata;
    tr_handle * handle = data->handle;
    const int port = data->port;

    handle->isPortSet = 1;
    tr_sharedSetPort( handle->shared, port );

    tr_free( data );
}

void
tr_setBindPort( tr_handle * handle, int port )
{
    struct bind_port_data * data = tr_new( struct bind_port_data, 1 );
    data->handle = handle;
    data->port = port;
    tr_runInEventThread( handle, tr_setBindPortImpl, data );
}

int
tr_getPublicPort( const tr_handle * h )
{
    assert( h != NULL );
    return tr_sharedGetPublicPort( h->shared );
}

void tr_natTraversalEnable( tr_handle * h, int enable )
{
    tr_globalLock( h );
    tr_sharedTraversalEnable( h->shared, enable );
    tr_globalUnlock( h );
}

const tr_handle_status *
tr_handleStatus( tr_handle * h )
{
    tr_handle_status * s;

    h->statCur = ( h->statCur + 1 ) % 2;
    s = &h->stats[h->statCur];

    tr_globalLock( h );

    s->natTraversalStatus = tr_sharedTraversalStatus( h->shared );
    s->publicPort = tr_sharedGetPublicPort( h->shared );

    tr_globalUnlock( h );

    return s;
}

/***
****
***/

void
tr_setUseGlobalSpeedLimit( tr_handle  * h,
                           int          up_or_down,
                           int          use_flag )
{
    if( up_or_down == TR_UP )
        h->useUploadLimit = use_flag ? 1 : 0;
    else
        h->useDownloadLimit = use_flag ? 1 : 0;
}

void
tr_setGlobalSpeedLimit( tr_handle  * h,
                        int          up_or_down,
                        int          KiB_sec )
{
    if( up_or_down == TR_DOWN )
        tr_rcSetLimit( h->download, KiB_sec );
    else
        tr_rcSetLimit( h->upload, KiB_sec );
}

void
tr_getGlobalSpeedLimit( tr_handle  * h,
                        int          up_or_down,
                        int        * setme_enabled,
                        int          * setme_KiBsec )
{
    if( setme_enabled != NULL )
       *setme_enabled = up_or_down==TR_UP ? h->useUploadLimit
                                          : h->useDownloadLimit;
    if( setme_KiBsec != NULL )
       *setme_KiBsec = tr_rcGetLimit( up_or_down==TR_UP ? h->upload
                                                        : h->download );
}


void
tr_setGlobalPeerLimit( tr_handle * handle UNUSED,
                       uint16_t    maxGlobalPeers )
{
    tr_fdSetPeerLimit( maxGlobalPeers );
}

uint16_t
tr_getGlobalPeerLimit( const tr_handle * handle UNUSED )
{
    return tr_fdGetPeerLimit( );
}

void
tr_torrentRates( tr_handle * h, float * toClient, float * toPeer )
{
    if( h )
    {
        tr_globalLock( h );

        if( toClient )
            *toClient = tr_rcRate( h->download );
        if( toPeer )
            *toPeer = tr_rcRate( h->upload );

        tr_globalUnlock( h );
    }
}

int
tr_torrentCount( const tr_handle * h )
{
    return h->torrentCount;
}

static void
tr_closeAllConnections( void * vh )
{
    tr_handle * h = vh;
    tr_torrent * t;

    tr_sharedShuttingDown( h->shared );
    tr_trackerShuttingDown( h );

    for( t=h->torrentList; t!=NULL; ) {
        tr_torrent * tmp = t;
        t = t->next;
        tr_torrentClose( tmp );
    }

    tr_peerMgrFree( h->peerMgr );

    tr_rcClose( h->upload );
    tr_rcClose( h->download );
    
    h->isClosed = TRUE;
}

static int
deadlineReached( const uint64_t deadline )
{
    return tr_date( ) >= deadline;
}

#define SHUTDOWN_MAX_SECONDS 30

void
tr_close( tr_handle * h )
{
    int i;
    const int maxwait_msec = SHUTDOWN_MAX_SECONDS * 1000;
    const uint64_t deadline = tr_date( ) + maxwait_msec;

    tr_deepLog( __FILE__, __LINE__, NULL, "shutting down transmission session %p", h );
    tr_statsClose( h );

    tr_runInEventThread( h, tr_closeAllConnections, h );
    while( !h->isClosed && !deadlineReached( deadline ) )
        tr_wait( 100 );

    _tr_blocklistFree( h->blocklist );
    h->blocklist = NULL;
    tr_webClose( &h->web );

    tr_eventClose( h );
    while( h->events && !deadlineReached( deadline ) )
        tr_wait( 100 );

    tr_fdClose( );
    tr_lockFree( h->lock );
    for( i=0; i<h->metainfoLookupCount; ++i )
        tr_free( h->metainfoLookup[i].filename );
    tr_free( h->metainfoLookup );
    tr_free( h->tag );
    tr_free( h->configDir );
    tr_free( h->resumeDir );
    tr_free( h->torrentDir );
    free( h );
}

tr_torrent **
tr_loadTorrents ( tr_handle   * h,
                  tr_ctor     * ctor,
                  int         * setmeCount )
{
    int i, n = 0;
    struct stat sb;
    DIR * odir = NULL;
    const char * dirname = tr_getTorrentDir( h );
    tr_torrent ** torrents;
    tr_list *l=NULL, *list=NULL;

    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */

    if( !stat( dirname, &sb )
        && S_ISDIR( sb.st_mode )
        && (( odir = opendir ( dirname ) )) )
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_torrent * tor;
                char filename[MAX_PATH_LENGTH];
                tr_buildPath( filename, sizeof(filename), dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, filename );
                tor = tr_torrentNew( h, ctor, NULL );
                if( tor ) {
                    tr_list_append( &list, tor );
                    n++;
                }
            }
        }
        closedir( odir );
    }

    torrents = tr_new( tr_torrent*, n );
    for( i=0, l=list; l!=NULL; l=l->next )
        torrents[i++] = (tr_torrent*) l->data;
    assert( i==n );

    tr_list_free( &list, NULL );

    if( n )
        tr_inf( _( "Loaded %d torrents" ), n );

    *setmeCount = n;
    return torrents;
}

/***
****
***/

void
tr_setPexEnabled( tr_handle * handle, int isPexEnabled )
{
    handle->isPexEnabled = isPexEnabled ? 1 : 0;
}

int
tr_isPexEnabled( const tr_handle * handle )
{
    return handle->isPexEnabled;
}

/***
****
***/

int
tr_blocklistGetRuleCount( tr_handle * handle )
{
    return _tr_blocklistGetRuleCount( handle->blocklist );
}

int
tr_blocklistIsEnabled( const tr_handle * handle )
{
    return _tr_blocklistIsEnabled( handle->blocklist );
}

void
tr_blocklistSetEnabled( tr_handle * handle, int isEnabled )
{
    _tr_blocklistSetEnabled( handle->blocklist, isEnabled );
}

int
tr_blocklistExists( const tr_handle * handle )
{
    return _tr_blocklistExists( handle->blocklist );
}

int
tr_blocklistSetContent( tr_handle  * handle, const char * filename )
{
    return _tr_blocklistSetContent( handle->blocklist, filename );
}

int
tr_blocklistHasAddress( tr_handle * handle, const struct in_addr * addr )
{
    return _tr_blocklistHasAddress( handle->blocklist, addr );
}

/***
****
***/

static int
compareLookupEntries( const void * va, const void * vb )
{
    const struct tr_metainfo_lookup * a = va;
    const struct tr_metainfo_lookup * b = vb;
    return strcmp( a->hashString, b->hashString );
}

static void
metainfoLookupResort( tr_handle * h )
{
    qsort( h->metainfoLookup, 
           h->metainfoLookupCount,
           sizeof( struct tr_metainfo_lookup ),
           compareLookupEntries );
}

static int
compareHashStringToLookupEntry( const void * va, const void * vb )
{
    const char * a = va;
    const struct tr_metainfo_lookup * b = vb;
    return strcmp( a, b->hashString );
}

const char*
tr_sessionFindTorrentFile( const tr_handle  * h,
                           const char       * hashStr )
{
    struct tr_metainfo_lookup * l = bsearch( hashStr,
                                             h->metainfoLookup,
                                             h->metainfoLookupCount,
                                             sizeof( struct tr_metainfo_lookup ),
                                             compareHashStringToLookupEntry );
    return l ? l->filename : NULL;
}

static void
metainfoLookupRescan( tr_handle * h )
{
    int i;
    int n;
    struct stat sb;
    const char * dirname = tr_getTorrentDir( h );
    DIR * odir = NULL;
    tr_ctor * ctor = NULL;
    tr_list * list = NULL;

    /* walk through the directory and find the mappings */
    ctor = tr_ctorNew( h );
    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */
    if( !stat( dirname, &sb ) && S_ISDIR( sb.st_mode ) && (( odir = opendir( dirname ))))
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_info inf;
                char filename[MAX_PATH_LENGTH];
                tr_buildPath( filename, sizeof(filename), dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, filename );
                if( !tr_torrentParse( h, ctor, &inf ) )
                {
                    tr_list_append( &list, tr_strdup( inf.hashString ) );
                    tr_list_append( &list, tr_strdup( filename ) );
                    tr_metainfoFree( &inf );
                }
            }
        }
        closedir( odir );
    }
    tr_ctorFree( ctor );

    n = tr_list_size( list ) / 2;
    h->metainfoLookup = tr_new0( struct tr_metainfo_lookup, n );
    h->metainfoLookupCount = n;
    for( i=0; i<n; ++i )
    {
        char * hashString = tr_list_pop_front( &list );
        char * filename = tr_list_pop_front( &list );

        memcpy( h->metainfoLookup[i].hashString, hashString, 2*SHA_DIGEST_LENGTH+1 );
        tr_free( hashString );
        h->metainfoLookup[i].filename = filename;
    }

    metainfoLookupResort( h );
    tr_dbg( "Found %d torrents in \"%s\"", n, dirname );
}

void
tr_sessionSetTorrentFile( tr_handle    * h,
                          const char   * hashString,
                          const char   * filename )
{
    struct tr_metainfo_lookup * l = bsearch( hashString,
                                             h->metainfoLookup,
                                             h->metainfoLookupCount,
                                             sizeof( struct tr_metainfo_lookup ),
                                             compareHashStringToLookupEntry );
    if( l != NULL )
    {
        if( l->filename != filename )
        {
            tr_free( l->filename );
            l->filename = tr_strdup( filename );
        }
    }
    else
    {
        const int n = h->metainfoLookupCount++;
        struct tr_metainfo_lookup * node;
        h->metainfoLookup = tr_renew( struct tr_metainfo_lookup,
                                      h->metainfoLookup,
                                      h->metainfoLookupCount );
        node = h->metainfoLookup + n;
        memcpy( node->hashString, hashString, 2*SHA_DIGEST_LENGTH+1 );
        node->filename = tr_strdup( filename );
        metainfoLookupResort( h );
    }
}
