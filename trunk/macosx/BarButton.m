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

#import "BarButton.h"

@implementation BarButton

- (id) initWithCoder: (NSCoder *) coder
{
	if ((self = [super initWithCoder: coder]))
    {
        fEnabled = NO;
        
        NSSize buttonSize = [self frame].size;
        fButtonNormal = [[NSImage alloc] initWithSize: buttonSize];
        fButtonIn = [[NSImage alloc] initWithSize: buttonSize];
        fButtonDown = [[NSImage alloc] initWithSize: buttonSize];
        
        //create shape
        NSBezierPath * rect = [NSBezierPath bezierPath];
        float ovalDiamater = 20.0;
        [rect appendBezierPathWithOvalInRect:
                NSMakeRect(0, 0, ovalDiamater, buttonSize.height)];
        [rect appendBezierPathWithOvalInRect:
                NSMakeRect(buttonSize.width - ovalDiamater, 0, ovalDiamater, buttonSize.height)];
        [rect appendBezierPathWithRect:
                NSMakeRect(ovalDiamater * 0.5, 0, buttonSize.width - ovalDiamater, buttonSize.height)];
        
        //create highlighted button
        [fButtonIn lockFocus];
        [[NSColor colorWithCalibratedRed: 0.4941 green: 0.5647 blue: 0.6706 alpha: 1.0] set];
        [rect fill];
        [fButtonIn unlockFocus];
        
        //create pushed button
        [fButtonDown lockFocus];
        [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.6] set];
        [rect fill];
        [fButtonDown unlockFocus];
        
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(resetBounds:)
                    name: NSViewBoundsDidChangeNotification object: nil];
	}
	return self;
}

- (void) awakeFromNib
{
    fTrackingTag = [self addTrackingRect: [self bounds] owner: self userData: nil assumeInside: NO];
}

- (void) dealloc
{
    [fButtonNormal release];
    [fButtonIn release];
    [fButtonDown release];
    
    [super dealloc];
}

//call only once to avoid overlap
- (void) setText: (NSString *) text
{
    NSShadow * stringShadow = [[NSShadow alloc] init];
    [stringShadow setShadowOffset: NSMakeSize(1.0, -1.0)];
    [stringShadow setShadowBlurRadius: 1.0];

    NSDictionary * normalAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor blackColor], NSForegroundColorAttributeName,
                [NSFont messageFontOfSize: 11.0], NSFontAttributeName,
                stringShadow, NSShadowAttributeName, nil];
    NSDictionary * highlightedAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor whiteColor], NSForegroundColorAttributeName,
                [NSFont messageFontOfSize: 11.0], NSFontAttributeName,
                stringShadow, NSShadowAttributeName, nil];
    
    [stringShadow release];
    
    NSSize textSize = [text sizeWithAttributes: normalAttributes],
            buttonSize = [self frame].size;
    NSRect textRect = NSMakeRect((buttonSize.width - textSize.width) * 0.5,
            (buttonSize.height - textSize.height) * 0.5, textSize.width, textSize.height);
    
    //create normal button
    [fButtonNormal lockFocus];
    [text drawInRect: textRect withAttributes: normalAttributes];
    [fButtonNormal unlockFocus];
    
    //create highlighted button
    [fButtonIn lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonIn unlockFocus];
    
    //create pushed button
    [fButtonDown lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonDown unlockFocus];
    
    [self setImage: fButtonNormal];
    
    [normalAttributes release];
    [highlightedAttributes release];
}

- (void) mouseEntered: (NSEvent *) event
{
    if (!fEnabled)
        [self setImage: fButtonIn];

    [super mouseEntered: event];
}

- (void) mouseExited: (NSEvent *) event
{
    if (!fEnabled)
        [self setImage: fButtonNormal];

    [super mouseExited: event];
}

- (void) mouseDown: (NSEvent *) event
{
    [self setImage: fButtonDown];

    [super mouseDown: event];
    
    //mouse up after mouse down
    if (NSPointInRect([self convertPoint: [[self window] convertScreenToBase:
                                [NSEvent mouseLocation]] fromView: nil], [self bounds]))
        [NSApp sendAction: [self action] to: [self target] from: self];
    
    [self setImage: fButtonIn];
}

- (void) setEnabled: (BOOL) enable
{
    fEnabled = enable;
    [self setImage: fEnabled ? fButtonIn : fButtonNormal];
}

- (void) resetBounds: (NSNotification *) notification
{
    [self removeTrackingRect: fTrackingTag];
    fTrackingTag = [self addTrackingRect: [self bounds] owner: self userData: nil assumeInside: NO];
}

@end
