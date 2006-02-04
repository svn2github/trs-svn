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

/***********************************************************************
 * This file handles all outgoing messages
 **********************************************************************/

static uint8_t * messagesPending( tr_peer_t * peer, int * size )
{
    if( peer->outBlockSending || peer->outMessagesPos < 1 )
    {
        return NULL;
    }

    *size = MIN( peer->outMessagesPos, 1024 );

    return peer->outMessages;
}

static void messagesSent( tr_peer_t * peer, int size )
{
    peer->outMessagesPos -= size;
    memmove( peer->outMessages, &peer->outMessages[size],
             peer->outMessagesPos );
}

static uint8_t * blockPending( tr_torrent_t * tor, tr_peer_t * peer,
                               int * size )
{
    if( !peer->outBlockLoaded )
    {
        uint8_t * p;
        tr_request_t * r;

        if( peer->amChoking || peer->outRequestCount < 1 )
        {
            /* No piece to send */
            return NULL;
        }

        /* We need to load the block for the next request */
        r = &peer->outRequests[0];

        /* Sanity check */
        if( !tr_cpPieceIsComplete( tor->completion, r->index ) )
        {
            /* We have been asked for something we don't have, buggy client?
               Let's just drop this request */
            tr_inf( "Block %d/%d/%d was requested but we don't have it",
                    r->index, r->begin, r->length );
            (peer->outRequestCount)--;
            memmove( &peer->outRequests[0], &peer->outRequests[1],
                     peer->outRequestCount * sizeof( tr_request_t ) );
            return NULL;
        }
        
        p = (uint8_t *) peer->outBlock;

        TR_HTONL( 9 + r->length, p );
        p[4] = 7;
        TR_HTONL( r->index, p + 5 );
        TR_HTONL( r->begin, p + 9 );

        tr_ioRead( tor->io, r->index, r->begin, r->length, &p[13] );

        peer_dbg( "SEND piece %d/%d (%d bytes)",
                  r->index, r->begin, r->length );

        peer->outBlockSize   = 13 + r->length;
        peer->outBlockLoaded = 1;

        (peer->outRequestCount)--;
        memmove( &peer->outRequests[0], &peer->outRequests[1],
                 peer->outRequestCount * sizeof( tr_request_t ) );
    }

    *size = MIN( 1024, peer->outBlockSize );

    return (uint8_t *) peer->outBlock;
}

static void blockSent( tr_peer_t * peer, int size )
{
    peer->outBlockSize -= size;
    memmove( peer->outBlock, &peer->outBlock[size], peer->outBlockSize );

    if( peer->outBlockSize > 0 )
    {
        /* We can't send messages until we are done sending the block */
        peer->outBlockSending = 1;
    }
    else
    {
        /* Block fully sent */
        peer->outBlockSending = 0;
        peer->outBlockLoaded  = 0;
    }
}

static uint8_t * getPointerForSize( tr_peer_t * peer, int size )
{
    uint8_t * p;

    if( peer->outMessagesPos + size > peer->outMessagesSize )
    {
        peer->outMessagesSize = peer->outMessagesPos + size;
        peer->outMessages     = realloc( peer->outMessages,
                                         peer->outMessagesSize );
    }

    p                     = &peer->outMessages[peer->outMessagesPos];
    peer->outMessagesPos += size;

    return p;
}

/***********************************************************************
 * sendKeepAlive
 ***********************************************************************
 * 
 **********************************************************************/
static void sendKeepAlive( tr_peer_t * peer )
{
    uint8_t * p;

    p = getPointerForSize( peer, 4 );

    TR_HTONL( 0, p );

    peer_dbg( "SEND keep-alive" );
}


/***********************************************************************
 * sendChoke
 ***********************************************************************
 * 
 **********************************************************************/
static void sendChoke( tr_peer_t * peer, int yes )
{
    uint8_t * p;

    p = getPointerForSize( peer, 5 );

    TR_HTONL( 1, p );
    p[4] = yes ? 0 : 1;

    peer->amChoking = yes;

    if( !yes )
    {
        /* Drop older requests from the last time it was unchoked,
           if any */
        peer->outRequestCount = 0;
    }

    peer_dbg( "SEND %schoke", yes ? "" : "un" );
}

