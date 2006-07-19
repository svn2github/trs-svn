/******************************************************************************
 * $Id$
 * 
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

#import <IOKit/IOMessage.h>

#import "Controller.h"
#import "Torrent.h"
#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "StringAdditions.h"

#import <Sparkle/Sparkle.h>
#import <Growl/GrowlApplicationBridge.h>

#define TOOLBAR_OPEN            @"Toolbar Open"
#define TOOLBAR_REMOVE          @"Toolbar Remove"
#define TOOLBAR_INFO            @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL       @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL      @"Toolbar Resume All"
#define TOOLBAR_PAUSE_SELECTED  @"Toolbar Pause Selected"
#define TOOLBAR_RESUME_SELECTED @"Toolbar Resume Selected"

#define TORRENT_TABLE_VIEW_DATA_TYPE    @"TorrentTableViewDataType"

#define ROW_HEIGHT_REGULAR 65.0
#define ROW_HEIGHT_SMALL 40.0

#define WEBSITE_URL @"http://transmission.m0k.org/"
#define FORUM_URL   @"http://transmission.m0k.org/forum/"

static void sleepCallBack(void * controller, io_service_t y,
        natural_t messageType, void * messageArgument)
{
    Controller * c = controller;
    [c sleepCallBack: messageType argument: messageArgument];
}


@implementation Controller

- (id) init
{
    if ((self = [super init]))
    {
        fLib = tr_init();
        
        fTorrents = [[NSMutableArray alloc] initWithCapacity: 10];
        fFilteredTorrents = [[NSMutableArray alloc] initWithCapacity: 10];
        
        fDefaults = [NSUserDefaults standardUserDefaults];
        fInfoController = [[InfoWindowController alloc] initWithWindowNibName: @"InfoWindow"];
        fPrefsController = [[PrefsController alloc] initWithWindowNibName: @"PrefsWindow"];
        fBadger = [[Badger alloc] init];
        
        [GrowlApplicationBridge setGrowlDelegate: self];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fTorrents release];
    [fFilteredTorrents release];
    
    [fToolbar release];
    [fInfoController release];
    [fPrefsController release];
    [fBadger release];
    
    [fSortType release];
    [fFilterType release];
    [fAutoImportedNames release];
    
    tr_close(fLib);
    [super dealloc];
}

- (void) awakeFromNib
{
    [fPrefsController setPrefs: fLib];
    
    #warning remove eventually
    NSImage * image = [NSImage imageNamed: @"StatusBorder.png"];
    [image setName: nil];
    [fStatusBar setBackgroundImage: [NSImage imageNamed: @"StatusBorder.png"]];
    [fFilterBar setBackgroundImage: [NSImage imageNamed: @"FilterBarBackground.png"]];
    
    [fWindow setAcceptsMouseMovedEvents: YES]; //ensure filter buttons display correctly
    
    [fAdvancedBarItem setState: [fDefaults boolForKey: @"UseAdvancedBar"]];

    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow setToolbar: fToolbar];
    [fWindow setDelegate: self];
    
    [fWindow makeFirstResponder: fTableView];
    
    //set table size
    if ([fDefaults boolForKey: @"SmallView"])
    {
        [fTableView setRowHeight: ROW_HEIGHT_SMALL];
        [fSmallViewItem setState: NSOnState];
    }
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize];
    contentMinSize.height = [[fWindow contentView] frame].size.height - [fScrollView frame].size.height
                                + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    //set info keyboard shortcuts
    unichar ch = NSRightArrowFunctionKey;
    [fNextInfoTabItem setKeyEquivalent: [NSString stringWithCharacters: & ch length: 1]];
    ch = NSLeftArrowFunctionKey;
    [fPrevInfoTabItem setKeyEquivalent: [NSString stringWithCharacters: & ch length: 1]];
    
    //set up filter bar
    NSView * contentView = [fWindow contentView];
    [fHideFilterButton setToolTip: @"Hide Filter Bar"];
    [fFilterBar setHidden: YES];
    
    fFilterBarVisible = NO;
    NSRect filterBarFrame = [fFilterBar frame];
    filterBarFrame.size.width = [fWindow frame].size.width;
    [fFilterBar setFrame: filterBarFrame];
    
    [contentView addSubview: fFilterBar];
    [fFilterBar setFrameOrigin: NSMakePoint(0, [contentView frame].origin.y + [contentView frame].size.height)];
    
    [self showFilterBar: [fDefaults boolForKey: @"FilterBar"] animate: NO];
    
    //set up status bar
    fStatusBarVisible = NO;
    [fShowFilterButton setToolTip: @"Show Filter Bar"];
    [fStatusBar setHidden: YES];
    
    NSRect statusBarFrame = [fStatusBar frame];
    statusBarFrame.size.width = [fWindow frame].size.width;
    [fStatusBar setFrame: statusBarFrame];
    
    [contentView addSubview: fStatusBar];
    [fStatusBar setFrameOrigin: NSMakePoint(0, [contentView frame].origin.y + [contentView frame].size.height)];
    [self showStatusBar: [fDefaults boolForKey: @"StatusBar"] animate: NO];
    
    //set speed limit
    fSpeedLimitNormalImage = [fSpeedLimitButton image];
    fSpeedLimitBlueImage = [NSImage imageNamed: @"SpeedLimitButtonBlue.png"];
    fSpeedLimitGraphiteImage = [NSImage imageNamed: @"SpeedLimitButtonGraphite.png"];
    
    [self updateControlTint: nil];
    
    if ((fSpeedLimitEnabled = [fDefaults boolForKey: @"SpeedLimit"]))
    {
        [fSpeedLimitItem setState: NSOnState];
        [fSpeedLimitDockItem setState: NSOnState];
        
        [fSpeedLimitButton setImage: [NSColor currentControlTint] == NSBlueControlTint
                                        ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage];
    }

    [fActionButton setToolTip: @"Shortcuts for changing global settings."];
    [fSpeedLimitButton setToolTip: @"Speed Limit overrides the total bandwidth limits with its own limits."];

    [fTableView setTorrents: fFilteredTorrents];
    [[fTableView tableColumnWithIdentifier: @"Torrent"] setDataCell: [[TorrentCell alloc] init]];

    [fTableView registerForDraggedTypes: [NSArray arrayWithObjects: NSFilenamesPboardType,
                                                        TORRENT_TABLE_VIEW_DATA_TYPE, nil]];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if (fRootPort = IORegisterForSystemPower(self, & notify, sleepCallBack, & iterator))
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify),
                            kCFRunLoopCommonModes);
    else
        NSLog(@"Could not IORegisterForSystemPower");

    //load torrents from history
    Torrent * torrent;
    NSDictionary * historyItem;
    NSEnumerator * enumerator = [[fDefaults arrayForKey: @"History"] objectEnumerator];
    while ((historyItem = [enumerator nextObject]))
        if ((torrent = [[Torrent alloc] initWithHistory: historyItem lib: fLib]))
        {
            [fTorrents addObject: torrent];
            [torrent release];
        }
    
    [self torrentNumberChanged];
    
    //set sort
    fSortType = [[fDefaults stringForKey: @"Sort"] retain];
    
    NSMenuItem * currentSortItem;
    if ([fSortType isEqualToString: @"Name"])
        currentSortItem = fNameSortItem;
    else if ([fSortType isEqualToString: @"State"])
        currentSortItem = fStateSortItem;
    else if ([fSortType isEqualToString: @"Progress"])
        currentSortItem = fProgressSortItem;
    else if ([fSortType isEqualToString: @"Date"])
        currentSortItem = fDateSortItem;
    else
        currentSortItem = fOrderSortItem;
    [currentSortItem setState: NSOnState];
    
    //set filter
    fFilterType = [[fDefaults stringForKey: @"Filter"] retain];

    //button width should be 9 pixels surrounding text length
    [fNoFilterButton setText: @"All"]; //16.64
    [fDownloadFilterButton setText: @"Downloading"]; //81.69
    [fSeedFilterButton setText: @"Seeding"]; //48.57
    [fPauseFilterButton setText: @"Paused"]; //44.06

    BarButton * currentFilterButton;
    if ([fFilterType isEqualToString: @"Pause"])
        currentFilterButton = fPauseFilterButton;
    else if ([fFilterType isEqualToString: @"Seed"])
        currentFilterButton = fSeedFilterButton;
    else if ([fFilterType isEqualToString: @"Download"])
        currentFilterButton = fDownloadFilterButton;
    else
        currentFilterButton = fNoFilterButton;

    [currentFilterButton setEnabled: YES];
    
    //set upload limit action button
    [fUploadLimitItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)",
                    [fDefaults integerForKey: @"UploadLimit"]]];
    if ([fDefaults boolForKey: @"CheckUpload"])
        [fUploadLimitItem setState: NSOnState];
    else
        [fUploadNoLimitItem setState: NSOnState];

	//set download limit action menu
    [fDownloadLimitItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)",
                    [fDefaults integerForKey: @"DownloadLimit"]]];
    if ([fDefaults boolForKey: @"CheckDownload"])
        [fDownloadLimitItem setState: NSOnState];
    else
        [fDownloadNoLimitItem setState: NSOnState];
    
    //set ratio action menu
    [fRatioSetItem setTitle: [NSString stringWithFormat: @"Stop at Ratio (%.2f)",
                                [fDefaults floatForKey: @"RatioLimit"]]];
    if ([fDefaults boolForKey: @"RatioCheck"])
        [fRatioSetItem setState: NSOnState];
    else
        [fRatioNotSetItem setState: NSOnState];
    
    //observe notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    
    [nc addObserver: self selector: @selector(updateControlTint:)
                    name: NSControlTintDidChangeNotification object: nil];
    
    [nc addObserver: self selector: @selector(prepareForUpdate:)
                    name: SUUpdaterWillRestartNotification object: nil];
    fUpdateInProgress = NO;
    
    [nc addObserver: self selector: @selector(limitGlobalChange:)
                    name: @"LimitGlobalChange" object: nil];
    
    [nc addObserver: self selector: @selector(ratioGlobalChange:)
                    name: @"RatioGlobalChange" object: nil];
    
    //check to start another because of stopped torrent
    [nc addObserver: self selector: @selector(checkWaitingForStopped:)
                    name: @"StoppedDownloading" object: nil];
    
    //check all torrents for starting
    [nc addObserver: self selector: @selector(globalStartSettingChange:)
                    name: @"GlobalStartSettingChange" object: nil];

    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStartSettingChange:)
                    name: @"TorrentStartSettingChange" object: nil];
    
    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStoppedForRatio:)
                    name: @"TorrentStoppedForRatio" object: nil];
    
    //change that just impacts the inspector
    [nc addObserver: self selector: @selector(reloadInspectorSettings:)
                    name: @"TorrentSettingChange" object: nil];
    
    //reset auto import
    [nc addObserver: self selector: @selector(autoImportChange:)
                    name: @"AutoImportSettingChange" object: nil];

    //timer to update the interface every second
    fCompleted = 0;
    [self updateUI: nil];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0 target: self
        selector: @selector(updateUI:) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
    
    //timer for auto import, will check every 15 seconds
    fAutoImportedNames = [[NSMutableArray alloc] init];
    
    [self checkAutoImportDirectory: nil];
    fAutoImportTimer = [NSTimer scheduledTimerWithTimeInterval: 15.0 target: self 
        selector: @selector(checkAutoImportDirectory:) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fAutoImportTimer forMode: NSDefaultRunLoopMode];
    
    [self applyFilter: nil];
    
    [fWindow makeKeyAndOrderFront: nil];

    //load info for no torrents
    [fInfoController updateInfoForTorrents: [NSArray array]];
    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app hasVisibleWindows: (BOOL) visibleWindows
{
    if (![fWindow isVisible] && ![[fPrefsController window] isVisible])
        [fWindow makeKeyAndOrderFront: nil];
    return NO;
}

- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication *) sender
{
    if (!fUpdateInProgress && [fDefaults boolForKey: @"CheckQuit"])
    {
        int active = 0, downloading = 0;
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
            {
                active++;
                if (![torrent isSeeding])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckQuitDownloading"] ? downloading > 0 : active > 0)
        {
            NSString * message = active == 1
                ? @"There is an active transfer. Do you really want to quit?"
                : [NSString stringWithFormat:
                    @"There are %d active transfers. Do you really want to quit?", active];

            NSBeginAlertSheet(@"Confirm Quit", @"Quit", @"Cancel", nil, fWindow, self,
                                @selector(quitSheetDidEnd:returnCode:contextInfo:),
                                nil, nil, message);
            return NSTerminateLater;
        }
    }

    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
    contextInfo: (void *) contextInfo
{
    [NSApp stopModal];
    [NSApp replyToApplicationShouldTerminate: returnCode == NSAlertDefaultReturn];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    //stop timers
    [fAutoImportTimer invalidate];
    [fTimer invalidate];
    
    //save history and stop running torrents
    [self updateTorrentHistory];
    [fTorrents makeObjectsPerformSelector: @selector(stopTransferForQuit)];
    
    //remember window states and close all windows
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];
    [[NSApp windows] makeObjectsPerformSelector: @selector(close)];
    [self showStatusBar: NO animate: NO];
    [self showFilterBar: NO animate: NO];
    
    //clear badge
    [fBadger clearBadge];

    //end quickly if the app is updating
    if (fUpdateInProgress)
        return;

    //wait for running transfers to stop (5 second timeout)
    NSDate * start = [NSDate date];
    BOOL timeUp = NO;
    
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while (!timeUp && (torrent = [enumerator nextObject]))
        while (![torrent isPaused] && !(timeUp = [start timeIntervalSinceNow] < -5.0))
        {
            usleep(100000);
            [torrent update];
        }
}

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code
    contextInfo: (Torrent *) torrent
{
    if (code == NSOKButton)
    {
        [torrent setDownloadFolder: [[openPanel filenames] objectAtIndex: 0]];
        [torrent update];
        [self attemptToStartAuto: torrent];
        
        [fTorrents addObject: torrent];
        
        [self torrentNumberChanged];
    }
    
    [NSApp stopModal];
}

- (void) application: (NSApplication *) sender openFiles: (NSArray *) filenames
{
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"], * torrentPath;
    Torrent * torrent;
    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib]))
            continue;

        //add it to the "File > Open Recent" menu
        [[NSDocumentController sharedDocumentController]
            noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

        if ([downloadChoice isEqualToString: @"Ask"])
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];

            [panel setPrompt: @"Select Download Folder"];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];

            [panel setMessage: [NSString stringWithFormat: @"Select the download folder for \"%@\"", [torrent name]]];

            [panel beginSheetForDirectory: nil file: nil types: nil
                modalForWindow: fWindow modalDelegate: self didEndSelector:
                @selector( folderChoiceClosed:returnCode:contextInfo: )
                contextInfo: torrent];
            [NSApp runModalForWindow: panel];
        }
        else
        {
            NSString * folder = [downloadChoice isEqualToString: @"Constant"]
                ? [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath]
                : [torrentPath stringByDeletingLastPathComponent];

            [torrent setDownloadFolder: folder];
            [torrent update];
            [self attemptToStartAuto: torrent];
            
            [fTorrents addObject: torrent];
        }
        
        [torrent release];
    }

    [self torrentNumberChanged];

    [self updateUI: nil];
    [self applyFilter: nil];
    [self updateTorrentHistory];
}

- (NSArray *) torrentsAtIndexes: (NSIndexSet *) indexSet
{
    if ([fFilteredTorrents respondsToSelector: @selector(objectsAtIndexes:)])
        return [fFilteredTorrents objectsAtIndexes: indexSet];
    else
    {
        NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [indexSet count]];
        unsigned int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [torrents addObject: [fFilteredTorrents objectAtIndex: i]];

        return torrents;
    }
}

- (void) torrentNumberChanged
{
    int count = [fTorrents count];
    [fTotalTorrentsField setStringValue: [NSString stringWithFormat:
        @"%d Transfer%s", count, count == 1 ? "" : "s"]];
}

//called on by applescript
- (void) open: (NSArray *) files
{
    [self performSelectorOnMainThread: @selector(openFromSheet:) withObject: files waitUntilDone: NO];
}

- (void) openShowSheet: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];

    [panel beginSheetForDirectory: nil file: nil types: [NSArray arrayWithObject: @"torrent"]
        modalForWindow: fWindow modalDelegate: self didEndSelector:
        @selector(openSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) openSheetClosed: (NSOpenPanel *) panel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
        [self performSelectorOnMainThread: @selector(openFromSheet:)
                    withObject: [panel filenames] waitUntilDone: NO];
}

- (void) openFromSheet: (NSArray *) filenames
{
    [self application: NSApp openFiles: filenames];
}

- (void) resumeSelectedTorrents: (id) sender
{
    [self resumeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (void) resumeAllTorrents: (id) sender
{
    [self resumeTorrents: fTorrents];
}

- (void) resumeTorrents: (NSArray *) torrents
{
    [torrents makeObjectsPerformSelector: @selector(startTransfer)];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) stopSelectedTorrents: (id) sender
{
    [self stopTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (void) stopAllTorrents: (id) sender
{
    [self stopTorrents: fTorrents];
}

- (void) stopTorrents: (NSArray *) torrents
{
    //don't want any of these starting then stopping
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];

    [torrents makeObjectsPerformSelector: @selector(stopTransfer)];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) removeTorrents: (NSArray *) torrents
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    [torrents retain];
    int active = 0, downloading = 0;

    if ([fDefaults boolForKey: @"CheckRemove"])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [torrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
            {
                active++;
                if (![torrent isSeeding])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckRemoveDownloading"] ? downloading > 0 : active > 0)
        {
            NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                torrents, @"Torrents",
                [NSNumber numberWithBool: deleteData], @"DeleteData",
                [NSNumber numberWithBool: deleteTorrent], @"DeleteTorrent", nil];

            NSString * title, * message;
            
            int selected = [fTableView numberOfSelectedRows];
            if (selected == 1)
            {
                title = [NSString stringWithFormat: @"Comfirm Removal of \"%@\"",
                            [[fFilteredTorrents objectAtIndex: [fTableView selectedRow]] name]];
                message = @"This transfer is active."
                            " Once removed, continuing the transfer will require the torrent file."
                            " Do you really want to remove it?";
            }
            else
            {
                title = [NSString stringWithFormat: @"Comfirm Removal of %d Transfers", selected];
                if (selected == active)
                    message = [NSString stringWithFormat:
                        @"There are %d active transfers.", active];
                else
                    message = [NSString stringWithFormat:
                        @"There are %d transfers (%d active).", selected, active];
                message = [message stringByAppendingString:
                    @" Once removed, continuing the transfers will require the torrent files."
                    " Do you really want to remove them?"];
            }

            NSBeginAlertSheet(title, @"Remove", @"Cancel", nil, fWindow, self,
                @selector(removeSheetDidEnd:returnCode:contextInfo:), nil, dict, message);
            return;
        }
    }
    
    [self confirmRemoveTorrents: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (NSDictionary *) dict
{
    [NSApp stopModal];

    NSArray * torrents = [dict objectForKey: @"Torrents"];
    BOOL deleteData = [[dict objectForKey: @"DeleteData"] boolValue],
        deleteTorrent = [[dict objectForKey: @"DeleteTorrent"] boolValue];
    [dict release];
    
    if (returnCode == NSAlertDefaultReturn)
        [self confirmRemoveTorrents: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
    else
        [torrents release];
}

- (void) confirmRemoveTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    //don't want any of these starting then stopping
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];

    NSNumber * lowestOrderValue = [NSNumber numberWithInt: [torrents count]], * currentOrderValue;

    enumerator = [torrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        [torrent stopTransfer];

        if (deleteData)
            [torrent trashData];
        if (deleteTorrent)
            [torrent trashTorrent];
        
        //determine lowest order value
        currentOrderValue = [torrent orderValue];
        if ([lowestOrderValue compare: currentOrderValue] == NSOrderedDescending)
            lowestOrderValue = currentOrderValue;

        [torrent removeForever];
        
        [fFilteredTorrents removeObject: torrent];
        [fTorrents removeObject: torrent];
    }
    [torrents release];

    //reset the order values if necessary
    if ([lowestOrderValue intValue] < [fTorrents count])
    {
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"orderValue" ascending: YES] autorelease];
        NSArray * descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];

        NSArray * tempTorrents = [fTorrents sortedArrayUsingDescriptors: descriptors];
        [descriptors release];

        int i;
        for (i = [lowestOrderValue intValue]; i < [tempTorrents count]; i++)
            [[tempTorrents objectAtIndex: i] setOrderValue: i];
    }
    
    [self torrentNumberChanged];
    [fTableView deselectAll: nil];
    [self updateUI: nil];
    [self updateTorrentHistory];
}

- (void) removeNoDelete: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: NO deleteTorrent: NO];
}

- (void) removeDeleteData: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: YES deleteTorrent: NO];
}

- (void) removeDeleteTorrent: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: NO deleteTorrent: YES];
}

- (void) removeDeleteDataAndTorrent: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: YES deleteTorrent: YES];
}

- (void) copyTorrentFile: (id) sender
{
    [self copyTorrentFileForTorrents: [[NSMutableArray alloc] initWithArray:
            [self torrentsAtIndexes: [fTableView selectedRowIndexes]]]];
}

- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents
{
    if ([torrents count] == 0)
    {
        [torrents release];
        return;
    }

    Torrent * torrent = [torrents objectAtIndex: 0];

    //warn user if torrent file can't be found
    if (![[NSFileManager defaultManager] fileExistsAtPath: [torrent torrentLocation]])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: @"OK"];
        [alert setMessageText: [NSString stringWithFormat: 
                @"Copy of \"%@\" Cannot Be Created", [torrent name]]];
        [alert setInformativeText: [NSString stringWithFormat: 
                @"The torrent file (%@) cannot be found.", [torrent torrentLocation]]];
        [alert setAlertStyle: NSWarningAlertStyle];
        
        [alert runModal];
        
        [torrents removeObjectAtIndex: 0];
        [self copyTorrentFileForTorrents: torrents];
    }
    else
    {
        NSSavePanel * panel = [NSSavePanel savePanel];
        [panel setRequiredFileType: @"torrent"];
        [panel setCanSelectHiddenExtension: YES];
        
        [panel beginSheetForDirectory: nil file: [torrent name]
            modalForWindow: fWindow modalDelegate: self didEndSelector:
            @selector(saveTorrentCopySheetClosed:returnCode:contextInfo:) contextInfo: torrents];
    }
}

- (void) saveTorrentCopySheetClosed: (NSSavePanel *) panel returnCode: (int) code
    contextInfo: (NSMutableArray *) torrents
{
    //if save successful, copy torrent to new location with name of data file
    if (code == NSOKButton)
        [[NSFileManager defaultManager] copyPath: [[torrents objectAtIndex: 0] torrentLocation]
                toPath: [panel filename] handler: nil];
    
    [torrents removeObjectAtIndex: 0];
    [self performSelectorOnMainThread: @selector(copyTorrentFileForTorrents:)
                withObject: torrents waitUntilDone: NO];
}

- (void) revealFile: (id) sender
{
    NSIndexSet * indexSet = [fTableView selectedRowIndexes];
    unsigned int i;
    
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [[fFilteredTorrents objectAtIndex: i] revealData];
}

- (void) showPreferenceWindow: (id) sender
{
    NSWindow * window = [fPrefsController window];
    if (![window isVisible])
        [window center];

    [window makeKeyAndOrderFront: nil];
}

- (void) showInfo: (id) sender
{
    if ([[fInfoController window] isVisible])
        [fInfoController close];
    else
    {
        [fInfoController updateInfoStats];
        [[fInfoController window] orderFront: nil];
    }
}

- (void) setInfoTab: (id) sender
{
    if (sender == fNextInfoTabItem)
        [fInfoController setNextTab];
    else
        [fInfoController setPreviousTab];
}

- (void) updateControlTint: (NSNotification *) notification
{
    if (fSpeedLimitEnabled && [fWindow isKeyWindow])
        [fSpeedLimitButton setImage: [NSColor currentControlTint] == NSBlueControlTint
                                        ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage];
}

- (void) updateUI: (NSTimer *) t
{
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
    {
        [torrent update];

        if ([torrent justFinished])
        {
            [self applyFilter: nil];
            [self checkToStartWaiting: torrent];
        
            [GrowlApplicationBridge notifyWithTitle: @"Download Complete"
                description: [torrent name] notificationName: @"Download Complete" iconData: nil
                priority: 0 isSticky: NO clickContext: nil];

            if (![fWindow isKeyWindow])
                fCompleted++;
        }
    }

    if ([fSortType isEqualToString: @"Progress"] || [fSortType isEqualToString: @"State"])
        [self sortTorrents];
    else
        [fTableView reloadData];
    
    //update the global DL/UL rates
    float downloadRate, uploadRate;
    tr_torrentRates(fLib, & downloadRate, & uploadRate);
    if (fStatusBarVisible)
    {
        [fTotalDLField setStringValue: [@"Total DL: " stringByAppendingString: [NSString stringForSpeed: downloadRate]]];
        [fTotalULField setStringValue: [@"Total UL: " stringByAppendingString: [NSString stringForSpeed: uploadRate]]];
    }

    if ([[fInfoController window] isVisible])
        [fInfoController updateInfoStats];

    //badge dock
    [fBadger updateBadgeWithCompleted: fCompleted
        uploadRate: uploadRate downloadRate: downloadRate];
}

- (void) updateTorrentHistory
{
    NSMutableArray * history = [NSMutableArray arrayWithCapacity: [fTorrents count]];

    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [history addObject: [torrent history]];

    [fDefaults setObject: history forKey: @"History"];
    [fDefaults synchronize];
}

- (void) sortTorrents
{
    //remember selected rows if needed
    NSArray * selectedTorrents = nil;
    int numSelected = [fTableView numberOfSelectedRows];
    if (numSelected > 0 && numSelected < [fFilteredTorrents count])
        selectedTorrents = [self torrentsAtIndexes: [fTableView selectedRowIndexes]];

    [self sortTorrentsIgnoreSelected]; //actually sort
    
    //set selected rows if needed
    if (selectedTorrents)
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
        NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
        while ((torrent = [enumerator nextObject]))
            [indexSet addIndex: [fFilteredTorrents indexOfObject: torrent]];
        
        [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
        [indexSet release];
    }
}

//doesn't remember selected rows
- (void) sortTorrentsIgnoreSelected
{
    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"name" ascending: YES] autorelease],
                    * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"orderValue" ascending: YES] autorelease];

    NSArray * descriptors;
    if ([fSortType isEqualToString: @"Name"])
        descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, orderDescriptor, nil];
    else if ([fSortType isEqualToString: @"State"])
    {
        NSSortDescriptor * stateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"stateSortKey" ascending: NO] autorelease],
                        * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"progressSortKey" ascending: NO] autorelease];
        
        descriptors = [[NSArray alloc] initWithObjects: stateDescriptor, progressDescriptor,
                                                            nameDescriptor, orderDescriptor, nil];
    }
    else if ([fSortType isEqualToString: @"Progress"])
    {
        NSSortDescriptor * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"progressSortKey" ascending: YES] autorelease];
        
        descriptors = [[NSArray alloc] initWithObjects: progressDescriptor, nameDescriptor, orderDescriptor, nil];
    }
    else if ([fSortType isEqualToString: @"Date"])
    {
        NSSortDescriptor * dateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"date" ascending: YES] autorelease];
    
        descriptors = [[NSArray alloc] initWithObjects: dateDescriptor, orderDescriptor, nil];
    }
    else
        descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];

    [fFilteredTorrents sortUsingDescriptors: descriptors];
    [descriptors release];
    
    [fTableView reloadData];
}

- (void) setSort: (id) sender
{
    NSMenuItem * prevSortItem;
    if ([fSortType isEqualToString: @"Name"])
        prevSortItem = fNameSortItem;
    else if ([fSortType isEqualToString: @"State"])
        prevSortItem = fStateSortItem;
    else if ([fSortType isEqualToString: @"Progress"])
        prevSortItem = fProgressSortItem;
    else if ([fSortType isEqualToString: @"Date"])
        prevSortItem = fDateSortItem;
    else
        prevSortItem = fOrderSortItem;
    
    if (sender != prevSortItem)
    {
        [prevSortItem setState: NSOffState];
        [sender setState: NSOnState];

        [fSortType release];
        if (sender == fNameSortItem)
            fSortType = [[NSString alloc] initWithString: @"Name"];
        else if (sender == fStateSortItem)
            fSortType = [[NSString alloc] initWithString: @"State"];
        else if (sender == fProgressSortItem)
            fSortType = [[NSString alloc] initWithString: @"Progress"];
        else if (sender == fDateSortItem)
            fSortType = [[NSString alloc] initWithString: @"Date"];
        else
            fSortType = [[NSString alloc] initWithString: @"Order"];
           
        [fDefaults setObject: fSortType forKey: @"Sort"];
    }

    [self sortTorrents];
}

- (void) applyFilter: (id) sender
{
    //remember selected rows if needed
    NSArray * selectedTorrents = [fTableView numberOfSelectedRows] > 0
                ? [self torrentsAtIndexes: [fTableView selectedRowIndexes]] : nil;

    NSMutableArray * tempTorrents = [[NSMutableArray alloc] initWithCapacity: [fTorrents count]];

    if ([fFilterType isEqualToString: @"Pause"])
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive])
                [tempTorrents addObject: torrent];
    }
    else if ([fFilterType isEqualToString: @"Seed"])
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] && [torrent progress] >= 1.0)
                [tempTorrents addObject: torrent];
    }
    else if ([fFilterType isEqualToString: @"Download"])
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] && [torrent progress] < 1.0)
                [tempTorrents addObject: torrent];
    }
    else
        [tempTorrents setArray: fTorrents];
    
    NSString * searchString = [fSearchFilterField stringValue];
    if (![searchString isEqualToString: @""])
    {
        int i;
        for (i = [tempTorrents count] - 1; i >= 0; i--)
            if ([[[tempTorrents objectAtIndex: i] name] rangeOfString: searchString
                                        options: NSCaseInsensitiveSearch].location == NSNotFound)
                [tempTorrents removeObjectAtIndex: i];
    }
    
    [fFilteredTorrents setArray: tempTorrents];
    [tempTorrents release];
    
    [self sortTorrentsIgnoreSelected];
    
    //set selected rows if needed
    if (selectedTorrents)
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
        NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
        unsigned index;
        while ((torrent = [enumerator nextObject]))
            if ((index = [fFilteredTorrents indexOfObject: torrent]) != NSNotFound)
                [indexSet addIndex: index];
        
        [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
        [indexSet release];
    }
}

//resets filter and sorts torrents
- (void) setFilter: (id) sender
{
    BarButton * prevFilterButton;
    if ([fFilterType isEqualToString: @"Pause"])
        prevFilterButton = fPauseFilterButton;
    else if ([fFilterType isEqualToString: @"Seed"])
        prevFilterButton = fSeedFilterButton;
    else if ([fFilterType isEqualToString: @"Download"])
        prevFilterButton = fDownloadFilterButton;
    else
        prevFilterButton = fNoFilterButton;
    
    if (sender != prevFilterButton)
    {
        [prevFilterButton setEnabled: NO];
        [sender setEnabled: YES];

        [fFilterType release];
        if (sender == fDownloadFilterButton)
            fFilterType = [[NSString alloc] initWithString: @"Download"];
        else if (sender == fPauseFilterButton)
            fFilterType = [[NSString alloc] initWithString: @"Pause"];
        else if (sender == fSeedFilterButton)
            fFilterType = [[NSString alloc] initWithString: @"Seed"];
        else
            fFilterType = [[NSString alloc] initWithString: @"None"];

        [fDefaults setObject: fFilterType forKey: @"Filter"];
    }

    [self applyFilter: nil];
}

- (void) toggleSpeedLimit: (id) sender
{
    fSpeedLimitEnabled = !fSpeedLimitEnabled;
    int state = fSpeedLimitEnabled ? NSOnState : NSOffState;

    [fSpeedLimitItem setState: state];
    [fSpeedLimitDockItem setState: state];
    
    [fSpeedLimitButton setImage: !fSpeedLimitEnabled ? fSpeedLimitNormalImage
        : ([NSColor currentControlTint] == NSBlueControlTint ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage)];
    
    [fPrefsController enableSpeedLimit: fSpeedLimitEnabled];
}

- (void) setLimitGlobalEnabled: (id) sender
{
    [fPrefsController setLimitEnabled: (sender == fUploadLimitItem || sender == fDownloadLimitItem)
        type: (sender == fUploadLimitItem || sender == fUploadNoLimitItem) ? @"Upload" : @"Download"];
}

- (void) setQuickLimitGlobal: (id) sender
{
    [fPrefsController setQuickLimit: [[sender title] intValue]
        type: [sender menu] == fUploadMenu ? @"Upload" : @"Download"];
}

- (void) limitGlobalChange: (NSNotification *) notification
{
    NSDictionary * dict = [notification object];
    
    NSMenuItem * limitItem, * noLimitItem;
    if ([[dict objectForKey: @"Type"] isEqualToString: @"Upload"])
    {
        limitItem = fUploadLimitItem;
        noLimitItem = fUploadNoLimitItem;
    }
    else
    {
        limitItem = fDownloadLimitItem;
        noLimitItem = fDownloadNoLimitItem;
    }
    
    BOOL enable = [[dict objectForKey: @"Enable"] boolValue];
    [limitItem setState: enable ? NSOnState : NSOffState];
    [noLimitItem setState: !enable ? NSOnState : NSOffState];
    
    [limitItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)",
                            [[dict objectForKey: @"Limit"] intValue]]];

    [dict release];
}

- (void) setRatioGlobalEnabled: (id) sender
{
    [fPrefsController setRatioEnabled: sender == fRatioSetItem];
}

- (void) setQuickRatioGlobal: (id) sender
{
    [fPrefsController setQuickRatio: [[sender title] floatValue]];
}

- (void) ratioGlobalChange: (NSNotification *) notification
{
    NSDictionary * dict = [notification object];
    
    BOOL enable = [[dict objectForKey: @"Enable"] boolValue];
    [fRatioSetItem setState: enable ? NSOnState : NSOffState];
    [fRatioNotSetItem setState: !enable ? NSOnState : NSOffState];
    
    [fRatioSetItem setTitle: [NSString stringWithFormat: @"Stop at Ratio (%.2f)",
                            [[dict objectForKey: @"Ratio"] floatValue]]];

    [dict release];
}

- (void) checkWaitingForStopped: (NSNotification *) notification
{
    [self checkToStartWaiting: [notification object]];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) checkToStartWaiting: (Torrent *) finishedTorrent
{
    //don't try to start a transfer if there should be none waiting
    if (![[fDefaults stringForKey: @"StartSetting"] isEqualToString: @"Wait"])
        return;

    int desiredActive = [fDefaults integerForKey: @"WaitToStartNumber"];
    
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent, * torrentToStart = nil;
    while ((torrent = [enumerator nextObject]))
    {
        //ignore the torrent just stopped
        if (torrent == finishedTorrent)
            continue;
    
        if ([torrent isActive])
        {
            if (![torrent isSeeding])
            {
                desiredActive--;
                if (desiredActive <= 0)
                    return;
            }
        }
        else
        {
            //use as next if it is waiting to start and either no previous or order value is lower
            if ([torrent waitingToStart] && (!torrentToStart
                || [[torrentToStart orderValue] compare: [torrent orderValue]] == NSOrderedDescending))
                torrentToStart = torrent;
        }
    }
    
    //since it hasn't returned, the queue amount has not been met
    if (torrentToStart)
    {
        [torrentToStart startTransfer];
        
        [self updateUI: nil];
        [self applyFilter: nil];
        [fInfoController updateInfoStatsAndSettings];
        [self updateTorrentHistory];
    }
}

- (void) torrentStartSettingChange: (NSNotification *) notification
{
    [self attemptToStartMultipleAuto: [notification object]];

    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) globalStartSettingChange: (NSNotification *) notification
{
    [self attemptToStartMultipleAuto: fTorrents];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) torrentStoppedForRatio: (NSNotification *) notification
{
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    
    [GrowlApplicationBridge notifyWithTitle: @"Seeding Complete"
                description: [[notification object] name] notificationName: @"Seeding Complete"
                iconData: nil priority: 0 isSticky: NO clickContext: nil];
}

- (void) attemptToStartAuto: (Torrent *) torrent
{
    [self attemptToStartMultipleAuto: [NSArray arrayWithObject: torrent]];
}

//will try to start, taking into consideration the start preference
- (void) attemptToStartMultipleAuto: (NSArray *) torrents
{
    NSString * startSetting = [fDefaults stringForKey: @"StartSetting"];
    if ([startSetting isEqualToString: @"Start"])
    {
        NSEnumerator * enumerator = [torrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent waitingToStart])
                [torrent startTransfer];
        
        return;
    }
    else if (![startSetting isEqualToString: @"Wait"])
        return;
    else;
    
    //determine the number of downloads needed to start
    int desiredActive = [fDefaults integerForKey: @"WaitToStartNumber"];
            
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if ([torrent isActive] && ![torrent isSeeding])
        {
            desiredActive--;
            if (desiredActive <= 0)
                break;
        }
    
    //sort torrents by order value
    NSArray * sortedTorrents;
    if ([torrents count] > 1 && desiredActive > 0)
    {
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                    @"orderValue" ascending: YES] autorelease];
        NSArray * descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];
        
        sortedTorrents = [torrents sortedArrayUsingDescriptors: descriptors];
        [descriptors release];
    }
    else
        sortedTorrents = torrents;

    enumerator = [sortedTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        if ([torrent progress] >= 1.0)
            [torrent startTransfer];
        else
        {
            if ([torrent waitingToStart] && desiredActive > 0)
            {
                [torrent startTransfer];
                desiredActive--;
            }
        }
        
        [torrent update];
    }
}

- (void) reloadInspectorSettings: (NSNotification *) notification
{
    [fInfoController updateInfoStatsAndSettings];
}

- (void) checkAutoImportDirectory: (NSTimer *) timer
{
    if (![fDefaults boolForKey: @"AutoImport"])
        return;
    	
    NSString * path = [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath];
    
    //if folder cannot be found or the contents hasn't changed simply give up
    NSArray * allFileNames;
    if (!(allFileNames = [[NSFileManager defaultManager] directoryContentsAtPath: path])
            || [allFileNames isEqualToArray: fAutoImportedNames])
        return;

    //try to add files that haven't already been added
    NSMutableArray * newFileNames = [NSMutableArray arrayWithArray: allFileNames];
    [newFileNames removeObjectsInArray: fAutoImportedNames];
    
    //save the current list of files
    [fAutoImportedNames setArray: allFileNames];
    
    NSEnumerator * enumerator = [newFileNames objectEnumerator];
    NSString * file;
    unsigned oldCount;
    while ((file = [enumerator nextObject]))
        if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
        {
            oldCount = [fTorrents count];
            [self openFromSheet: [NSArray arrayWithObject: [path stringByAppendingPathComponent: file]]];
            
            //import only actually happened if the torrent array is larger
            if (oldCount < [fTorrents count])
                [GrowlApplicationBridge notifyWithTitle: @"Torrent File Auto Added"
                    description: file notificationName: @"Torrent Auto Added" iconData: nil
                    priority: 0 isSticky: NO clickContext: nil];
        }
}

- (void) autoImportChange: (NSNotification *) notification
{
    [fAutoImportedNames removeAllObjects];
    [self checkAutoImportDirectory: nil];
}

- (int) numberOfRowsInTableView: (NSTableView *) t
{
    return [fFilteredTorrents count];
}

- (void) tableView: (NSTableView *) t willDisplayCell: (id) cell
    forTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    [cell setTorrent: [fFilteredTorrents objectAtIndex: row]];
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) indexes
    toPasteboard: (NSPasteboard *) pasteboard
{
    //only allow reordering of rows if sorting by order with no filter
    if ([fSortType isEqualToString: @"Order"] && [fFilterType isEqualToString: @"None"]
            && [[fSearchFilterField stringValue] isEqualToString: @""])
    {
        [pasteboard declareTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE] owner: self];
        [pasteboard setData: [NSKeyedArchiver archivedDataWithRootObject: indexes]
                                forType: TORRENT_TABLE_VIEW_DATA_TYPE];
        return YES;
    }
    return NO;
}

- (NSDragOperation) tableView: (NSTableView *) t validateDrop: (id <NSDraggingInfo>) info
    proposedRow: (int) row proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //check if any files to add have "torrent" as an extension
        NSEnumerator * enumerator = [[pasteboard propertyListForType: NSFilenamesPboardType] objectEnumerator];
        NSString * file;
        while ((file = [enumerator nextObject]))
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                [fTableView setDropRow: -1 dropOperation: NSTableViewDropOn];
                return NSDragOperationGeneric;
            }
    }
    else if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow: row dropOperation: NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    else;
    
    return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) t acceptDrop: (id <NSDraggingInfo>) info
    row: (int) newRow dropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //create an array of files with the "torrent" extension
        NSMutableArray * filesToOpen = [[NSMutableArray alloc] init];
        NSEnumerator * enumerator = [[pasteboard propertyListForType: NSFilenamesPboardType] objectEnumerator];
        NSString * file;
        while ((file = [enumerator nextObject]))
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
                [filesToOpen addObject: file];
    
        [self application: NSApp openFiles: filesToOpen];
        [filesToOpen release];
    }
    else
    {
        //remember selected rows if needed
        NSArray * selectedTorrents = nil;
        int numSelected = [fTableView numberOfSelectedRows];
        if (numSelected > 0 && numSelected < [fFilteredTorrents count])
            selectedTorrents = [self torrentsAtIndexes: [fTableView selectedRowIndexes]];
    
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData:
                                [pasteboard dataForType: TORRENT_TABLE_VIEW_DATA_TYPE]];
        
        //move torrent in array 
        NSArray * movingTorrents = [[self torrentsAtIndexes: indexes] retain];
        [fFilteredTorrents removeObjectsInArray: movingTorrents];
        
        //determine the insertion index now that transfers to move have been removed
        int i, decrease = 0;
        for (i = [indexes firstIndex]; i < newRow && i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
            decrease++;
        
        //insert objects at new location
        for (i = 0; i < [movingTorrents count]; i++)
            [fFilteredTorrents insertObject: [movingTorrents objectAtIndex: i] atIndex: newRow - decrease + i];
        
        [movingTorrents release];
        
        //redo order values
        int low = [indexes firstIndex], high = [indexes lastIndex];
        if (newRow < low)
            low = newRow;
        else if (newRow > high + 1)
            high = newRow - 1;
        else;
        
        for (i = low; i <= high; i++)
            [[fFilteredTorrents objectAtIndex: i] setOrderValue: i];
        
        [fTableView reloadData];
        
        //set selected rows if needed
        if (selectedTorrents)
        {
            Torrent * torrent;
            NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
            NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
            while ((torrent = [enumerator nextObject]))
                [indexSet addIndex: [fFilteredTorrents indexOfObject: torrent]];
            
            [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
            [indexSet release];
        }
    }
    
    return YES;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fInfoController updateInfoForTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (void) toggleSmallView: (id) sender
{
    BOOL makeSmall = ![fDefaults boolForKey: @"SmallView"];
    
    [fTableView setRowHeight: makeSmall ? ROW_HEIGHT_SMALL : ROW_HEIGHT_REGULAR];
    [fSmallViewItem setState: makeSmall];
    
    [fDefaults setBool: makeSmall forKey: @"SmallView"];
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize],
            contentSize = [[fWindow contentView] frame].size;
    contentMinSize.height = contentSize.height - [fScrollView frame].size.height
                            + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    //resize for larger min height
    if (!makeSmall && contentSize.height < contentMinSize.height)
    {
        NSRect frame = [fWindow frame];
        float heightChange = contentMinSize.height - contentSize.height;
        frame.size.height += heightChange;
        frame.origin.y -= heightChange;
        
        [fWindow setFrame: frame display: YES];
        [fTableView reloadData];
    }
}

- (void) toggleStatusBar: (id) sender
{
    [self showStatusBar: !fStatusBarVisible animate: YES];
    [fDefaults setBool: fStatusBarVisible forKey: @"StatusBar"];
}

- (void) showStatusBar: (BOOL) show animate: (BOOL) animate
{
    if (show == fStatusBarVisible)
        return;

    if (show)
        [fStatusBar setHidden: NO];

    NSRect frame = [fWindow frame];
    float heightChange = [fStatusBar frame].size.height;
    if (!show)
        heightChange *= -1;

    frame.size.height += heightChange;
    frame.origin.y -= heightChange;
        
    fStatusBarVisible = show;
    
    [self updateUI: nil];
    
    //set views to not autoresize
    unsigned int statsMask = [fStatusBar autoresizingMask];
    unsigned int filterMask = [fFilterBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fStatusBar setAutoresizingMask: 0];
    [fFilterBar setAutoresizingMask: 0];
    [fScrollView setAutoresizingMask: 0];
    
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fStatusBar setAutoresizingMask: statsMask];
    [fFilterBar setAutoresizingMask: filterMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
        [fStatusBar setHidden: YES];
    
    //reset tracking rects for filter buttons
    [fNoFilterButton resetBounds: nil];
    [fSeedFilterButton resetBounds: nil];
    [fDownloadFilterButton resetBounds: nil];
    [fPauseFilterButton resetBounds: nil];
}

- (void) toggleFilterBar: (id) sender
{
    [self showFilterBar: !fFilterBarVisible animate: YES];
    [fDefaults setBool: fFilterBarVisible forKey: @"FilterBar"];
    
    //disable filtering when hiding
    if (!fFilterBarVisible)
    {
        [fSearchFilterField setStringValue: @""];
        [self setFilter: fNoFilterButton];
    }
}

- (void) showFilterBar: (BOOL) show animate: (BOOL) animate
{
    if (show == fFilterBarVisible)
        return;

    if (show)
        [fFilterBar setHidden: NO];

    NSRect frame = [fWindow frame];
    float heightChange = [fFilterBar frame].size.height;
    if (!show)
        heightChange *= -1;

    frame.size.height += heightChange;
    frame.origin.y -= heightChange;
        
    fFilterBarVisible = show;
    
    //set views to not autoresize
    unsigned int filterMask = [fFilterBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fFilterBar setAutoresizingMask: 0];
    [fScrollView setAutoresizingMask: 0];
    
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fFilterBar setAutoresizingMask: filterMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
    {
        [fFilterBar setHidden: YES];
        [fWindow makeFirstResponder: fTableView];
    }
    
    //enable show filter button in status bar
    [fShowFilterButton setEnabled: !show];
    
    //reset tracking rects for filter buttons
    [fNoFilterButton resetBounds: nil];
    [fSeedFilterButton resetBounds: nil];
    [fDownloadFilterButton resetBounds: nil];
    [fPauseFilterButton resetBounds: nil];
}

- (void) toggleAdvancedBar: (id) sender
{
    int state = ![fAdvancedBarItem state];
    [fAdvancedBarItem setState: state];
    [fDefaults setBool: state forKey: @"UseAdvancedBar"];

    [fTableView display];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_OPEN])
    {
        [item setLabel: @"Open"];
        [item setPaletteLabel: @"Open Torrent Files"];
        [item setToolTip: @"Open torrent files"];
        [item setImage: [NSImage imageNamed: @"Open.png"]];
        [item setTarget: self];
        [item setAction: @selector(openShowSheet:)];
    }
    else if ([ident isEqualToString: TOOLBAR_REMOVE])
    {
        [item setLabel: @"Remove"];
        [item setPaletteLabel: @"Remove Selected"];
        [item setToolTip: @"Remove selected transfers"];
        [item setImage: [NSImage imageNamed: @"Remove.png"]];
        [item setTarget: self];
        [item setAction: @selector(removeNoDelete:)];
    }
    else if ([ident isEqualToString: TOOLBAR_INFO])
    {
        [item setLabel: @"Inspector"];
        [item setPaletteLabel: @"Show/Hide Inspector"];
        [item setToolTip: @"Display torrent inspector"];
        [item setImage: [NSImage imageNamed: @"Info.png"]];
        [item setTarget: self];
        [item setAction: @selector(showInfo:)];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_ALL])
    {
        [item setLabel: @"Pause All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Pause all transfers"];
        [item setImage: [NSImage imageNamed: @"PauseAll.png"]];
        [item setTarget: self];
        [item setAction: @selector(stopAllTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        [item setLabel: @"Resume All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Resume all transfers"];
        [item setImage: [NSImage imageNamed: @"ResumeAll.png"]];
        [item setTarget: self];
        [item setAction: @selector(resumeAllTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        [item setLabel: @"Pause"];
        [item setPaletteLabel: @"Pause Selected"];
        [item setToolTip: @"Pause selected transfers"];
        [item setImage: [NSImage imageNamed: @"PauseSelected.png"]];
        [item setTarget: self];
        [item setAction: @selector(stopSelectedTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        [item setLabel: @"Resume"];
        [item setPaletteLabel: @"Resume Selected"];
        [item setToolTip: @"Resume selected transfers"];
        [item setImage: [NSImage imageNamed: @"ResumeSelected.png"]];
        [item setTarget: self];
        [item setAction: @selector(resumeSelectedTorrents:)];
    }
    else
    {
        [item release];
        return nil;
    }

    return item;
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) t
{
    return [NSArray arrayWithObjects:
            TOOLBAR_OPEN, TOOLBAR_REMOVE,
            TOOLBAR_PAUSE_SELECTED, TOOLBAR_RESUME_SELECTED,
            TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL, TOOLBAR_INFO,
            NSToolbarSeparatorItemIdentifier,
            NSToolbarSpaceItemIdentifier,
            NSToolbarFlexibleSpaceItemIdentifier,
            NSToolbarCustomizeToolbarItemIdentifier, nil];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) t
{
    return [NSArray arrayWithObjects:
            TOOLBAR_OPEN, TOOLBAR_REMOVE,
            NSToolbarSeparatorItemIdentifier,
            TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL,
            NSToolbarFlexibleSpaceItemIdentifier,
            TOOLBAR_INFO, nil];
}

- (BOOL) validateToolbarItem: (NSToolbarItem *) toolbarItem
{
    NSString * ident = [toolbarItem itemIdentifier];

    //enable remove item
    if ([ident isEqualToString: TOOLBAR_REMOVE])
        return [fTableView numberOfSelectedRows] > 0;

    //enable pause all item
    if ([ident isEqualToString: TOOLBAR_PAUSE_ALL])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
                return YES;
        return NO;
    }

    //enable resume all item
    if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isPaused])
                return YES;
        return NO;
    }

    //enable pause item
    if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[fFilteredTorrents objectAtIndex: i] isActive])
                return YES;
        return NO;
    }
    
    //enable resume item
    if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[fFilteredTorrents objectAtIndex: i] isPaused])
                return YES;
        return NO;
    }

    return YES;
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];

    //only enable some items if it is in a context menu or the window is useable 
    BOOL canUseMenu = [fWindow isKeyWindow] || [[[menuItem menu] title] isEqualToString: @"Context"];

    //enable show info
    if (action == @selector(showInfo:))
    {
        NSString * title = [[fInfoController window] isVisible] ? @"Hide Inspector" : @"Show Inspector";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return YES;
    }
    
    //enable prev/next inspector tab
    if (action == @selector(setInfoTab:))
        return [[fInfoController window] isVisible];
    
    //enable toggle status bar
    if (action == @selector(toggleStatusBar:))
    {
        NSString * title = fStatusBarVisible ? @"Hide Status Bar" : @"Show Status Bar";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return canUseMenu;
    }
    
    //enable toggle filter bar
    if (action == @selector(toggleFilterBar:))
    {
        NSString * title = fFilterBarVisible ? @"Hide Filter Bar" : @"Show Filter Bar";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return canUseMenu;
    }

    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isPaused])
                return YES;
        return NO;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
                return YES;
        return NO;
    }

    //enable reveal in finder
    if (action == @selector(revealFile:))
        return canUseMenu && [fTableView numberOfSelectedRows] > 0;

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:)
        || action == @selector(removeDeleteTorrent:) || action == @selector(removeDeleteDataAndTorrent:))
    {
        BOOL warning = NO,
            onlyDownloading = [fDefaults boolForKey: @"CheckRemoveDownloading"],
            canDelete = action != @selector(removeDeleteTorrent:) && action != @selector(removeDeleteDataAndTorrent:);
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fFilteredTorrents objectAtIndex: i];
            if (!warning && [torrent isActive])
            {
                warning = onlyDownloading ? ![torrent isSeeding] : YES;
                if (warning && canDelete)
                    break;
            }
            if (!canDelete && [torrent publicTorrent])
            {
                canDelete = YES;
                if (warning)
                    break;
            }
        }
    
        //append or remove ellipsis when needed
        NSString * title = [menuItem title], * ellipsis = [NSString ellipsis];
        if (warning && [fDefaults boolForKey: @"CheckRemove"])
        {
            if (![title hasSuffix: ellipsis])
                [menuItem setTitle: [title stringByAppendingEllipsis]];
        }
        else
        {
            if ([title hasSuffix: ellipsis])
                [menuItem setTitle: [title substringToIndex: [title rangeOfString: ellipsis].location]];
        }
        
        return canUseMenu && canDelete && [fTableView numberOfSelectedRows] > 0;
    }

    //enable pause item
    if (action == @selector(stopSelectedTorrents:))
    {
        if (!canUseMenu)
            return NO;
    
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fFilteredTorrents objectAtIndex: i];
            if ([torrent isActive])
                return YES;
        }
        return NO;
    }
    
    //enable resume item
    if (action == @selector(resumeSelectedTorrents:))
    {
        if (!canUseMenu)
            return NO;
    
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fFilteredTorrents objectAtIndex: i];
            if ([torrent isPaused])
                return YES;
        }
        return NO;
    }
    
    //enable sort and advanced bar items
    if (action == @selector(setSort:) || action == @selector(toggleAdvancedBar:)
            || action == @selector(toggleSmallView:))
        return canUseMenu;
    
    //enable copy torrent file item
    if (action == @selector(copyTorrentFile:))
    {
        return canUseMenu && [fTableView numberOfSelectedRows] > 0;
    }

    return YES;
}

- (void) sleepCallBack: (natural_t) messageType argument: (void *) messageArgument
{
    NSEnumerator * enumerator;
    Torrent * torrent;
    BOOL active;

    switch (messageType)
    {
        case kIOMessageSystemWillSleep:
            //close all connections before going to sleep and remember we should resume when we wake up
            [fTorrents makeObjectsPerformSelector: @selector(sleep)];

            //wait for running transfers to stop (5 second timeout)
            NSDate * start = [NSDate date];
            BOOL timeUp = NO;
            
            NSEnumerator * enumerator = [fTorrents objectEnumerator];
            Torrent * torrent;
            while (!timeUp && (torrent = [enumerator nextObject]))
                while (![torrent isPaused] && !(timeUp = [start timeIntervalSinceNow] < -5.0))
                {
                    usleep(100000);
                    [torrent update];
                }

            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageCanSystemSleep:
            //pevent idle sleep unless all paused
            active = NO;
            enumerator = [fTorrents objectEnumerator];
            while ((torrent = [enumerator nextObject]))
                if ([torrent isActive])
                {
                    active = YES;
                    break;
                }

            if (active)
                IOCancelPowerChange(fRootPort, (long) messageArgument);
            else
                IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageSystemHasPoweredOn:
            //resume sleeping transfers after we wake up
            [fTorrents makeObjectsPerformSelector: @selector(wakeUp)];
            break;
    }
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    NSRect windowRect = [fWindow frame];
    float newHeight = windowRect.size.height - [fScrollView frame].size.height
        + [fFilteredTorrents count] * ([fTableView rowHeight] + [fTableView intercellSpacing].height);

    float minHeight = [fWindow minSize].height;
    if (newHeight < minHeight)
        newHeight = minHeight;

    windowRect.origin.y -= (newHeight - windowRect.size.height);
    windowRect.size.height = newHeight;

    return windowRect;
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: nil];
}

- (void) windowDidBecomeKey: (NSNotification *) notification
{
    //reset dock badge for completed
    if (fCompleted > 0)
    {
        fCompleted = 0;
        [self updateUI: nil];
    }
    
    //set status fields inactive color
    /*NSColor * enabledColor = [NSColor controlTextColor];
    [fTotalTorrentsField setTextColor: enabledColor];
    [fTotalDLField setTextColor: enabledColor];
    [fTotalULField setTextColor: enabledColor];*/
    
    //set filter images as active
    [fNoFilterButton setForActive];
    [fSeedFilterButton setForActive];
    [fDownloadFilterButton setForActive];
    [fPauseFilterButton setForActive];
    
    /*if (fSpeedLimitEnabled)
        [fSpeedLimitButton setImage: [NSColor currentControlTint] == NSBlueControlTint
                                        ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage];*/
}

