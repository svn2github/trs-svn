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

#include "transmission.h"

tr_completion_t * tr_cpInit( tr_torrent_t * tor )
{
    tr_completion_t * cp;
    int i;

    cp                   = calloc( 1, sizeof( tr_completion_t ) );
    cp->tor              = tor;
    cp->blockBitfield    = calloc( 1, ( tor->blockCount + 7 ) / 8 );
    cp->blockDownloaders = calloc( 1, tor->blockCount );
    cp->pieceBitfield    = calloc( 1, ( tor->info.pieceCount + 7 ) / 8 );
    cp->missingBlocks    = calloc( 1, tor->info.pieceCount * sizeof( int ) );

    for( i = 0; i < tor->info.pieceCount; i++ )
    {
        cp->missingBlocks[i] = tr_pieceCountBlocks( i );
    }

    return cp;
}

void tr_cpClose( tr_completion_t * cp )
{
    free( cp->blockBitfield );
    free( cp->blockDownloaders );
    free( cp->pieceBitfield );
    free( cp->missingBlocks );
    free( cp );
}

float tr_cpCompletionAsFloat( tr_completion_t * cp )
{
    return (float) cp->blockCount / (float) cp->tor->blockCount;
}

uint64_t tr_cpLeftBytes( tr_completion_t * cp )
{
    tr_torrent_t * tor = cp->tor;
    uint64_t left;
    left = (uint64_t) ( cp->tor->blockCount - cp->blockCount ) *
           (uint64_t) tor->blockSize;
    if( !tr_bitfieldHas( cp->blockBitfield, cp->tor->blockCount - 1 ) &&
        tor->info.totalSize % tor->blockSize )
    {
        left += tor->info.totalSize % tor->blockSize;
        left -= tor->blockSize;
    }
    return left;
}

/* Pieces */
int tr_cpPieceIsComplete( tr_completion_t * cp, int piece )
{
    return tr_bitfieldHas( cp->pieceBitfield, piece );
}

uint8_t * tr_cpPieceBitfield( tr_completion_t * cp )
{
    return cp->pieceBitfield;
}

void tr_cpPieceAdd( tr_completion_t * cp, int piece )
{
    tr_torrent_t * tor = cp->tor;
    int startBlock, endBlock, i;

    startBlock = tr_pieceStartBlock( piece );
    endBlock   = startBlock + tr_pieceCountBlocks( piece );
    for( i = startBlock; i < endBlock; i++ )
    {
        tr_cpBlockAdd( cp, i );
    }

    tr_bitfieldAdd( cp->pieceBitfield, piece );
}

/* Blocks */
void tr_cpDownloaderAdd( tr_completion_t * cp, int block )
{
    tr_torrent_t * tor = cp->tor;
    if( !cp->blockDownloaders[block] && !tr_cpBlockIsComplete( cp, block ) )
    {
        cp->missingBlocks[tr_blockPiece(block)]--;
    }
    (cp->blockDownloaders[block])++;
}

void tr_cpDownloaderRem( tr_completion_t * cp, int block )
{
    tr_torrent_t * tor = cp->tor;
    (cp->blockDownloaders[block])--;
    if( !cp->blockDownloaders[block] && !tr_cpBlockIsComplete( cp, block ) )
    {
        cp->missingBlocks[tr_blockPiece(block)]++;
    }
}

int tr_cpBlockIsComplete( tr_completion_t * cp, int block )
{
    return tr_bitfieldHas( cp->blockBitfield, block );
}

void tr_cpBlockAdd( tr_completion_t * cp, int block )
{
    tr_torrent_t * tor = cp->tor;
    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        (cp->blockCount)++;
        if( !cp->blockDownloaders[block] )
        {
            (cp->missingBlocks[tr_blockPiece(block)])--;
        }
    }
    tr_bitfieldAdd( cp->blockBitfield, block );
}

void tr_cpBlockRem( tr_completion_t * cp, int block )
{
    tr_torrent_t * tor = cp->tor;
    if( tr_cpBlockIsComplete( cp, block ) )
    {
        (cp->blockCount)--;
        if( !cp->blockDownloaders[block] )
        {
            (cp->missingBlocks[tr_blockPiece(block)])++;
        }
    }
    tr_bitfieldRem( cp->blockBitfield, block );
}

uint8_t * tr_cpBlockBitfield( tr_completion_t * cp )
{
    return cp->blockBitfield;
}

void tr_cpBlockBitfieldSet( tr_completion_t * cp, uint8_t * bitfield )
{
    tr_torrent_t * tor = cp->tor;
    int i, j;
    int startBlock, endBlock;
    int pieceComplete;

    for( i = 0; i < cp->tor->info.pieceCount; i++ )
    {
        startBlock    = tr_pieceStartBlock( i );
        endBlock      = startBlock + tr_pieceCountBlocks( i );
        pieceComplete = 1;

        for( j = startBlock; j < endBlock; j++ )
        {
            if( tr_bitfieldHas( bitfield, j ) )
            {
                tr_cpBlockAdd( cp, j );
            }
            else
            {
                pieceComplete = 0;
            }
        }
        if( pieceComplete )
        {
            tr_cpPieceAdd( cp, i );
        }
    }
}

int tr_cpMissingBlockInPiece( tr_completion_t * cp, int piece )
{
    tr_torrent_t * tor = cp->tor;
    int start, count, end, i;

    start = tr_pieceStartBlock( piece );
    count = tr_pieceCountBlocks( piece );
    end   = start + count;

    for( i = start; i < end; i++ )
    {
        if( tr_cpBlockIsComplete( cp, i ) || cp->blockDownloaders[i] )
        {
            continue;
        }
        return i;
    }

    return -1;
}

int tr_cpMostMissingBlockInPiece( tr_completion_t * cp, int piece,
                                  int * downloaders )
{
    tr_torrent_t * tor = cp->tor;
    int start, count, end, i;
    int * pool, poolSize, min, ret;

    start = tr_pieceStartBlock( piece );
    count = tr_pieceCountBlocks( piece );
    end   = start + count;

    pool     = malloc( count * sizeof( int ) );
    poolSize = 0;
    min      = 255;

    for( i = start; i < end; i++ )
    {
        if( tr_cpBlockIsComplete( cp, i ) || cp->blockDownloaders[i] > min )
        {
            continue;
        }
        if( cp->blockDownloaders[i] < min )
        {
            min      = cp->blockDownloaders[i];
            poolSize = 0;
        }
        if( cp->blockDownloaders[i] <= min )
        {
            pool[poolSize++] = i;
        }
    }

    if( poolSize < 1 )
    {
        return -1;
    }

    ret = pool[0];
    free( pool );
    *downloaders = min;
    return ret;
}

