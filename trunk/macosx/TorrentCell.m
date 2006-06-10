/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#import "TorrentCell.h"
#import "StringAdditions.h"

#define BAR_HEIGHT 12

@implementation TorrentCell

/***********************************************************************
 * Static tables
 ***********************************************************************
 * We use these tables to optimize the drawing. They contain packed
 * RGBA pixels for every color we might need.
 **********************************************************************/

static uint32_t kBorder[] =
    { 0x00000005, 0x00000010, 0x00000015, 0x00000015,
      0x00000015, 0x00000015, 0x00000015, 0x00000015,
      0x00000015, 0x00000015, 0x00000010, 0x00000005 };

static uint32_t kBack[] =
    { 0xB4B4B4FF, 0xE3E3E3FF, 0xE8E8E8FF, 0xDEDEDEFF,
      0xDBDBDBFF, 0xE5E5E5FF, 0xE7E7E7FF, 0xF5F5F5FF,
      0xFAFAFAFF, 0xDEDEDEFF, 0x0000003F, 0x00000015 };

/* Coefficients for the "3D effect":
   0.59, 0.91, 0.97, 0.92, 0.79, 0.76, 0.85, 0.93, 1.00, 0.99 */

/* 255, 100, 80 */
static uint32_t kRed[] =
    { 0x963A2FFF, 0xE85B48FF, 0xF7614DFF, 0xEA5C49FF,
      0xC94F3FFF, 0xC14C3CFF, 0xD85544FF, 0xED5D4AFF,
      0xFF6450FF, 0xFC634FFF, 0x0000003F, 0x00000015 };

/* 160, 220, 255 */
static uint32_t kBlue1[] =
    { 0x5E8196FF, 0x91C8E8FF, 0x9BD5F7FF, 0x93CAEAFF,
      0x7EADC9FF, 0x79A7C1FF, 0x88BBD8FF, 0x94CCEDFF,
      0xA0DCFFFF, 0x9ED9FCFF, 0x0000003F, 0x00000015 };

/* 120, 190, 255 */
static uint32_t kBlue2[] =
    { 0x467096FF, 0x6DACE8FF, 0x74B8F7FF, 0x6EAEEAFF,
      0x5E96C9FF, 0x5B90C1FF, 0x66A1D8FF, 0x6FB0EDFF,
      0x78BEFFFF, 0x76BCFCFF, 0x0000003F, 0x00000015 };

/* 80, 160, 255 */
static uint32_t kBlue3[] =
    { 0x2F5E96FF, 0x4891E8FF, 0x4D9BF7FF, 0x4993EAFF,
      0x3F7EC9FF, 0x3C79C1FF, 0x4488D8FF, 0x4A94EDFF,
      0x50A0FFFF, 0x4F9EFCFF, 0x0000003F, 0x00000015 };

/* 30, 70, 180 */
static uint32_t kBlue4[] =
    { 0x11296AFF, 0x1B3FA3FF, 0x1D43AEFF, 0x1B40A5FF,
      0x17378EFF, 0x163588FF, 0x193B99FF, 0x1B41A7FF,
      0x1E46B4FF, 0x1D45B2FF, 0x0000003F, 0x00000015 };

/* 130, 130, 130 */
static uint32_t kGray[] =
    { 0x4C4C4CFF, 0x767676FF, 0x7E7E7EFF, 0x777777FF,
      0x666666FF, 0x626262FF, 0x6E6E6EFF, 0x787878FF,
      0x828282FF, 0x808080FF, 0x0000003F, 0x00000015 };

/* 0, 255, 0 */
static uint32_t kGreen[] =
    { 0x009600FF, 0x00E800FF, 0x00F700FF, 0x00EA00FF,
      0x00C900FF, 0x00C100FF, 0x00D800FF, 0x00ED00FF,
      0x00FF00FF, 0x00FC00FF, 0x0000003F, 0x00000015 };

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
}

- (void) buildSimpleBar: (int) width bitmap: (NSBitmapImageRep *) bitmap
{
    int        h, w, end, pixelsPerRow;
    uint32_t * p;
    uint32_t * colors;

    pixelsPerRow = [bitmap bytesPerRow] / 4;

    p   = (uint32_t *) [bitmap bitmapData] + 1;
    end = lrintf( floor( [fTorrent progress] * ( width - 2 ) ) );

    if( [fTorrent isSeeding] )
        colors = kGreen;
    else if( [fTorrent isActive] )
        colors = kBlue2;
    else
        colors = kGray;

    for( h = 0; h < BAR_HEIGHT; h++ )
    {
        for( w = 0; w < end; w++ )
        {
            p[w] = htonl( colors[h] );
        }
        for( w = end; w < width - 2; w++ )
        {
            p[w] = htonl( kBack[h] );
        }
        p += pixelsPerRow;
    }
}