- (void) windowDidResignKey: (NSNotification *) notification
{
    #warning should it do this?
    //set status fields inactive color
    /*NSColor * disabledColor = [NSColor disabledControlTextColor];
    [fTotalTorrentsField setTextColor: disabledColor];
    [fTotalDLField setTextColor: disabledColor];
    [fTotalULField setTextColor: disabledColor];*/

    //set filter images as inactive
    [fNoFilterButton setForInactive];
    [fSeedFilterButton setForInactive];
    [fDownloadFilterButton setForInactive];
    [fPauseFilterButton setForInactive];
    
    #warning need real inactive image, or to not do anything at all (?)
    /*if (fSpeedLimitEnabled)
        [fSpeedLimitButton setImage: fSpeedLimitNormalImage];*/
}

- (void) windowDidResize: (NSNotification *) notification
{
    //hide search filter if it overlaps filter buttons
    NSRect buttonFrame = [fPauseFilterButton frame];
    BOOL overlap = buttonFrame.origin.x + buttonFrame.size.width + 2.0 > [fSearchFilterField frame].origin.x;
    
    if (![fSearchFilterField isHidden])
    {
        if (overlap)
            [fSearchFilterField setHidden: YES];
    }
    else
    {
        if (!overlap)
            [fSearchFilterField setHidden: NO];
    }
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: FORUM_URL]];
}

- (void) checkUpdate: (id) sender
{
    [fPrefsController checkUpdate];
}

- (void) prepareForUpdate: (NSNotification *) notification
{
    fUpdateInProgress = YES;
}

- (NSDictionary *) registrationDictionaryForGrowl
{
    NSArray * notifications = [NSArray arrayWithObjects: @"Download Complete",
                                @"Seeding Complete", @"Torrent Auto Added", nil];
    return [NSDictionary dictionaryWithObjectsAndKeys: notifications, GROWL_NOTIFICATIONS_ALL,
                                notifications, GROWL_NOTIFICATIONS_DEFAULT, nil];
}

@end
