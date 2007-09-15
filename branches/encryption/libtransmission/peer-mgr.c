/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
#include <string.h> /* memcpy, memcmp */
#include <stdlib.h> /* qsort */
#include <stdio.h> /* printf */

#include "transmission.h"
#include "clients.h"
#include "completion.h"
#include "handshake.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
#include "ptrarray.h"
#include "trevent.h"
#include "utils.h"

/* how frequently to change which peers are choked */
#define RECHOKE_PERIOD_SECONDS (15 * 1000)

#define REFILL_PERIOD_MSEC 1000

/* how many peers to unchoke per-torrent. */
/* FIXME: make this user-configurable? */
#define NUM_UNCHOKED_PEERS_PER_TORRENT 8

/**
***
**/

struct tr_block
{
    unsigned int have          : 1;
    unsigned int dnd           : 1;
    unsigned int low_priority  : 1;
    unsigned int high_priority : 1;
    uint8_t requestCount;
    uint8_t scarcity;
    uint32_t block;
};

#define MAX_SCARCITY UINT8_MAX
#define MAX_REQ_COUNT UINT8_MAX

static void
incrementReqCount( struct tr_block * block )
{
    assert( block != NULL );

    if( block->requestCount < MAX_REQ_COUNT )
        block->requestCount++;
}

static void
incrementScarcity( struct tr_block * block )
{
    assert( block != NULL );

    if( block->scarcity < MAX_SCARCITY )
        block->scarcity++;
}

static int
compareBlockByIndex( const void * va, const void * vb )
{
    const struct tr_block * a = (const struct tr_block *) va;
    const struct tr_block * b = (const struct tr_block *) vb;
    return tr_compareUint32( a->block, b->block );
}

static int
compareBlockByInterest( const void * va, const void * vb )
{
    const struct tr_block * a = (const struct tr_block *) va;
    const struct tr_block * b = (const struct tr_block *) vb;
    int i;

    if( a->dnd != b->dnd )
        return a->dnd ? 1 : -1;

    if( a->have != b->have )
        return a->have ? 1 : -1;

    if(( i = tr_compareUint8( a->requestCount, b->requestCount )))
        return i;

    if( a->high_priority != b->high_priority )
        return a->high_priority ? -1 : 1;

    if( a->low_priority != b->low_priority )
        return a->low_priority ? 1 : -1;

    if(( i = tr_compareUint16( a->scarcity, b->scarcity )))
        return i;

    if(( i = tr_compareUint32( a->block, b->block )))
        return i;

    return 0;
}

/**
***
**/

typedef struct
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    tr_ptrArray * peers; /* tr_peer */
    tr_timer * chokeTimer;
    tr_timer * refillTimer;
    tr_torrent * tor;

    struct tr_block * blocks;
    uint32_t blockCount;

    unsigned int isRunning : 1;

    struct tr_peerMgr * manager;
}
Torrent;

struct tr_peerMgr
{
    tr_handle * handle;
    tr_ptrArray * torrents; /* Torrent */
    int connectionCount;
    tr_ptrArray * handshakes; /* in-process */
};

/**
***
**/

static int
torrentCompare( const void * va, const void * vb )
{
    const Torrent * a = (const Torrent*) va;
    const Torrent * b = (const Torrent*) vb;
    return memcmp( a->hash, b->hash, SHA_DIGEST_LENGTH );
}

static int
torrentCompareToHash( const void * va, const void * vb )
{
    const Torrent * a = (const Torrent*) va;
    const uint8_t * b_hash = (const uint8_t*) vb;
    return memcmp( a->hash, b_hash, SHA_DIGEST_LENGTH );
}

static Torrent*
getExistingTorrent( tr_peerMgr * manager, const uint8_t * hash )
{
    return (Torrent*) tr_ptrArrayFindSorted( manager->torrents,
                                             hash,
                                             torrentCompareToHash );
}

static int chokePulse( void * vtorrent );

static int
peerCompare( const void * va, const void * vb )
{
    const tr_peer * a = (const tr_peer *) va;
    const tr_peer * b = (const tr_peer *) vb;
    return memcmp( &a->in_addr, &b->in_addr, sizeof(struct in_addr) );
}

