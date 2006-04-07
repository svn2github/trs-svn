/******************************************************************************
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

#import "TorrentTableView.h"
#import "Controller.h"
#import "Torrent.h"
#import "Utils.h"

@implementation TorrentTableView

- (void) setTorrents: (NSArray *) torrents
{
    fTorrents = torrents;
}

- (NSRect) pauseRectForRow: (int) row
{
    int col;
    NSRect cellRect, rect;

    col      = [self columnWithIdentifier: @"Torrent"];
    cellRect = [self frameOfCellAtColumn: col row: row];
    rect     = NSMakeRect( cellRect.origin.x + cellRect.size.width - 39,
                           cellRect.origin.y + 3, 14, 14 );

    return rect;
}

- (NSRect) revealRectForRow: (int) row
{
    int col;
    NSRect cellRect, rect;

    col      = [self columnWithIdentifier: @"Torrent"];
    cellRect = [self frameOfCellAtColumn: col row: row];
    rect     = NSMakeRect( cellRect.origin.x + cellRect.size.width - 20,
                           cellRect.origin.y + 3, 14, 14 );

    return rect;
}

- (BOOL) pointInPauseRect: (NSPoint) point
{
    return NSPointInRect( point, [self pauseRectForRow:
                                    [self rowAtPoint: point]] );
}

- (BOOL) pointInRevealRect: (NSPoint) point
{
    return NSPointInRect( point, [self revealRectForRow:
                                    [self rowAtPoint: point]] );
}


- (void) mouseDown: (NSEvent *) e
{
    fClickPoint = [self convertPoint: [e locationInWindow] fromView: nil];
    int row = [self rowAtPoint: fClickPoint];

    if( [e modifierFlags] & NSAlternateKeyMask )
    {
        [fController advancedChanged: self];
        fClickPoint = NSMakePoint( 0, 0 );
    }
    else if( ![self pointInPauseRect: fClickPoint] &&
             ![self pointInRevealRect: fClickPoint] )
    {
        [super mouseDown: e];
    }
    else;

    [self display];
}

- (void) mouseUp: (NSEvent *) e
{
    NSPoint point;
    int row;
    bool sameRow;
    Torrent * torrent;

    point = [self convertPoint: [e locationInWindow] fromView: nil];
    row   = [self rowAtPoint: point];
    sameRow = row == [self rowAtPoint: fClickPoint];
    
    if( sameRow && [self pointInPauseRect: point]
            && [self pointInPauseRect: fClickPoint] )
    {
        torrent = [fTorrents objectAtIndex: row];

		if( [torrent isPaused] )
			[fController resumeTorrentWithIndex: [NSIndexSet indexSetWithIndex: row]];
		else if( [torrent isActive] )
			[fController stopTorrentWithIndex: [NSIndexSet indexSetWithIndex: row]];
		else;
    }
    else if( sameRow && [self pointInRevealRect: point]
                && [self pointInRevealRect: fClickPoint] )
    {
        torrent = [fTorrents objectAtIndex: row];
        [torrent reveal];
    }
	else;
    
	[super mouseUp: e];

    fClickPoint = NSMakePoint( 0, 0 );
    [self display];
}

- (NSMenu *) menuForEvent: (NSEvent *) e
{
    NSPoint point;
    int row;

    point = [self convertPoint: [e locationInWindow] fromView: nil];
    row = [self rowAtPoint: point];
    
    if( row >= 0 )
    {
        if( OSX_VERSION >= 10.3 )
            [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row]
                byExtendingSelection: [self isRowSelected: row]];
        else
            [self selectRow: row byExtendingSelection: [self isRowSelected: row]];
        return fContextRow;
    }
    else
    {
        [self deselectAll: self];
        return fContextNoRow;
    }
}

- (void) drawRect: (NSRect) r
{
    unsigned i;
    NSRect rect;
    NSImage * image;
    Torrent * torrent;

    [super drawRect: r];

    for( i = 0; i < [fTorrents count]; i++ )
    {
        torrent = [fTorrents objectAtIndex: i];
        rect  = [self pauseRectForRow: i];
        image = nil;

        if( [torrent isPaused] )
        {
            image = NSPointInRect( fClickPoint, rect ) ?
                [NSImage imageNamed: @"ResumeOn.png"] :
                [NSImage imageNamed: @"ResumeOff.png"];
        }
        else if( [torrent isActive] )
        {
            image = NSPointInRect( fClickPoint, rect ) ?
                [NSImage imageNamed: @"PauseOn.png"] :
                [NSImage imageNamed: @"PauseOff.png"];
        }
        else;

        if( image )
        {
            [image setFlipped: YES];
            [image drawAtPoint: rect.origin fromRect:
                NSMakeRect( 0, 0, 14, 14 ) operation:
                NSCompositeSourceOver fraction: 1.0];
        }

        rect  = [self revealRectForRow: i];
        image = NSPointInRect( fClickPoint, rect ) ?
            [NSImage imageNamed: @"RevealOn.png"] :
            [NSImage imageNamed: @"RevealOff.png"];
        [image setFlipped: YES];
        [image drawAtPoint: rect.origin fromRect:
            NSMakeRect( 0, 0, 14, 14 ) operation:
            NSCompositeSourceOver fraction: 1.0];
    }
}

@end
