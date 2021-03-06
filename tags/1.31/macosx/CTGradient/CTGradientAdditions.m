/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#import "CTGradientAdditions.h"

@implementation CTGradient (ProgressBar)

+ (CTGradient *) progressGradientForRed: (float) redComponent green: (float) greenComponent blue: (float) blueComponent
{
    CTGradientElement color1;
    color1.red = redComponent;
    color1.green = greenComponent;
    color1.blue = blueComponent;
    color1.alpha = 1.0;
    color1.position = 0.0;
    
    CTGradientElement color2;
    color2.red = redComponent * 0.95;
    color2.green = greenComponent * 0.95;
    color2.blue = blueComponent * 0.95;
    color2.alpha = 1.0;
    color2.position = 0.5;
    
    CTGradientElement color3;
    color3.red = redComponent * 0.85;
    color3.green = greenComponent * 0.85;
    color3.blue = blueComponent * 0.85;
    color3.alpha = 1.0;
    color3.position = 0.5;
    
    CTGradientElement color4;
    color4.red = redComponent;
    color4.green = greenComponent;
    color4.blue = blueComponent;
    color4.alpha = 1.0;
    color4.position = 1.0;
    
    CTGradient * newInstance = [[[self class] alloc] init];
    [newInstance addElement: &color1];
    [newInstance addElement: &color2];
    [newInstance addElement: &color3];
    [newInstance addElement: &color4];
    
    return [newInstance autorelease];
}

CTGradient * fProgressWhiteGradient = nil;
+ (CTGradient *)progressWhiteGradient
{
    if (!fProgressWhiteGradient)
        fProgressWhiteGradient = [[[self class] progressGradientForRed: 0.95 green: 0.95 blue: 0.95] retain];
    return fProgressWhiteGradient;
}

CTGradient * fProgressGrayGradient = nil;
+ (CTGradient *)progressGrayGradient
{
    if (!fProgressGrayGradient)
        fProgressGrayGradient = [[[self class] progressGradientForRed: 0.7 green: 0.7 blue: 0.7] retain];
    return fProgressGrayGradient;
}

CTGradient * fProgressLightGrayGradient = nil;
+ (CTGradient *)progressLightGrayGradient
{
    if (!fProgressLightGrayGradient)
        fProgressLightGrayGradient = [[[self class] progressGradientForRed: 0.87 green: 0.87 blue: 0.87] retain];
    return fProgressLightGrayGradient;
}

CTGradient * fProgressBlueGradient = nil;
+ (CTGradient *)progressBlueGradient
{
    if (!fProgressBlueGradient)
        fProgressBlueGradient = [[[self class] progressGradientForRed: 0.35 green: 0.67 blue: 0.98] retain];
    return fProgressBlueGradient;
}

CTGradient * fProgressDarkBlueGradient = nil;
+ (CTGradient *)progressDarkBlueGradient
{
    if (!fProgressDarkBlueGradient)
        fProgressDarkBlueGradient = [[[self class] progressGradientForRed: 0.616 green: 0.722 blue: 0.776] retain];
    return fProgressDarkBlueGradient;
}

CTGradient * fProgressGreenGradient = nil;
+ (CTGradient *)progressGreenGradient
{
    if (!fProgressGreenGradient)
        fProgressGreenGradient = [[[self class] progressGradientForRed: 0.44 green: 0.89 blue: 0.40] retain];
    return fProgressGreenGradient;
}

CTGradient * fProgressLightGreenGradient = nil;
+ (CTGradient *)progressLightGreenGradient
{
    if (!fProgressLightGreenGradient)
        fProgressLightGreenGradient = [[[self class] progressGradientForRed: 0.62 green: 0.99 blue: 0.58] retain];
    return fProgressLightGreenGradient;
}

CTGradient * fProgressDarkGreenGradient = nil;
+ (CTGradient *)progressDarkGreenGradient
{
    if (!fProgressDarkGreenGradient)
        fProgressDarkGreenGradient = [[[self class] progressGradientForRed: 0.627 green: 0.714 blue: 0.639] retain];
    return fProgressDarkGreenGradient;
}

CTGradient * fProgressRedGradient = nil;
+ (CTGradient *)progressRedGradient
{
    if (!fProgressRedGradient)
        fProgressRedGradient = [[[self class] progressGradientForRed: 0.902 green: 0.439 blue: 0.451] retain];
    return fProgressRedGradient;
}

CTGradient * fProgressYellowGradient = nil;
+ (CTGradient *)progressYellowGradient
{
    if (!fProgressYellowGradient)
        fProgressYellowGradient = [[[self class] progressGradientForRed: 0.933 green: 0.890 blue: 0.243] retain];
    return fProgressYellowGradient;
}

@end