static int
peerCompareToAddr( const void * va, const void * vb )
{
    const tr_peer * a = (const tr_peer *) va;
    const struct in_addr * b = (const struct in_addr *) vb;
    return memcmp( &a->in_addr, b, sizeof(struct in_addr) );
}

static tr_peer*
getExistingPeer( Torrent * torrent, const struct in_addr * in_addr )
{
    assert( torrent != NULL );
    assert( torrent->peers != NULL );
    assert( in_addr != NULL );

    return (tr_peer*) tr_ptrArrayFindSorted( torrent->peers,
                                             in_addr,
                                             peerCompareToAddr );
}

static tr_peer*
getPeer( Torrent * torrent, const struct in_addr * in_addr )
{
    tr_peer * peer = getExistingPeer( torrent, in_addr );

    if( peer == NULL )
    {
        peer = tr_new0( tr_peer, 1 );
        memcpy( &peer->in_addr, in_addr, sizeof(struct in_addr) );
        tr_ptrArrayInsertSorted( torrent->peers, peer, peerCompare );
fprintf( stderr, "getPeer: torrent %p now has %d peers\n", torrent, tr_ptrArraySize(torrent->peers) );
    }
    return peer;
}

static void
disconnectPeer( tr_peer * peer )
{
    assert( peer != NULL );

    tr_peerIoFree( peer->io );
    peer->io = NULL;

    if( peer->msgs != NULL )
    {
fprintf( stderr, "PUB unsub peer %p from msgs %p\n", peer, peer->msgs );
        tr_peerMsgsUnsubscribe( peer->msgs, peer->msgsTag );
        tr_peerMsgsFree( peer->msgs );
        peer->msgs = NULL;
    }

    tr_bitfieldFree( peer->have );
    peer->have = NULL;

    tr_bitfieldFree( peer->blame );
    peer->blame = NULL;

    tr_bitfieldFree( peer->banned );
    peer->banned = NULL;
}

static void
freePeer( tr_peer * peer )
{
    disconnectPeer( peer );
    tr_free( peer->client );
    tr_free( peer );
}

static void
freeTorrent( tr_peerMgr * manager, Torrent * t )
{
    int i, size;
    tr_peer ** peers;
    uint8_t hash[SHA_DIGEST_LENGTH];

fprintf( stderr, "timer freeTorrent %p\n", t );

    assert( manager != NULL );
    assert( t != NULL );
    assert( t->peers != NULL );
    assert( getExistingTorrent( manager, t->hash ) != NULL );

    memcpy( hash, t->hash, SHA_DIGEST_LENGTH );

    tr_timerFree( &t->chokeTimer );
    tr_timerFree( &t->refillTimer );

    peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &size );
    for( i=0; i<size; ++i )
        freePeer( peers[i] );

    tr_ptrArrayFree( t->peers );
    tr_ptrArrayRemoveSorted( manager->torrents, t, torrentCompare );
    tr_free( t->blocks );
    tr_free( t );

    assert( getExistingTorrent( manager, hash ) == NULL );
}

/**
***
**/

tr_peerMgr*
tr_peerMgrNew( tr_handle * handle )
{
    tr_peerMgr * m = tr_new0( tr_peerMgr, 1 );
    m->handle = handle;
    m->torrents = tr_ptrArrayNew( );
    m->handshakes = tr_ptrArrayNew( );
    return m;
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    int i, n;
fprintf( stderr, "timer peerMgrFree\n" );

    while( !tr_ptrArrayEmpty( manager->torrents ) )
        freeTorrent( manager, (Torrent*)tr_ptrArrayNth( manager->torrents, 0) );
    tr_ptrArrayFree( manager->torrents );

    for( i=0, n=tr_ptrArraySize(manager->handshakes); i<n; ++i )
        tr_handshakeAbort( (tr_handshake*) tr_ptrArrayNth( manager->handshakes, i) );
    tr_ptrArrayFree( manager->handshakes );

    tr_free( manager );
}

/**
***
**/

static tr_peer**
getConnectedPeers( Torrent * t, int * setmeCount )
{
    int i, peerCount, connectionCount;
    tr_peer **peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    tr_peer **ret = tr_new( tr_peer*, peerCount );

    for( i=connectionCount=0; i<peerCount; ++i )
        if( peers[i]->msgs != NULL )
            ret[connectionCount++] = peers[i];

    *setmeCount = connectionCount;
    return ret;
}

