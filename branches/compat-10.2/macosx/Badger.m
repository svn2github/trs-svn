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

#import "Badger.h"
#import "StringAdditions.h"

@interface Badger (Private)

- (void) badgeString: (NSString *) string forRect: (NSRect) rect;

@end

@implementation Badger

- (id) init
{
    if ((self = [super init]))
    {
        fBadge = [NSImage imageNamed: @"Badge"];
        fDockIcon = [[NSApp applicationIconImage] copy];
        fBadgedDockIcon = [fDockIcon copy];
        fUploadBadge = [NSImage imageNamed: @"UploadBadge"];
        fDownloadBadge = [NSImage imageNamed: @"DownloadBadge"];
        
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
        NSShadow * stringShadow = [[NSShadow alloc] init];
        [stringShadow setShadowOffset: NSMakeSize(2, -2)];
        [stringShadow setShadowBlurRadius: 4];
#endif
        
        fAttributes = [[NSDictionary dictionaryWithObjectsAndKeys:
                            [NSColor whiteColor], NSForegroundColorAttributeName,
                            [NSFont fontWithName: @"Helvetica-Bold" size: 28], NSFontAttributeName,
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
                            stringShadow, NSShadowAttributeName,
#endif
                            nil] retain];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
        [stringShadow release];
#endif
        
        fCompleted = 0;
        fSpeedShown = NO;
    }
    
    return self;
}

- (void) dealloc
{
    [fDockIcon release];
    [fBadgedDockIcon release];

    [fAttributes release];

    [super dealloc];
}

- (void) updateBadgeWithCompleted: (int) completed
                    uploadRate: (NSString *) uploadRate
                    downloadRate: (NSString *) downloadRate
{
    NSImage * dockIcon = nil;
    NSSize iconSize = [fDockIcon size];

    //set seeding and downloading badges if there was a change
    if (completed != fCompleted)
    {
        fCompleted = completed;
        
        dockIcon = [fDockIcon copy];
        
        //set completed badge to top right
        if (completed > 0)
        {
            NSRect badgeRect;
            badgeRect.size = [fBadge size];
            badgeRect.origin.x = iconSize.width - badgeRect.size.width;
            badgeRect.origin.y = iconSize.height - badgeRect.size.height;
                                        
            [dockIcon lockFocus];
            
            //place badge
            [fBadge compositeToPoint: badgeRect.origin
                        operation: NSCompositeSourceOver];
            
            //ignore shadow of badge when placing string
            float badgeBottomExtra = 5.0;
            badgeRect.size.height -= badgeBottomExtra;
            badgeRect.origin.y += badgeBottomExtra;
            
            //place badge text
            [self badgeString: [NSString stringWithInt: completed]
                        forRect: badgeRect];
                        
            [dockIcon unlockFocus];
        }
        
        [fBadgedDockIcon release];
        fBadgedDockIcon = [dockIcon copy];
    }

    //display upload and download rates
    BOOL speedShown = NO;
    if (uploadRate || downloadRate)
    {
        speedShown = YES;
    
        NSRect badgeRect, stringRect;
        badgeRect.size = [fUploadBadge size];
        badgeRect.origin = NSZeroPoint;
        
        //ignore shadow of badge when placing string
        float badgeBottomExtra = 2.0;
        stringRect = badgeRect;
        stringRect.size.height -= badgeBottomExtra;
        stringRect.origin.y += badgeBottomExtra;

        if (!dockIcon)
            dockIcon = [fBadgedDockIcon copy];
        
        [dockIcon lockFocus];
        
        if (uploadRate)
        {
            //place badge
            [fUploadBadge compositeToPoint: badgeRect.origin
                        operation: NSCompositeSourceOver];
            
            //place badge text
            [self badgeString: uploadRate forRect: stringRect];
        }
        
        if (downloadRate)
        {
            //download rate above upload rate
            if (uploadRate)
            {
                float spaceBetween = badgeRect.size.height + 2.0;
                badgeRect.origin.y += spaceBetween;
                stringRect.origin.y += spaceBetween;
            }
        
            //place badge
            [fDownloadBadge compositeToPoint: badgeRect.origin
                        operation: NSCompositeSourceOver];
            
            //place badge text
            [self badgeString: downloadRate forRect: stringRect];
        }
        
        [dockIcon unlockFocus];
    }

    if (dockIcon || fSpeedShown)
    {
        if (!dockIcon)
            dockIcon = [fBadgedDockIcon copy];
            
        [NSApp setApplicationIconImage: dockIcon];
        
        [dockIcon release];
    }
    
    fSpeedShown = speedShown;
}

- (void) clearBadge
{
    [fBadgedDockIcon release];
    fBadgedDockIcon = [fDockIcon copy];

    [NSApp setApplicationIconImage: fDockIcon];
}

@end

@implementation Badger (Private)

//dock icon must have locked focus
- (void) badgeString: (NSString *) string forRect: (NSRect) rect
{
    NSSize stringSize = [string sizeWithAttributes: fAttributes];
    
    //string is in center of image
    rect.origin.x += (rect.size.width - stringSize.width) * 0.5;
    rect.origin.y += (rect.size.height - stringSize.height) * 0.5;
                        
    [string drawAtPoint: rect.origin withAttributes: fAttributes];
}

@end
