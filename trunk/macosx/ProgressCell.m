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

#include "ProgressCell.h"

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

@implementation ProgressCell

- (id) init
{
    NSImage * bgImg;
    NSSize    size;

    self = [super init];

    /* Have a NSBitmapImageRep ready to draw the progression bar */
    bgImg  = [NSImage imageNamed: @"Progress.tiff"];
    fBgBmp = [[bgImg representations] objectAtIndex: 0];
    size   = [bgImg size];
    fImg   = [[NSImage alloc] initWithSize: size];
    fBmp   = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes: NULL pixelsWide: size.width
        pixelsHigh: size.height bitsPerSample: 8 samplesPerPixel: 4
        hasAlpha: YES isPlanar: NO
        colorSpaceName: NSCalibratedRGBColorSpace
        bytesPerRow: 0 bitsPerPixel: 0];

    [fImg addRepresentation: fBmp];

    return self;
}

- (void) setStat: (tr_stat_t *) stat
{
    int i;

    fStat = stat;

    fProgressString = [NSString stringWithFormat:
        @"%.2f %%", 100.0 * fStat->progress];
    fDlString = [NSString stringWithFormat:
        @"DL: %.2f KB/s", fStat->rateDownload];
    fUlString = [NSString stringWithFormat:
        @"UL: %.2f KB/s", fStat->rateUpload];

    for( i = 0; i < [fImg size].height; i++ )
    {
        memcpy( [fBmp bitmapData] + i * [fBmp bytesPerRow],
                [fBgBmp bitmapData] + i * [fBgBmp bytesPerRow],
                [fImg size].width * 4 );
    }

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

- (void) buildSimpleBar
{
    int        h, w;
    uint32_t * p;

    for( h = 0; h < 14; h++ )
    {
        p = (uint32_t *) ( [fBmp bitmapData] +
                h * [fBmp bytesPerRow] ) + 2;

        for( w = 0; w < 120; w++ )
        {
            *p = kBlue2[h];

            if( w >= (int) ( fStat->progress * 120 ) )
            {
                break;
            }

            p++;
        }
    }
}

- (void) buildAdvancedBar
{
    int        h, w;
    uint32_t * p;

    if( fStat->status & TR_STATUS_SEED )
    {
        for( h = 0; h < 2; h++ )
        {
            p = (uint32_t *) ( [fBmp bitmapData] +
                    h * [fBmp bytesPerRow] ) + 2;
        }
    }

    for( h = 0; h < 14; h++ )
    {
        p = (uint32_t *) ( [fBmp bitmapData] +
                h * [fBmp bytesPerRow] ) + 2;

        for( w = 0; w < 120; w++ )
        {
            if( fStat->status & TR_STATUS_SEED )
            {
                *p = kGreen[h];
            }
            else
            {
                /* Download is not finished yet */
                if( h < 2 )
                {
                    /* First two lines: dark blue to show progression */
                    *p = kBlue4[h];

                    if( w >= (int) ( fStat->progress * 120 ) )
                    {
                        break;
                    }
                }
                else
                {
                    /* Lines 2 to X: blue or grey depending on whether
                       we have the piece or not */
                    if( fStat->pieces[w] < 0 )
                    {
                        *p = kGray[h];
                    }
                    else if( fStat->pieces[w] < 1 )
                    {
                        *p = kRed[h];
                    }
                    else if( fStat->pieces[w] < 2 )
                    {
                        *p = kBlue1[h];
                    }
                    else if( fStat->pieces[w] < 3 )
                    {
                        *p = kBlue2[h];
                    }
                    else
                    {
                        *p = kBlue3[h];
                    }
                }
            }

            p++;
        }
    }
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    if( ![view lockFocusIfCanDraw] )
    {
        return;
    }

    NSMutableDictionary * attributes;
    NSPoint pen = cellFrame.origin;

    attributes = [NSMutableDictionary dictionaryWithCapacity: 1];
    [attributes setObject: [NSFont messageFontOfSize:12.0]
        forKey: NSFontAttributeName];

    pen.x += 5; pen.y += 5;

    pen.y += [fImg size].height;
    [fImg compositeToPoint: pen operation: NSCompositeSourceOver];
    pen.y -= [fImg size].height;

    [attributes setObject: [NSFont messageFontOfSize:10.0]
        forKey: NSFontAttributeName];

    pen.x += 5; pen.y += 20;
    [fDlString drawAtPoint: pen withAttributes: attributes];

    pen.x += 0; pen.y += 15;
    [fUlString drawAtPoint: pen withAttributes: attributes];

    [view unlockFocus];
}

@end