static void
populateBlockArray( Torrent * t )
{
    uint32_t i;
    struct tr_block * b;
    const tr_torrent * tor = t->tor;

    t->blockCount = tor->blockCount;
    t->blocks = b = tr_new( struct tr_block, t->blockCount );

    for( i=0; i<t->blockCount; ++i, ++b )
    {
        const int index = tr_torBlockPiece( tor, i );
        b->have = tr_cpBlockIsComplete( tor->completion, i ) ? 1 : 0;
        b->dnd = tor->info.pieces[index].dnd ? 1 : 0;
        b->low_priority = tor->info.pieces[index].priority == TR_PRI_LOW;
        b->high_priority = tor->info.pieces[index].priority == TR_PRI_HIGH;
        b->requestCount = 0;
        b->scarcity = 0;
        b->block = i;
    }
}

static int
refillPulse( void * vtorrent )
{
    uint32_t i;
    int size;
    Torrent * t = (Torrent *) vtorrent;
    tr_peer ** peers;
    const int isSeeding = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
    const int wantToRefill = t->isRunning && !isSeeding;

    if( !wantToRefill && t->blocks!=NULL ) /* torrent has stopped or switched to seeding */
    {
        tr_free( t->blocks );
        t->blocks = NULL;
        t->blockCount = 0;
    }
    else if( wantToRefill && t->blocks==NULL ) /* torrent has started or switched to leeching */
    {
        populateBlockArray( t );
    }

    peers = getConnectedPeers( t, &size );
    if( wantToRefill && size>0 )
    {
        /* sort the blocks by interest */
        fprintf( stderr, "sorting [%s] blocks by interest...", t->tor->info.name );
        qsort( t->blocks, t->blockCount, sizeof(struct tr_block), compareBlockByInterest );
        fprintf( stderr, "done\n" );

        /* walk through all the most interesting blocks */
        for( i=0; i<t->blockCount; ++i )
        {
            const uint32_t b = t->blocks[i].block;
            const uint32_t index = tr_torBlockPiece( t->tor, b );
            const uint32_t begin = ( b * t->tor->blockSize )-( index * t->tor->info.pieceSize );
            const uint32_t length = tr_torBlockCountBytes( t->tor, (int)b );
            int j;

            if( t->blocks[i].have || t->blocks[i].dnd )
                continue;

            if( !size ) /* all peers full */
                break;

            /* find a peer who can ask for this block */
            for( j=0; j<size; )
            {
                const int val = tr_peerMsgsAddRequest( peers[j]->msgs, index, begin, length );
                switch( val )
                {
                    case TR_ADDREQ_FULL: 
                    case TR_ADDREQ_CLIENT_CHOKED: 
                        peers[j] = peers[--size];
                        break;

                    case TR_ADDREQ_MISSING: 
                        ++j;
                        break;

                    case TR_ADDREQ_OK:
                        fprintf( stderr, "peer %p took the request for block %d\n", peers[j]->msgs, b );
                        incrementReqCount( &t->blocks[i] );
                        j = size;
                        break;
                }
            }
        }

        /* put the blocks back the way we found them */
        qsort( t->blocks, t->blockCount, sizeof(struct tr_block), compareBlockByIndex );
    }

    /* cleanup */
    tr_free( peers );

    return TRUE;
}

static void
broadcastClientHave( Torrent * t, uint32_t index )
{
    int i, size;
    tr_peer ** peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        tr_peerMsgsHave( peers[i]->msgs, index );
    tr_free( peers );
}

static void
broadcastGotBlock( Torrent * t, uint32_t index, uint32_t offset, uint32_t length )
{
    int i, size;
    tr_peer ** peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        tr_peerMsgsCancel( peers[i]->msgs, index, offset, length );
    tr_free( peers );
}

