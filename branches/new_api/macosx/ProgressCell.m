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

#import "ProgressCell.h"
#import "StringAdditions.h"

@implementation ProgressCell

/***********************************************************************
 * Static tables
 ***********************************************************************
 * We use these tables to optimize the drawing. They contain packed
 * RGBA pixels for every color we might need.
 **********************************************************************/
#if 0
/* Coefficients for the "3D effect" */
static float kBarCoeffs[] =
    { 0.49, 0.73, 0.84, 0.85, 0.84, 0.79, 0.78,
      0.82, 0.86, 0.91, 0.93, 0.95, 0.96, 0.71 };
#endif

/* 255, 100, 80 */
static uint32_t kRed[] =
    { 0x7C3127FF, 0xBA493AFF, 0xD65443FF, 0xD85544FF, 0xD65443FF,
      0xC94F3FFF, 0xC64E3EFF, 0xD15241FF, 0xDB5644FF, 0xE85B48FF,
      0xED5D4AFF, 0xF25F4CFF, 0xF4604CFF, 0xB54738FF };

/* 160, 220, 255 */
static uint32_t kBlue1[] =
    { 0x4E6B7CFF, 0x74A0BAFF, 0x86B8D6FF, 0x88BBD8FF, 0x86B8D6FF,
      0x7EADC9FF, 0x7CABC6FF, 0x83B4D1FF, 0x89BDDBFF, 0x91C8E8FF,
      0x94CCEDFF, 0x98D1F2FF, 0x99D3F4FF, 0x719CB5FF };

/* 120, 190, 255 */
static uint32_t kBlue2[] =
    { 0x3A5D7CFF, 0x578ABAFF, 0x649FD6FF, 0x66A1D8FF, 0x649FD6FF,
      0x5E96C9FF, 0x5D94C6FF, 0x629BD1FF, 0x67A3DBFF, 0x6DACE8FF,
      0x6FB0EDFF, 0x72B4F2FF, 0x73B6F4FF, 0x5586B5FF };

/* 80, 160, 255 */
static uint32_t kBlue3[] =
    { 0x274E7CFF, 0x3A74BAFF, 0x4386D6FF, 0x4488D8FF, 0x4386D6FF,
      0x3F7EC9FF, 0x3E7CC6FF, 0x4183D1FF, 0x4489DBFF, 0x4891E8FF,
      0x4A94EDFF, 0x4C98F2FF, 0x4C99F4FF, 0x3871B5FF };

/* 30, 70, 180 */
static uint32_t kBlue4[] =
    { 0x0E2258FF, 0x153383FF, 0x193A97FF, 0x193B99FF, 0x193A97FF,
      0x17378EFF, 0x17368CFF, 0x183993FF, 0x193C9AFF, 0x1B3FA3FF,
      0x1B41A7FF, 0x1C42ABFF, 0x1C43ACFF, 0x15317FFF };

/* 130, 130, 130 */
static uint32_t kGray[] =
    { 0x3F3F3FFF, 0x5E5E5EFF, 0x6D6D6DFF, 0x6E6E6EFF, 0x6D6D6DFF,
      0x666666FF, 0x656565FF, 0x6A6A6AFF, 0x6F6F6FFF, 0x767676FF,
      0x787878FF, 0x7B7B7BFF, 0x7C7C7CFF, 0x5C5C5CFF };

/* 0, 255, 0 */
static uint32_t kGreen[] =
    { 0x007C00FF, 0x00BA00FF, 0x00D600FF, 0x00D800FF, 0x00D600FF,
      0x00C900FF, 0x00C600FF, 0x00D100FF, 0x00DB00FF, 0x00E800FF,
      0x00ED00FF, 0x00F200FF, 0x00F400FF, 0x00B500FF };

/* 0, 255, 0 */
static uint32_t kBack[] =
    { 0xB2B2B2FF, 0xDADADAFF, 0xEAEAEAFF, 0xECECECFF, 0xEAEAEAFF,
      0xE2E2E2FF, 0xE1E1E1FF, 0xE7E7E7FF, 0xEDEDEDFF, 0xF3F3F3FF,
      0xF6F6F6FF, 0xF8F8F8FF, 0xFAFAFAFF, 0xD7D7D7FF };

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
}

- (void) setTextColor: (NSColor *) color
{
    fTextColor = color;
}

/***********************************************************************
 * init
 ***********************************************************************
 * Prepares the NSBitmapImageReps we are going to need in order to
 * draw.
 **********************************************************************/
- (id) init
{
    self = [super init];

    /* Load the background image for the progress bar and get it as a
       32-bit bitmap */
    fBackground = [[[NSImage imageNamed: @"Progress.png"]
                     representations] objectAtIndex: 0];

    return self;
}

/***********************************************************************
 * buildSimpleBar
 **********************************************************************/
- (void) buildSimpleBar
{
    int        h, w, end, pixelsPerRow;
    uint32_t * p;
    uint32_t * colors;

    pixelsPerRow = [fBitmap bytesPerRow] / 4;

    /* The background image is 124*18 pixels, but the actual
       progress bar is 120*14 : the first two columns, the last
       two columns and the last four lines contain the shadow. */

    p   = (uint32_t *) [fBitmap bitmapData] + 2;
    end = lrintf( floor( [fTorrent progress] * fWidth ) );

    /*
    if( fStat->status & TR_STATUS_SEED )
        colors = kGreen;
    else if( fStat->status & ( TR_STATUS_CHECK | TR_STATUS_DOWNLOAD ) )
    */
        colors = kBlue2;
    /*
    else
        colors = kGray;
    */

    for( h = 0; h < 14; h++ )
    {
        for( w = 0; w < end; w++ )
        {
            p[w] = htonl( colors[h] );
        }
        for( w = end; w < fWidth; w++ )
        {
            p[w] = htonl( kBack[h] );
        }
        p += pixelsPerRow;
    }
}

