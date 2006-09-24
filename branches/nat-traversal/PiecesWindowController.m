//
//  PiecesWindowController.m
//  Transmission
//
//  Created by Livingston on 9/23/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import "PiecesWindowController.h"

#define HEIGHT 4.0
#define WIDTH 4.0
#define BETWEEN 1.0
#define ACROSS 18
#define DOWN 18

#define BLANK -99

@implementation PiecesWindowController

- (id) initWithWindowNibName: (NSString *) name
{
    if ((self = [super initWithWindowNibName: name]))
    {
        fTorrent = nil;
        fPieces = malloc(ACROSS * DOWN);
        int i;
        for (i = 0; i < DOWN * ACROSS; i++)
            fPieces[i] = BLANK;
        
        fBack = [NSImage imageNamed: @"PiecesBack.tiff"];
        fWhitePiece = [NSImage imageNamed: @"BoxWhite.tiff"];
        fGreenPiece = [NSImage imageNamed: @"BoxGreen.tiff"];
        fBlue1Piece = [NSImage imageNamed: @"BoxBlue1.tiff"];
        fBlue2Piece = [NSImage imageNamed: @"BoxBlue2.tiff"];
        fBlue3Piece = [NSImage imageNamed: @"BoxBlue3.tiff"];
        
        fExistingImage = [fBack copy];
    }
    
    return self;
}

- (void) awakeFromNib
{
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    [window setBecomesKeyOnlyIfNeeded: YES];
    
    [window setFrameAutosaveName: @"PiecesWindowFrame"];
    [window setFrameUsingName: @"PiecesWindowFrame"];
}

- (void) dealloc
{
    free(fPieces);
    
    if (fTorrent)
        [fTorrent release];
    [fExistingImage release];
    [super dealloc];
}

- (void) setTorrent: (Torrent *) torrent
{
    BOOL first = YES;
    
    if (fTorrent)
    {
        [fTorrent release];
        
        if (!torrent)
        {
            fTorrent = nil;
            [fImageView setImage: fBack];
        }
        else
            first = NO;
    }
    
    if (torrent)
    {
        fTorrent = [torrent retain];
        [self updateView: first];
    }
}

- (void) updateView: (BOOL) first
{
    if (!fTorrent)
        return;
    
    int numPieces = ACROSS * DOWN;
    int8_t * pieces = malloc(numPieces);
    [fTorrent getAvailability: pieces size: numPieces];
    
    int i, j, piece, index = -1;
    NSPoint point;
    NSImage * pieceImage;
    BOOL change = NO;
    
    [fExistingImage lockFocus];
    
    for (i = 0; i < DOWN; i++)
        for (j = 0; j < ACROSS; j++)
        {
            pieceImage = nil;
        
            index++;
            piece = pieces[index];
            if (piece < 0)
            {
                if (fPieces[index] != -1)
                {
                    fPieces[index] = -1;
                    pieceImage = fGreenPiece;
                }
            }
            else if (piece == 0)
            {
                if (fPieces[index] != 0)
                {
                    fPieces[index] = 0;
                    pieceImage = fWhitePiece;
                }
            }
            else if (piece == 1)
            {
                if (fPieces[index] != 1)
                {
                    fPieces[index] = 1;
                    pieceImage = fBlue1Piece;
                }
            }
            else if (piece == 2)
            {
                if (fPieces[index] != 2)
                {
                    fPieces[index] = 2;
                    pieceImage = fBlue2Piece;
                }
            }
            else
            {
                if (fPieces[index] != 3)
                {
                    fPieces[index] = 3;
                    pieceImage = fBlue3Piece;
                }
            }
            
            if (pieceImage)
            {
                point = NSMakePoint(j * (WIDTH + BETWEEN) + BETWEEN, i * (HEIGHT + BETWEEN) + BETWEEN);
                [pieceImage compositeToPoint: point operation: NSCompositeSourceOver];
                
                change = YES;
            }
        }
    
    [fExistingImage unlockFocus];
    
    //reload the image regardless if it wasn't called by the timer
    if (change || first)
    {
        [fImageView setImage: nil];
        [fImageView setImage: fExistingImage];
    }
    
    free(pieces);
}

@end