static void
msgsCallbackFunc( void * source UNUSED, void * vevent, void * vt )
{
    Torrent * t = (Torrent *) vt;
    const tr_peermsgs_event * e = (const tr_peermsgs_event *) vevent;

    switch( e->eventType )
    {
        case TR_PEERMSG_PEER_BITFIELD: {
            if( t->blocks!=NULL ) {
                int i;
                for( i=0; i<t->tor->info.pieceCount; ++i ) {
                    const uint32_t begin = tr_torPieceFirstBlock( t->tor, i );
                    const uint32_t end = begin + tr_torPieceCountBlocks( t->tor, i );
                    uint32_t j;
                    for( j=begin; t->blocks!=NULL && j<end; ++j ) {
                        assert( t->blocks[j].block == j );
                        incrementScarcity( &t->blocks[j] );
                    }
                }
            }
            break;
        }

        case TR_PEERMSG_CLIENT_HAVE:
            broadcastClientHave( t, e->pieceIndex );
            break;

        case TR_PEERMSG_PEER_HAVE: {
            if( t->blocks != NULL ) {
                uint32_t i;
                const uint32_t begin = tr_torPieceFirstBlock( t->tor, e->pieceIndex );
                const uint32_t end = begin + tr_torPieceCountBlocks( t->tor, (int)e->pieceIndex );
                for( i=begin; i<end; ++i ) {
                    assert( t->blocks[i].block == i );
                    incrementScarcity( &t->blocks[i] );
                }
            }
            break;
        }

        case TR_PEERMSG_CLIENT_BLOCK: {
            if( t->blocks != NULL ) {
                const uint32_t i = _tr_block( t->tor, e->pieceIndex, e->offset );
                assert( t->blocks[i].block == i );
                t->blocks[i].have = 1;
            }
            broadcastGotBlock( t, e->pieceIndex, e->offset, e->length );
            break;
        }

        case TR_PEERMSG_GOT_PEX:
            /* FIXME */
            break;

        case TR_PEERMSG_GOT_ERROR:
            /* FIXME */
            break;

        default:
            assert(0);
    }
}

static void
myHandshakeDoneCB( tr_handshake    * handshake,
                   tr_peerIo       * io,
                   int               isConnected,
                   const uint8_t   * peer_id,
                   void            * vmanager )
{
    int ok = isConnected;
    uint16_t port;
    const struct in_addr * in_addr;
    tr_peerMgr * manager = (tr_peerMgr*) vmanager;
    const uint8_t * hash = NULL;
    Torrent * t;
    tr_handshake * ours;

    assert( io != NULL );

    ours = tr_ptrArrayRemoveSorted( manager->handshakes,
                                    handshake,
                                    tr_comparePointers );
    assert( handshake == ours );

    in_addr = tr_peerIoGetAddress( io, &port );

    if( !tr_peerIoHasTorrentHash( io ) ) /* incoming connection gone wrong? */
    {
        tr_peerIoFree( io );
        --manager->connectionCount;
        return;
    }

    hash = tr_peerIoGetTorrentHash( io );
    t = getExistingTorrent( manager, hash );
    if( !t || !t->isRunning )
    {
        tr_peerIoFree( io );
        --manager->connectionCount;
        return;
    }

    fprintf( stderr, "peer-mgr: torrent [%s] finished a handshake; isConnected is %d\n", t->tor->info.name, isConnected );

    /* if we couldn't connect or were snubbed,
     * the peer's probably not worth remembering. */
    if( !ok ) {
        tr_peer * peer = getExistingPeer( t, in_addr );
        fprintf( stderr, "peer-mgr: torrent [%s] got a bad one, and you know what? fuck them.\n", t->tor->info.name );
        tr_peerIoFree( io );
        --manager->connectionCount;
        if( peer ) {
            tr_ptrArrayRemoveSorted( t->peers, peer, peerCompare );
            freePeer( peer );
        }
        return;
    }

    if( 1 ) {
        tr_peer * peer = getPeer( t, in_addr );
        if( peer->msgs != NULL ) { /* we alerady have this peer */
            tr_peerIoFree( io );
            --manager->connectionCount;
        } else {
            peer->port = port;
            peer->io = io;
            peer->msgs = tr_peerMsgsNew( t->tor, peer );
            peer->client = peer_id ? tr_clientForId( peer_id ) : NULL;
            fprintf( stderr, "PUB sub peer %p to msgs %p\n", peer, peer->msgs );
            peer->msgsTag = tr_peerMsgsSubscribe( peer->msgs, msgsCallbackFunc, t );
        }
    }
}