/***********************************************************************
 * buildAdvancedBar
 **********************************************************************/
- (void) buildAdvancedBar
{
    int        h, w, end, pixelsPerRow;
    uint32_t * p;
    uint32_t * colors;

    fPieces = malloc( fWidth );
    [fTorrent getAvailability: fPieces size: fWidth];

#if 0
    if( fStat->status & TR_STATUS_SEED )
    {
        /* All green, same as the simple bar */
        [self buildSimpleBar];
        return;
    }
#endif

    pixelsPerRow = [fBitmap bytesPerRow] / 4;

    /* First two lines: dark blue to show progression */
    p    = (uint32_t *) [fBitmap bitmapData];
    p   += 2;
    end  = lrintf( floor( [fTorrent progress] * fWidth ) );
    for( h = 0; h < 2; h++ )
    {
        for( w = 0; w < end; w++ )
        {
            p[w] = htonl( kBlue4[h] );
        }
        for( w = end; w < fWidth; w++ )
        {
            p[w] = htonl( kBack[h] );
        }
        p += pixelsPerRow;
    }

    /* Lines 2 to 14: blue or grey depending on whether
       we have the piece or not */
    for( w = 0; w < fWidth; w++ )
    {
        /* Point to pixel ( 2 + w, 2 ). We will then draw
           "vertically" */
        p  = (uint32_t *) ( [fBitmap bitmapData] +
                2 * [fBitmap bytesPerRow] );
        p += 2 + w;

        if( fPieces[w] < 0 )
        {
            colors = kGray;
        }
        else if( fPieces[w] < 1 )
        {
            colors = kRed;
        }
        else if( fPieces[w] < 2 )
        {
            colors = kBlue1;
        }
        else if( fPieces[w] < 3 )
        {
            colors = kBlue2;
        }
        else
        {
            colors = kBlue3;
        }

        for( h = 2; h < 14; h++ )
        {
            p[0]  = htonl( colors[h] );
            p    += pixelsPerRow;
        }
    }

    free( fPieces );
}

- (void) buildBar
{
    int i, j;
    uint32_t * in, * out;

    /* Reset our bitmap to the background image... */
    in  = (uint32_t *) [fBackground bitmapData];
    out = (uint32_t *) [fBitmap bitmapData];
    for( i = 0; i < [fBitmap size].height - 4; i++ )
    {
        for( j = 0; j < 2; j++ )
        {
            out[j] = in[j];
        }
        for( j = fWidth + 2; j < fWidth + 4; j++ )
        {
            out[j] = in[(int)[fBackground size].width + j - fWidth - 4];
        }
        in  += [fBackground bytesPerRow] / 4;
        out += [fBitmap bytesPerRow] / 4;
    }
    for( i = [fBitmap size].height - 4; i < [fBitmap size].height; i++ )
    {
        for( j = 0; j < 2; j++ )
        {
            out[j] = in[j];
        }
        for( j = 2; j < fWidth + 2; j++ )
        {
            out[j] = in[2];
        }
        for( j = fWidth + 2; j < fWidth + 4; j++ )
        {
            out[j] = in[(int)[fBackground size].width + j - fWidth - 4];
        }
        in  += [fBackground bytesPerRow] / 4;
        out += [fBitmap bytesPerRow] / 4;
    }

    /* ...and redraw the progress bar on the top of it */
    if( [[NSUserDefaults standardUserDefaults]
            boolForKey:@"UseAdvancedBar"])
    {
        [self buildAdvancedBar];
    }
    else
    {
        [self buildSimpleBar];
    }
}

/***********************************************************************
 * drawWithFrame
 ***********************************************************************
 * We have the strings, we have the bitmap. Let's just draw them where
 * they belong.
 **********************************************************************/
- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    NSImage * img;
    NSMutableDictionary * attributes;
    NSPoint pen;

    if( ![view lockFocusIfCanDraw] )
    {
        return;
    }

    pen = cellFrame.origin;

    fWidth  = NSWidth( cellFrame ) - 14;
    fBitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes: nil pixelsWide: fWidth + 4
        pixelsHigh: 18 bitsPerSample: 8 samplesPerPixel: 4
        hasAlpha: YES isPlanar: NO colorSpaceName:
        NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    img = [[NSImage alloc] initWithSize: [fBitmap size]];
    [img addRepresentation: fBitmap];
    [img setFlipped: YES];

    /* Draw the progress bar */
    pen.x += 5; pen.y += 5;
    [self buildBar];
    [img drawAtPoint: pen fromRect: NSMakeRect( 0, 0,
            [img size].width, [img size].height )
        operation: NSCompositeSourceOver fraction: 1.0];

    [img release];
    [fBitmap release];

    /* Draw the strings with font 10 */
    attributes = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSFont messageFontOfSize: 10.0], NSFontAttributeName,
        fTextColor, NSForegroundColorAttributeName, nil];
    pen.x += 5; pen.y += 20;
    [[fTorrent downloadString] drawAtPoint: pen withAttributes: attributes];
    pen.x += 0; pen.y += 15;
    [[fTorrent uploadString] drawAtPoint: pen withAttributes: attributes];

    [view unlockFocus];
}

@end