/***********************************************************************
 * sendInterest
 ***********************************************************************
 * 
 **********************************************************************/
static void sendInterest( tr_peer_t * peer, int yes )
{
    uint8_t * p;

    p = getPointerForSize( peer, 5 );
    
    TR_HTONL( 1, p );
    p[4] = yes ? 2 : 3;

    peer->amInterested = yes;

    peer_dbg( "SEND %sinterested", yes ? "" : "un" );
}

/***********************************************************************
 * sendHave
 ***********************************************************************
 * 
 **********************************************************************/
static void sendHave( tr_peer_t * peer, int piece )
{
    uint8_t * p;

    p = getPointerForSize( peer, 9 );

    TR_HTONL( 5, &p[0] );
    p[4] = 4;
    TR_HTONL( piece, &p[5] );

    peer_dbg( "SEND have %d", piece );
}

/***********************************************************************
 * sendBitfield
 ***********************************************************************
 * Builds a 'bitfield' message:
 *  - size = 5 + X (4 bytes)
 *  - id   = 5     (1 byte)
 *  - bitfield     (X bytes)
 **********************************************************************/
static void sendBitfield( tr_torrent_t * tor, tr_peer_t * peer )
{
    uint8_t * p;
    int       bitfieldSize = ( tor->info.pieceCount + 7 ) / 8;

    p = getPointerForSize( peer, 5 + bitfieldSize );

    TR_HTONL( 1 + bitfieldSize, p );
    p[4] = 5;
    memcpy( &p[5], tr_cpPieceBitfield( tor->completion ), bitfieldSize );

    peer_dbg( "SEND bitfield" );
}

/***********************************************************************
 * sendRequest
 ***********************************************************************
 *
 **********************************************************************/
static void sendRequest( tr_torrent_t * tor, tr_peer_t * peer, int block )
{
    tr_info_t * inf = &tor->info;
    tr_request_t * r;
    uint8_t * p;

    /* Get the piece the block is a part of, its position in the piece
       and its size */
    r         = &peer->inRequests[peer->inRequestCount];
    r->index  = block / ( inf->pieceSize / tor->blockSize );
    r->begin  = ( block % ( inf->pieceSize / tor->blockSize ) ) *
                    tor->blockSize;
    r->length = tor->blockSize;
    if( block == tor->blockCount - 1 )
    {
        int lastSize = inf->totalSize % tor->blockSize;
        if( lastSize )
        {
            r->length = lastSize;
        }
    }
    (peer->inRequestCount)++;

    /* Build the "ask" message */
    p = getPointerForSize( peer, 17 );

    TR_HTONL( 13, p );
    p[4] = 6;
    TR_HTONL( r->index, p + 5 );
    TR_HTONL( r->begin, p + 9 );
    TR_HTONL( r->length, p + 13 );

    tr_cpDownloaderAdd( tor->completion, block );

    peer_dbg( "SEND request %d/%d (%d bytes)",
              r->index, r->begin, r->length );
}

/***********************************************************************
 * sendCancel
 ***********************************************************************
 *
 **********************************************************************/
static void sendCancel( tr_torrent_t * tor, int block )
{
    int i, j;
    uint8_t * p;
    tr_peer_t * peer;
    tr_request_t * r;

    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];

        for( j = 1; j < peer->inRequestCount; j++ )
        {
            r = &peer->inRequests[j];

            if( block != tr_block( r->index, r->begin ) )
            {
                continue;
            }

            p = getPointerForSize( peer, 17 );
        
            /* Build the "cancel" message */
            TR_HTONL( 13, p );
            p[4] = 8;
            TR_HTONL( r->index,  p + 5  );
            TR_HTONL( r->begin,  p + 9  );
            TR_HTONL( r->length, p + 13 );

            peer_dbg( "SEND cancel %d/%d (%d bytes)",
                      r->index, r->begin, r->length );

            (peer->inRequestCount)--;
            memmove( &peer->inRequests[j], &peer->inRequests[j+1],
                     ( peer->inRequestCount - j ) * sizeof( tr_request_t ) );
            break;
        }
    }
}