static void
initiateHandshake( tr_peerMgr * manager, tr_peerIo * io )
{
    tr_handshake * handshake = tr_handshakeNew( io,
                                                HANDSHAKE_ENCRYPTION_PREFERRED,
                                                myHandshakeDoneCB,
                                                manager );
    ++manager->connectionCount;

    tr_ptrArrayInsertSorted( manager->handshakes, handshake, tr_comparePointers );
}

void
tr_peerMgrAddIncoming( tr_peerMgr      * manager,
                       struct in_addr  * addr,
                       int               socket )
{
    tr_peerIo * io = tr_peerIoNewIncoming( manager->handle, addr, socket );
    initiateHandshake( manager, io );
}

static void
maybeConnect( tr_peerMgr * manager, Torrent * t, tr_peer * peer )
{
    tr_peerIo * io;

    assert( manager != NULL );
    assert( t != NULL );
    assert( peer != NULL );

    if( peer->io != NULL ) { /* already connected */
        fprintf( stderr, "not connecting because we already have an IO for that address\n" );
        return;
    }
    if( !t->isRunning ) { /* torrent's not running */
        fprintf( stderr, "OUTGOING connection not being made because t [%s] is not running\n", t->tor->info.name );
        return;
    }

    io = tr_peerIoNewOutgoing( manager->handle,
                               &peer->in_addr,
                               peer->port,
                               t->hash );
    initiateHandshake( manager, io );
}

void
tr_peerMgrAddPex( tr_peerMgr     * manager,
                  const uint8_t  * torrentHash,
                  int              from,
                  const tr_pex   * pex,
                  int              pexCount )
{
    int i;
    const tr_pex * walk = pex;
    Torrent * t = getExistingTorrent( manager, torrentHash );
    for( i=0; i<pexCount; ++i )
    {
        tr_peer * peer = getPeer( t, &walk->in_addr );
        peer->port = walk->port;
        peer->from = from;
        maybeConnect( manager, t, peer );
    }
}

void
tr_peerMgrAddPeers( tr_peerMgr    * manager,
                    const uint8_t * torrentHash,
                    int             from,
                    const uint8_t * peerCompact,
                    int             peerCount )
{
    int i;
    const uint8_t * walk = peerCompact;
    Torrent * t = getExistingTorrent( manager, torrentHash );
    for( i=0; t!=NULL && i<peerCount; ++i )
    {
        tr_peer * peer;
        struct in_addr addr;
        uint16_t port;
        memcpy( &addr, walk, 4 ); walk += 4;
        memcpy( &port, walk, 2 ); walk += 2;
        peer = getPeer( t, &addr );
        peer->port = port;
        peer->from = from;
        maybeConnect( manager, t, peer );
    }
}

/**
***
**/

int
tr_peerMgrIsAcceptingConnections( const tr_peerMgr * manager UNUSED )
{
    return TRUE; /* manager->connectionCount < MAX_CONNECTED_PEERS; */
}

void
tr_peerMgrSetBlame( tr_peerMgr     * manager UNUSED,
                    const uint8_t  * torrentHash UNUSED,
                    int              pieceIndex UNUSED,
                    int              success UNUSED )
{
    fprintf( stderr, "FIXME: tr_peerMgrSetBlame\n" );
}

int
tr_pexCompare( const void * va, const void * vb )
{
    const tr_pex * a = (const tr_pex *) va;
    const tr_pex * b = (const tr_pex *) vb;
    int i = memcmp( &a->in_addr, &b->in_addr, sizeof(struct in_addr) );
    if( i ) return i;
    if( a->port < b->port ) return -1;
    if( a->port > b->port ) return 1;
    return 0;
}

int tr_pexCompare( const void * a, const void * b );


int
tr_peerMgrGetPeers( tr_peerMgr      * manager,
                    const uint8_t   * torrentHash,
                    tr_pex         ** setme_pex )
{
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    int i, peerCount;
    const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    tr_pex * pex = tr_new( tr_pex, peerCount );
    tr_pex * walk = pex;

    for( i=0; i<peerCount; ++i, ++walk )
    {
        walk->in_addr = peers[i]->in_addr;
        walk->port = peers[i]->port;
        walk->flags = '\0'; /* FIXME */
    }

    assert( ( walk - pex ) == peerCount );
    qsort( pex, peerCount, sizeof(tr_pex), tr_pexCompare );
    *setme_pex = pex;
    return peerCount;
}