- (void) buildAdvancedBar: (int) width bitmap: (NSBitmapImageRep *) bitmap
{
    int      h, w, end, pixelsPerRow;
    uint32_t * p, * colors;
    uint8_t  * bitmapData  = [bitmap bitmapData];
    int        bytesPerRow = [bitmap bytesPerRow];

    int8_t * pieces = malloc( width );
    [fTorrent getAvailability: pieces size: width];

    if( [fTorrent isSeeding] )
    {
        /* All green, same as the simple bar */
        [self buildSimpleBar: width bitmap: bitmap];
        return;
    }

    pixelsPerRow = bytesPerRow / 4;

    /* First two lines: dark blue to show progression */
    end  = lrintf( floor( [fTorrent progress] * ( width - 2 ) ) );
    for( h = 0; h < 2; h++ )
    {
        p = (uint32_t *) ( bitmapData + h * bytesPerRow ) + 1;
        for( w = 0; w < end; w++ )
        {
            p[w] = htonl( kBlue4[h] );
        }
        for( w = end; w < width - 2; w++ )
        {
            p[w] = htonl( kBack[h] );
        }
    }

    /* Lines 2 to 14: blue or grey depending on whether
       we have the piece or not */
    for( w = 0; w < width - 2; w++ )
    {
        /* Point to pixel ( 2 + w, 2 ). We will then draw
           "vertically" */
        p  = (uint32_t *) ( bitmapData + 2 * bytesPerRow ) + 1 + w;

        if( pieces[w] < 0 )
        {
            colors = kGray;
        }
        else if( pieces[w] < 1 )
        {
            colors = kRed;
        }
        else if( pieces[w] < 2 )
        {
            colors = kBlue1;
        }
        else if( pieces[w] < 3 )
        {
            colors = kBlue2;
        }
        else
        {
            colors = kBlue3;
        }

        for( h = 2; h < BAR_HEIGHT; h++ )
        {
            p[0] = htonl( colors[h] );
            p    = (uint32_t *) ( (uint8_t *) p + bytesPerRow );
        }
    }

    free( pieces );
}

- (void) buildBarWithWidth: (int) width bitmap: (NSBitmapImageRep *) bitmap
{
    int h;
    uint32_t * p;

    /* Left and right borders */
    p = (uint32_t *) [bitmap bitmapData];
    for( h = 0; h < BAR_HEIGHT; h++ )
    {
        p[0]          = htonl( kBorder[h] );
        p[width - 1] = htonl( kBorder[h] );
        p += [bitmap bytesPerRow] / 4;
    }

    /* ...and redraw the progress bar on the top of it */
    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"UseAdvancedBar"])
        [self buildAdvancedBar: width bitmap: bitmap];
    else
        [self buildSimpleBar: width bitmap: bitmap];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    BOOL highlighted = [self isHighlighted] && [[view window] isKeyWindow];
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor blackColor],
                    NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName, nil];
    NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor],
                    NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 9.0], NSFontAttributeName, nil];

    NSPoint pen = cellFrame.origin;
    float padding = 3.0, linePadding = 2.0;

    //icon
    NSImage * icon = [fTorrent iconFlipped];
    NSSize iconSize = [icon size];
    
    pen.x += padding;
    pen.y += (cellFrame.size.height - iconSize.height) * 0.5;
    
    [icon drawAtPoint: pen fromRect:
        NSMakeRect( 0, 0, iconSize.width, iconSize.height )
        operation: NSCompositeSourceOver fraction: 1.0];

    float extraNameShift = 1.0,
        mainWidth = cellFrame.size.width - iconSize.width - 3.0 * padding - extraNameShift;

    //name string
    pen.x += iconSize.width + padding + extraNameShift;
    pen.y = cellFrame.origin.y + padding;
    NSAttributedString * nameString = [[fTorrent name] attributedStringFittingInWidth: mainWidth
                                attributes: nameAttributes];
    [nameString drawAtPoint: pen];
    
    //progress string
    pen.y += [nameString size].height + linePadding - 1.0;
    
    NSAttributedString * progressString = [[fTorrent progressString]
        attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
    [progressString drawAtPoint: pen];

    //progress bar
    pen.x -= extraNameShift;
    pen.y += [progressString size].height + linePadding;
    
    float barWidth = mainWidth + extraNameShift - 42.0 - padding;
    
    NSBitmapImageRep * bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes: nil pixelsWide: barWidth
        pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4
        hasAlpha: YES isPlanar: NO colorSpaceName:
        NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    NSImage * img = [[NSImage alloc] initWithSize: [bitmap size]];
    [img addRepresentation: bitmap];
    [img setFlipped: YES];
    
    [self buildBarWithWidth: barWidth bitmap: bitmap];
    
    [img drawAtPoint: pen fromRect: NSMakeRect( 0, 0,
            [img size].width, [img size].height )
        operation: NSCompositeSourceOver fraction: 1.0];
    [img release];
    [bitmap release];

    //status strings
    pen.x += extraNameShift;
    pen.y += BAR_HEIGHT + linePadding;
    NSAttributedString * statusString = [[fTorrent statusString]
        attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
    [statusString drawAtPoint: pen];
    
    [nameAttributes release];
    [statusAttributes release];
}

@end
