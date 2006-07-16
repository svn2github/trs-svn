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

//height of button should be made 17.0
- (id) initWithCoder: (NSCoder *) coder
{
	if ((self = [super initWithCoder: coder]))
    {
        fEnabled = NO;
        fTrackingTag = 0;
        
        NSSize buttonSize = [self frame].size;
        fButtonNormal = [[NSImage alloc] initWithSize: buttonSize];
        fButtonOver = [[NSImage alloc] initWithSize: buttonSize];
        fButtonPressed = [[NSImage alloc] initWithSize: buttonSize];
        fButtonSelected = [[NSImage alloc] initWithSize: buttonSize];
        
        //rolled over button
        NSImage * leftOver = [NSImage imageNamed: @"FilterButtonOverLeft.png"],
                * rightOver = [NSImage imageNamed: @"FilterButtonOverRight.png"],
                * mainOver = [NSImage imageNamed: @"FilterButtonOverMain.png"];
        
        float endWidth = [leftOver size].width,
                mainWidth = buttonSize.width - 2.0 * endWidth;
        NSPoint leftPoint = NSMakePoint(0, 0),
                mainPoint = NSMakePoint(endWidth, 0),
                rightPoint = NSMakePoint(mainWidth + endWidth, 0);
        NSSize mainSize = NSMakeSize(mainWidth, [leftOver size].height);

        [mainOver setScalesWhenResized: YES];
        [mainOver setSize: mainSize];
        
        [fButtonOver lockFocus];
        [leftOver compositeToPoint: leftPoint operation: NSCompositeSourceOver];
        [mainOver compositeToPoint: mainPoint operation: NSCompositeSourceOver];
        [rightOver compositeToPoint: rightPoint operation: NSCompositeSourceOver];
        [fButtonOver unlockFocus];
        
        //pressed button
        NSImage * leftPressed = [NSImage imageNamed: @"FilterButtonPressedLeft.png"],
                * rightPressed = [NSImage imageNamed: @"FilterButtonPressedRight.png"],
                * mainPressed = [NSImage imageNamed: @"FilterButtonPressedMain.png"];
        
        [mainPressed setScalesWhenResized: YES];
        [mainPressed setSize: mainSize];
        
        [fButtonPressed lockFocus];
        [leftPressed compositeToPoint: leftPoint operation: NSCompositeSourceOver];
        [mainPressed compositeToPoint: mainPoint operation: NSCompositeSourceOver];
        [rightPressed compositeToPoint: rightPoint operation: NSCompositeSourceOver];
        [fButtonPressed unlockFocus];
        
        //selected button
        NSImage * leftSelected = [NSImage imageNamed: @"FilterButtonSelectedLeft.png"],
                * rightSelected = [NSImage imageNamed: @"FilterButtonSelectedRight.png"],
                * mainSelected = [NSImage imageNamed: @"FilterButtonSelectedMain.png"];
        
        [mainSelected setScalesWhenResized: YES];
        [mainSelected setSize: mainSize];
        
        [fButtonSelected lockFocus];
        [leftSelected compositeToPoint: leftPoint operation: NSCompositeSourceOver];
        [mainSelected compositeToPoint: mainPoint operation: NSCompositeSourceOver];
        [rightSelected compositeToPoint: rightPoint operation: NSCompositeSourceOver];
        [fButtonSelected unlockFocus];
        
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(resetBounds:)
                    name: NSViewBoundsDidChangeNotification object: nil];
	}
	return self;
}

- (void) dealloc
{
    [fButtonNormal release];
    [fButtonOver release];
    [fButtonPressed release];
    [fButtonSelected release];
    
    [super dealloc];
}

//call only once to avoid overlapping text
- (void) setText: (NSString *) text
{
    NSShadow * stringShadow = [[NSShadow alloc] init];
    [stringShadow setShadowOffset: NSMakeSize(1.0, -1.0)];
    [stringShadow setShadowBlurRadius: 1.0];

    NSDictionary * normalAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor blackColor], NSForegroundColorAttributeName,
                [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                stringShadow, NSShadowAttributeName, nil];
    NSDictionary * highlightedAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                [NSColor whiteColor], NSForegroundColorAttributeName,
                [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                stringShadow, NSShadowAttributeName, nil];
    
    [stringShadow release];
    
    NSSize textSize = [text sizeWithAttributes: normalAttributes],
            buttonSize = [self frame].size;
    NSRect textRect = NSMakeRect((buttonSize.width - textSize.width) * 0.5,
            (buttonSize.height - textSize.height) * 0.5 + 1.5, textSize.width, textSize.height);
    
    //normal button
    [fButtonNormal lockFocus];
    [text drawInRect: textRect withAttributes: normalAttributes];
    [fButtonNormal unlockFocus];
    
    //rolled over button
    [fButtonOver lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonOver unlockFocus];
    
    //pressed button
    [fButtonPressed lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonPressed unlockFocus];
    
    //selected button
    [fButtonSelected lockFocus];
    [text drawInRect: textRect withAttributes: highlightedAttributes];
    [fButtonSelected unlockFocus];
    
    [self setImage: fButtonNormal];
    
    [normalAttributes release];
    [highlightedAttributes release];
}

- (void) mouseEntered: (NSEvent *) event
{
    if (!fEnabled)
        [self setImage: fButtonOver];

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
    [self setImage: fButtonPressed];

    [super mouseDown: event];
    
    //mouse up after mouse down
    if (NSPointInRect([self convertPoint: [[self window] convertScreenToBase:
                                [NSEvent mouseLocation]] fromView: nil], [self bounds]))
        [NSApp sendAction: [self action] to: [self target] from: self];
    
    [self setImage: fEnabled ? fButtonSelected : fButtonOver];
}

- (void) setEnabled: (BOOL) enable
{
    fEnabled = enable;
    [self setImage: fEnabled ? fButtonSelected : fButtonNormal];
}

- (void) resetBounds: (NSNotification *) notification
{
    if (fTrackingTag)
        [self removeTrackingRect: fTrackingTag];
    fTrackingTag = [self addTrackingRect: [self bounds] owner: self userData: nil assumeInside: NO];
}

@end