void
tr_peerMgrStartTorrent( tr_peerMgr     * manager,
                        const uint8_t  * torrentHash )
{
    int i, peerCount;
    Torrent * t = getExistingTorrent( manager, torrentHash );

    t->isRunning = 1;

    peerCount = tr_ptrArraySize( t->peers );
    for( i=0; i<peerCount; ++i )
        maybeConnect( manager, t, tr_ptrArrayNth( t->peers, i ) );
}

void
tr_peerMgrStopTorrent( tr_peerMgr     * manager,
                       const uint8_t  * torrentHash)
{
    int i, peerCount;
    Torrent * t = getExistingTorrent( manager, torrentHash );

    t->isRunning = 0;

    peerCount = tr_ptrArraySize( t->peers );
    for( i=0; i<peerCount; ++i )
        disconnectPeer( tr_ptrArrayNth( t->peers, i ) );
}

void
tr_peerMgrUpdateCompletion( tr_peerMgr     * manager,
                            const uint8_t  * torrentHash )
{
    uint32_t i;
    Torrent * t = getExistingTorrent( manager, torrentHash );

    for( i=0; t->blocks!=NULL && i<t->blockCount; ++i ) {
        assert( t->blocks[i].block == i );
        t->blocks[i].have = tr_cpBlockIsComplete( t->tor->completion, i ) ? 1 : 0;
    }
}

void
tr_peerMgrAddTorrent( tr_peerMgr * manager,
                      tr_torrent * tor )
{
    Torrent * t;

    assert( tor != NULL );
    assert( getExistingTorrent( manager, tor->info.hash ) == NULL );

    t = tr_new0( Torrent, 1 );
    t->manager = manager;
    t->tor = tor;
    t->peers = tr_ptrArrayNew( );
    t->chokeTimer = tr_timerNew( manager->handle, chokePulse, t, RECHOKE_PERIOD_SECONDS );
    t->refillTimer = tr_timerNew( t->manager->handle, refillPulse, t, REFILL_PERIOD_MSEC );

    memcpy( t->hash, tor->info.hash, SHA_DIGEST_LENGTH );
    tr_ptrArrayInsertSorted( manager->torrents, t, torrentCompare );
}

void
tr_peerMgrRemoveTorrent( tr_peerMgr     * manager,
                         const uint8_t  * torrentHash )
{
    Torrent * t = getExistingTorrent( manager, torrentHash );
    assert( t != NULL );
    tr_peerMgrStopTorrent( manager, torrentHash );
    freeTorrent( manager, t );
}

void
tr_peerMgrTorrentAvailability( const tr_peerMgr * manager,
                               const uint8_t    * torrentHash,
                               int8_t           * tab,
                               int                tabCount )
{
    int i;
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const tr_torrent * tor = t->tor;
    const float interval = tor->info.pieceCount / (float)tabCount;

    memset( tab, 0, tabCount );

    for( i=0; i<tabCount; ++i )
    {
        const int piece = i * interval;

        if( tor == NULL )
            tab[i] = 0;
        else if( tr_cpPieceIsComplete( tor->completion, piece ) )
            tab[i] = -1;
        else {
            int j, peerCount;
            const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
            for( j=0; j<peerCount; ++j )
                if( tr_bitfieldHas( peers[j]->have, i ) )
                    ++tab[i];
        }
    }
}


void
tr_peerMgrTorrentStats( const tr_peerMgr * manager,
                        const uint8_t    * torrentHash,
                        int              * setmePeersTotal,
                        int              * setmePeersConnected,
                        int              * setmePeersSendingToUs,
                        int              * setmePeersGettingFromUs,
                        int              * setmePeersFrom )
{
    int i, size;
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );

    *setmePeersTotal          = size;
    *setmePeersConnected      = 0;
    *setmePeersSendingToUs    = 0;
    *setmePeersGettingFromUs  = 0;

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];

        if( peer->io == NULL ) /* not connected */
            continue;

        ++*setmePeersConnected;

        ++setmePeersFrom[peer->from];

        if( tr_peerIoGetRateToPeer( peer->io ) > 0.01 )
            ++*setmePeersGettingFromUs;

        if( tr_peerIoGetRateToClient( peer->io ) > 0.01 )
            ++*setmePeersSendingToUs;
    }
}

