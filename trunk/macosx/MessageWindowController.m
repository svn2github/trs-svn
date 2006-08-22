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

#import "MessageWindowController.h"
#import <transmission.h>

#define LEVEL_ERROR 0
#define LEVEL_INFO  1
#define LEVEL_DEBUG 2

#define UPDATE_SECONDS 0.35

@interface MessageWindowController (Private)

MessageWindowController * selfReference; //I'm not sure why "self" can't be used directly

@end

@implementation MessageWindowController

- (id) initWithWindowNibName: (NSString *) name
{
    if ((self = [super initWithWindowNibName: name]))
    {
        selfReference = self;
        
        fLock = [[NSLock alloc] init];
        fBufferArray = [[NSMutableArray alloc] init];
        
        fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self
                    selector: @selector(updateLog:) userInfo: nil repeats: YES];
        
        [[self window] update]; //make sure nib is loaded right away
    }
    return self;
}

- (void) dealloc
{
    [fTimer invalidate];
    
    tr_setMessageFunction(NULL);
    
    [fLock release];
    [fBufferArray release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    int level = [[NSUserDefaults standardUserDefaults] integerForKey: @"MessageLevel"];
    if (level == TR_MSG_ERR)
        [fLevelButton selectItemAtIndex: LEVEL_ERROR];
    else if (level == TR_MSG_INF)
        [fLevelButton selectItemAtIndex: LEVEL_INFO];
    else if (level == TR_MSG_DBG)
        [fLevelButton selectItemAtIndex: LEVEL_DEBUG];
    else
    {
        level = TR_MSG_ERR;
        [fLevelButton selectItemAtIndex: LEVEL_ERROR];
        [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
    }
    
    tr_setMessageLevel(level);
    tr_setMessageFunction(addMessage);
}

void addMessage(int level, const char * message)
{
    [selfReference addMessage: message level: level];
}

- (void) addMessage: (const char *) message level: (int) level
{
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

    NSString * levelString;

    if (level == TR_MSG_ERR)
        levelString = @"ERR";
    else if (level == TR_MSG_INF)
        levelString = @"INF";
    else if (level == TR_MSG_DBG)
        levelString = @"DBG";
    else
        levelString = @"???";
    
    NSAttributedString * messageString = [[[NSAttributedString alloc] initWithString:
            [NSString stringWithFormat: @"(%@ %@) %s\n", [[NSDate date] dateWithCalendarFormat: @"%Y-%m-%d %H:%M:%S.%F"
                                timeZone: nil], levelString, message]] autorelease];
    
    [fLock lock];
    [fBufferArray addObject: messageString];
    [fLock unlock];
    
    [pool release];
}

- (void) updateLog: (NSTimer *) timer
{
    if ([fBufferArray count] == 0)
        return;
    
    [fLock lock];
    
    //keep scrolled to bottom if already at bottom or there is no scroll bar yet
    BOOL shouldScroll = NO;
    NSScroller * scroller = [fScrollView verticalScroller];
    if ([scroller floatValue] == 1.0 || [scroller isHidden] || [scroller knobProportion] == 1.0)
        shouldScroll = YES;
    
    NSEnumerator * enumerator = [fBufferArray objectEnumerator];
    NSAttributedString * messageString;
    while ((messageString = [enumerator nextObject]))
        [[fTextView textStorage] appendAttributedString: messageString];
    [fBufferArray removeAllObjects];
    
    [fTextView setFont: [NSFont fontWithName: @"Monaco" size: 10]]; //find a way to set this permanently
    
    if (shouldScroll)
        [fTextView scrollRangeToVisible: NSMakeRange([[fTextView string] length], 0)];
    
    [fLock unlock];
}

- (void) changeLevel: (id) sender
{
    int selection = [fLevelButton indexOfSelectedItem], level;
    if (selection == LEVEL_INFO)
        level = TR_MSG_INF;
    else if (selection == LEVEL_DEBUG)
        level = TR_MSG_DBG;
    else
        level = TR_MSG_ERR;
    
    [self updateLog: nil];
    
    tr_setMessageLevel(level);
    [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
}

- (void) clearLog: (id) sender
{
    [fLock lock];
    [fTextView setString: @""];
    [fLock unlock];
}

@end