struct tr_peer_stat *
tr_peerMgrPeerStats( const tr_peerMgr  * manager,
                     const uint8_t     * torrentHash,
                     int               * setmeCount UNUSED )
{
    int i, size;
    const Torrent * t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );
    tr_peer_stat * ret;

    ret = tr_new0( tr_peer_stat, size );

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];
        const int live = peer->io != NULL;
        tr_peer_stat * stat = ret + i;

        tr_netNtop( &peer->in_addr, stat->addr, sizeof(stat->addr) );
        stat->port             = peer->port;
        stat->from             = peer->from;
        stat->client           = peer->client;
        stat->progress         = peer->progress;
        stat->isConnected      = live ? 1 : 0;
        stat->isEncrypted      = tr_peerIoIsEncrypted( peer->io ) ? 1 : 0;
        stat->uploadToRate     = tr_peerIoGetRateToPeer( peer->io );
        stat->downloadFromRate = tr_peerIoGetRateToClient( peer->io );
        stat->isDownloading    = stat->uploadToRate > 0.01;
        stat->isUploading      = stat->downloadFromRate > 0.01;
    }

    *setmeCount = size;
    return ret;
}

void
tr_peerMgrDisablePex( tr_peerMgr    * manager,
                      const uint8_t * torrentHash,
                      int             disable)
{
    Torrent * t = getExistingTorrent( manager, torrentHash );
    tr_torrent * tor = t->tor;

    if( ( tor->pexDisabled != disable ) && ! ( TR_FLAG_PRIVATE & tor->info.flags ) )
    {
        int i, size;
        tr_peer ** peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &size );
        for( i=0; i<size; ++i )
            peers[i]->pexEnabled = disable ? 0 : 1;

        tor->pexDisabled = disable;
    }
}

/**
***
**/

typedef struct
{
    tr_peer * peer;
    float rate;
    int randomKey;
    int preferred;
    int doUnchoke;
}
ChokeData;

static int
compareChoke( const void * va, const void * vb )
{
    const ChokeData * a = ( const ChokeData * ) va;
    const ChokeData * b = ( const ChokeData * ) vb;

    if( a->preferred != b->preferred )
        return a->preferred ? -1 : 1;

    if( a->preferred )
    {
        if( a->rate > b->rate ) return -1;
        if( a->rate < b->rate ) return 1;
        return 0;
    }
    else
    {
        return a->randomKey - b->randomKey;
    }
}

static int
clientIsSnubbedBy( const tr_peer * peer )
{
    assert( peer != NULL );

    return peer->peerSentDataAt < (time(NULL) - 30);
}

static void
rechokeLeech( Torrent * t )
{
    int i, size, unchoked=0;
    tr_peer ** peers = getConnectedPeers( t, &size );
    ChokeData * choke = tr_new0( ChokeData, size );

    /* sort the peers by preference and rate */
    for( i=0; i<size; ++i ) {
        tr_peer * peer = peers[i];
        ChokeData * node = &choke[i];
        node->peer = peer;
        node->preferred = peer->peerIsInterested && !clientIsSnubbedBy(peer);
        node->randomKey = tr_rand( INT_MAX );
        node->rate = tr_peerIoGetRateToClient( peer->io );
    }
    qsort( choke, size, sizeof(ChokeData), compareChoke );

    for( i=0; i<size && i<NUM_UNCHOKED_PEERS_PER_TORRENT; ++i ) {
        choke[i].doUnchoke = 1;
        ++unchoked;
    }

    for( ; i<size; ++i ) {
        choke[i].doUnchoke = 1;
        ++unchoked;
        if( choke[i].peer->peerIsInterested )
            break;
    }

    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( choke[i].peer->msgs, !choke[i].doUnchoke );

    /* cleanup */
    tr_free( choke );
    tr_free( peers );
}

static void
rechokeSeed( Torrent * t )
{
    int i, size;
    tr_peer ** peers = getConnectedPeers( t, &size );

    /* FIXME */
    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( peers[i]->msgs, FALSE );

    tr_free( peers );
}

static int
chokePulse( void * vtorrent )
{
    Torrent * t = vtorrent;
    const int done = tr_cpGetStatus( t->tor->completion ) != TR_CP_INCOMPLETE;
    if( done )
        rechokeLeech( vtorrent );
    else
        rechokeSeed( vtorrent );
    return TRUE;
}
