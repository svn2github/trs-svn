/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#import "PrefsController.h"
#import "NSStringAdditions.h"
#import "UKKQueue.h"

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2
#define DOWNLOAD_ASK        3

#define UPDATE_SECONDS 86400

#define TOOLBAR_GENERAL     @"TOOLBAR_GENERAL"
#define TOOLBAR_TRANSFERS   @"TOOLBAR_TRANSFERS"
#define TOOLBAR_BANDWIDTH   @"TOOLBAR_BANDWIDTH"
#define TOOLBAR_ADVANCED    @"TOOLBAR_ADVANCED"

@interface PrefsController (Private)

- (void) setPrefView: (id) sender;

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) incompleteFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;

@end

@implementation PrefsController

- (id) initWithHandle: (tr_handle *) handle
{
    if ((self = [super initWithWindowNibName: @"PrefsWindow"]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        fHandle = handle;
        
        //checks for old version upload speed of -1
        if ([fDefaults integerForKey: @"UploadLimit"] < 0)
        {
            [fDefaults setInteger: 20 forKey: @"UploadLimit"];
            [fDefaults setBool: NO forKey: @"CheckUpload"];
        }
        
        //set check for update to right value
        [self setCheckForUpdate: nil];
        
        //set auto import
        NSString * autoPath;
        if ([fDefaults boolForKey: @"AutoImport"] && (autoPath = [fDefaults stringForKey: @"AutoImportDirectory"]))
            [[UKKQueue sharedFileWatcher] addPath: [autoPath stringByExpandingTildeInPath]];
        
        //set bind port
        int bindPort = [fDefaults integerForKey: @"BindPort"];
        tr_setBindPort(fHandle, bindPort);
        
        //set NAT
        tr_natTraversalEnable(fHandle, [fDefaults boolForKey: @"NatTraversal"]);
        
        //set encryption
        tr_setEncryptionMode(fHandle, [fDefaults boolForKey: @"EncryptionRequire"] ? TR_ENCRYPTION_REQUIRED : TR_ENCRYPTION_PREFERRED);
        
        //actually set bandwidth limits
        [self applySpeedSettings: nil];
    }
    
    return self;
}

- (void) dealloc
{
    if (fNatStatusTimer)
        [fNatStatusTimer invalidate];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    fHasLoaded = YES;
    
    NSToolbar * toolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [toolbar setDelegate: self];
    [toolbar setAllowsUserCustomization: NO];
    [toolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [toolbar setSizeMode: NSToolbarSizeModeRegular];
    [[self window] setToolbar: toolbar];

    [toolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [self setPrefView: nil];
    
    //set download folder
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    if ([downloadChoice isEqualToString: @"Constant"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
    else if ([downloadChoice isEqualToString: @"Torrent"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
    else
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    
    //set stop ratio
    [self updateRatioStopField];
    
    //set limits
    [self updateLimitFields];
    
    //set speed limit
    [fSpeedLimitUploadField setIntValue: [fDefaults integerForKey: @"SpeedLimitUploadLimit"]];
    [fSpeedLimitDownloadField setIntValue: [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]];
    
    //set port
    [fPortField setIntValue: [fDefaults integerForKey: @"BindPort"]];
    fNatStatus = -1;
    
    [self updatePortStatus];
    fNatStatusTimer = [NSTimer scheduledTimerWithTimeInterval: 5.0 target: self
                        selector: @selector(updatePortStatus) userInfo: nil repeats: YES];
    
    //set queue values
    [fQueueDownloadField setIntValue: [fDefaults integerForKey: @"QueueDownloadNumber"]];
    [fQueueSeedField setIntValue: [fDefaults integerForKey: @"QueueSeedNumber"]];
    
    //set stalled value
    [fStalledField setIntValue: [fDefaults integerForKey: @"StalledMinutes"]];
}

- (void) setUpdater: (SUUpdater *) updater
{
    fUpdater = updater;
}

- (NSToolbarItem *) toolbar: (NSToolbar *) toolbar itemForItemIdentifier: (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item;
    item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_GENERAL])
    {
        [item setLabel: NSLocalizedString(@"General", "Preferences -> General toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Preferences.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: NSLocalizedString(@"Transfers", "Preferences -> Transfers toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Transfers.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_BANDWIDTH])
    {
        [item setLabel: NSLocalizedString(@"Bandwidth", "Preferences -> Bandwidth toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Bandwidth.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_ADVANCED])
    {
        [item setLabel: NSLocalizedString(@"Advanced", "Preferences -> Advanced toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Advanced.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else
    {
        [item release];
        return nil;
    }

    return [item autorelease];
}

- (NSArray *) toolbarSelectableItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarDefaultItemIdentifiers: toolbar];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarAllowedItemIdentifiers: toolbar];
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects: TOOLBAR_GENERAL, TOOLBAR_TRANSFERS,
                                        TOOLBAR_BANDWIDTH, TOOLBAR_ADVANCED, nil];
}

- (void) setPort: (id) sender
{
    int port = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", port]])
    {
        NSBeep();
        [sender setIntValue: [fDefaults integerForKey: @"BindPort"]];
        return;
    }
    
    [fDefaults setInteger: fPublicPort forKey: @"BindPort"];
    
    tr_setBindPort(fHandle, port);
    
    fPublicPort = -1;
    [self updatePortStatus];
}

- (void) setNat: (id) sender
{
    tr_natTraversalEnable(fHandle, [fDefaults boolForKey: @"NatTraversal"]);
    [self updatePortStatus];
}

- (void) updatePortStatus
{
    tr_handle_status * stat = tr_handleStatus(fHandle);
    
    BOOL change;
    if (change = (fNatStatus != stat->natTraversalStatus))
    {
        fNatStatus = stat->natTraversalStatus;
        switch (fNatStatus)
        {
            case TR_NAT_TRAVERSAL_MAPPED:
                [fNatStatusField setStringValue: NSLocalizedString(@"Port successfully mapped",
                                                    "Preferences -> Advanced -> port map status")];
                [fNatStatusImage setImage: [NSImage imageNamed: @"GreenDot.tiff"]];
                break;
            
            case TR_NAT_TRAVERSAL_NOTFOUND:
            case TR_NAT_TRAVERSAL_ERROR:
                [fNatStatusField setStringValue: NSLocalizedString(@"Error mapping port",
                                                    "Preferences -> Advanced -> port map status")];
                [fNatStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
                break;
            
            default:
                [fNatStatusField setStringValue: @""];
                [fNatStatusImage setImage: nil];
        }
    }
    
    if (change || fPublicPort != stat->publicPort)
    {
        fPublicPort = stat->publicPort;
        
        [fPortStatusField setStringValue: [NSLocalizedString(@"Checking port status",
                                            "Preferences -> Advanced -> port status") stringByAppendingEllipsis]];
        [fPortStatusImage setImage: nil];
        [fPortStatusProgress startAnimation: self];
        
        PortChecker * portChecker = [[PortChecker alloc] initWithDelegate: self];
        [portChecker probePort: fPublicPort];
    }
}

- (void) portCheckerDidFinishProbing: (PortChecker *) portChecker
{
    [fPortStatusProgress stopAnimation: self];
    switch ([portChecker status])
    {
        case PORT_STATUS_OPEN:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is open", "Preferences -> Advanced -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"GreenDot.tiff"]];
            break;
        case PORT_STATUS_STEALTH:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is stealth", "Preferences -> Advanced -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
            break;
        case PORT_STATUS_CLOSED:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is closed", "Preferences -> Advanced -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
            break;
        case PORT_STATUS_ERROR:
            [fPortStatusField setStringValue: NSLocalizedString(@"Unable to check port status",
                                                "Preferences -> Advanced -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"YellowDot.tiff"]];
            break;
    }
    [portChecker release];
}

- (NSArray *) sounds
{
    NSMutableArray * sounds = [NSMutableArray array];
    
    NSArray * directories = [NSArray arrayWithObjects: @"/System/Library/Sounds", @"/Library/Sounds",
                                [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Sounds"], nil];
    BOOL isDirectory;
    NSEnumerator * soundEnumerator;
    NSString * sound;
    
    NSString * directory;
    NSEnumerator * enumerator = [directories objectEnumerator];
    while ((directory = [enumerator nextObject]))
        if ([[NSFileManager defaultManager] fileExistsAtPath: directory isDirectory: &isDirectory] && isDirectory)
        {
                soundEnumerator = [[[NSFileManager defaultManager] directoryContentsAtPath: directory] objectEnumerator];
                while ((sound = [soundEnumerator nextObject]))
                {
                    sound = [sound stringByDeletingPathExtension];
                    if ([NSSound soundNamed: sound])
                        [sounds addObject: sound];
                }
        }
    
    return sounds;
}

- (void) setSound: (id) sender
{
    //play sound when selecting
    NSSound * sound;
    if ((sound = [NSSound soundNamed: [sender titleOfSelectedItem]]))
        [sound play];
}

- (void) setEncryptionRequired: (id) sender
{
    tr_setEncryptionMode(fHandle, [fDefaults boolForKey: @"EncryptionRequire"] ? TR_ENCRYPTION_REQUIRED : TR_ENCRYPTION_PREFERRED);
}

- (void) applySpeedSettings: (id) sender
{
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        tr_setUseGlobalSpeedLimit(fHandle, TR_UP, 1);
        tr_setGlobalSpeedLimit(fHandle, TR_UP, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
        
        tr_setUseGlobalSpeedLimit(fHandle, TR_DOWN, 1);
        tr_setGlobalSpeedLimit(fHandle, TR_DOWN, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);
    }
    else
    {
        tr_setUseGlobalSpeedLimit(fHandle, TR_UP, [fDefaults boolForKey: @"CheckUpload"]);
        tr_setGlobalSpeedLimit(fHandle, TR_UP, [fDefaults integerForKey: @"UploadLimit"]);
        
        tr_setUseGlobalSpeedLimit(fHandle, TR_DOWN, [fDefaults boolForKey: @"CheckDownload"]);
        tr_setGlobalSpeedLimit(fHandle, TR_DOWN, [fDefaults integerForKey: @"DownloadLimit"]);
    }
}

- (void) applyRatioSetting: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) updateRatioStopField
{
    if (!fHasLoaded)
        return;
    
    [fRatioStopField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
    
    [self applyRatioSetting: nil];
}

- (void) setRatioStop: (id) sender
{
    float ratio = [sender floatValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%.2f", ratio]] || ratio < 0)
    {
        NSBeep();
        [sender setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
        return;
    }
    
    [fDefaults setFloat: ratio forKey: @"RatioLimit"];
    
    [self applyRatioSetting: nil];
}

- (void) updateLimitFields
{
    if (!fHasLoaded)
        return;
    
    [fUploadField setIntValue: [fDefaults integerForKey: @"UploadLimit"]];
    [fDownloadField setIntValue: [fDefaults integerForKey: @"DownloadLimit"]];
}

- (void) setGlobalLimit: (id) sender
{
    BOOL upload = sender == fUploadField;
    
    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]] || limit < 0)
    {
        NSBeep();
        [sender setIntValue: [fDefaults integerForKey: upload ? @"UploadLimit" : @"DownloadLimit"]];
        return;
    }
    
    [fDefaults setInteger: limit forKey: upload ? @"UploadLimit" : @"DownloadLimit"];
    
    [self applySpeedSettings: self];
}

- (void) setSpeedLimit: (id) sender
{
    BOOL upload = sender == fSpeedLimitUploadField;
    
    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]])
    {
        NSBeep();
        [sender setIntValue: [fDefaults integerForKey: upload ? @"SpeedLimitUploadLimit" : @"SpeedLimitDownloadLimit"]];
        return;
    }
    
    [fDefaults setInteger: limit forKey: upload ? @"SpeedLimitUploadLimit" : @"SpeedLimitDownloadLimit"];
    
    [self applySpeedSettings: self];
}

- (void) setAutoSpeedLimit: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSpeedLimitChange" object: self];
}

- (void) setBadge: (id) sender
{   
    [[NSNotificationCenter defaultCenter] postNotificationName: @"DockBadgeChange" object: self];
}

- (void) resetWarnings: (id) sender
{
    [fDefaults setBool: YES forKey: @"WarningDebug"];
    [fDefaults setBool: YES forKey: @"WarningDuplicate"];
    [fDefaults setBool: YES forKey: @"WarningRemainingSpace"];
}

- (void) setCheckForUpdate: (id) sender
{
    NSTimeInterval seconds = [fDefaults boolForKey: @"CheckForUpdates"] ? UPDATE_SECONDS : 0;
    [fDefaults setInteger: seconds forKey: @"SUScheduledCheckInterval"];
    if (fUpdater)
        [fUpdater scheduleCheckWithInterval: seconds];
}

- (void) setQueue: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
}

- (void) setQueueNumber: (id) sender
{
    BOOL download = sender == fQueueDownloadField;
    
    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]] || limit < 1)
    {
        NSBeep();
        [sender setIntValue: [fDefaults integerForKey: download ? @"QueueDownloadNumber" : @"QueueSeedNumber"]];
        return;
    }
    
    [fDefaults setInteger: limit forKey: download ? @"QueueDownloadNumber" : @"QueueSeedNumber"];
    [self setQueue: nil];
}

- (void) setStalled: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: self];
}

- (void) setStalledMinutes: (id) sender
{
    int minutes = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", minutes]] || minutes < 1)
    {
        NSBeep();
        [sender setIntValue: [fDefaults integerForKey: @"StalledMinutes"]];
        return;
    }
    
    [fDefaults setInteger: minutes forKey: @"StalledMinutes"];
    [self setStalled: nil];
}

- (void) setDownloadLocation: (id) sender
{
    switch ([fFolderPopUp indexOfSelectedItem])
    {
        case DOWNLOAD_FOLDER:
            [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
            break;
        case DOWNLOAD_TORRENT:
            [fDefaults setObject: @"Torrent" forKey: @"DownloadChoice"];
            break;
        case DOWNLOAD_ASK:
            [fDefaults setObject: @"Ask" forKey: @"DownloadChoice"];
            break;
    }
}

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(folderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) incompleteFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(incompleteFolderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoImport: (id) sender
{
    NSString * path;
    if ((path = [fDefaults stringForKey: @"AutoImportDirectory"]))
    {
        path = [path stringByExpandingTildeInPath];
        if ([fDefaults boolForKey: @"AutoImport"])
            [[UKKQueue sharedFileWatcher] addPath: path];
        else
            [[UKKQueue sharedFileWatcher] removePathFromQueue: path];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
    }
    else
        [self importFolderSheetShow: nil];
}

- (void) importFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(importFolderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoSize: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSizeSettingChange" object: self];
}

- (void) helpForNetwork: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"PortForwarding"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

@end

@implementation PrefsController (Private)

- (void) setPrefView: (id) sender
{
    NSView * view = fGeneralView;
    if (sender)
    {
        NSString * identifier = [sender itemIdentifier];
        if ([identifier isEqualToString: TOOLBAR_TRANSFERS])
            view = fTransfersView;
        else if ([identifier isEqualToString: TOOLBAR_BANDWIDTH])
            view = fBandwidthView;
        else if ([identifier isEqualToString: TOOLBAR_ADVANCED])
            view = fAdvancedView;
        else;
    }
    
    NSWindow * window = [self window];
    if ([window contentView] == view)
        return;
    
    NSRect windowRect = [window frame];
    float difference = ([view frame].size.height - [[window contentView] frame].size.height) * [window userSpaceScaleFactor];
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;
    
    [view setHidden: YES];
    [window setContentView: view];
    [window setFrame: windowRect display: YES animate: YES];
    [view setHidden: NO];
    
    //set title label
    if (sender)
        [window setTitle: [sender label]];
    else
    {
        NSToolbar * toolbar = [window toolbar];
        NSString * itemIdentifier = [toolbar selectedItemIdentifier];
        NSEnumerator * enumerator = [[toolbar items] objectEnumerator];
        NSToolbarItem * item;
        while ((item = [enumerator nextObject]))
            if ([[item itemIdentifier] isEqualToString: itemIdentifier])
            {
                [window setTitle: [item label]];
                break;
            }
    }
    
    //for advanced view make sure progress indicator hides itself
    if (view == fAdvancedView && [fPortStatusImage image])
        [fPortStatusProgress setDisplayedWhenStopped: NO];
}

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"DownloadFolder"];
        [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
    }
    else
    {
        //reset if cancelled
        NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
        if ([downloadChoice isEqualToString: @"Constant"])
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        else if ([downloadChoice isEqualToString: @"Torrent"])
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
        else
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    }
}

- (void) incompleteFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"IncompleteDownloadFolder"];
    [fIncompleteFolderPopUp selectItemAtIndex: 0];
}

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    NSString * path = [fDefaults stringForKey: @"AutoImportDirectory"];
    if (code == NSOKButton)
    {
        UKKQueue * sharedQueue = [UKKQueue sharedFileWatcher];
        if (path)
            [sharedQueue removePathFromQueue: [path stringByExpandingTildeInPath]];
        
        path = [[openPanel filenames] objectAtIndex: 0];
        [fDefaults setObject: path forKey: @"AutoImportDirectory"];
        [sharedQueue addPath: [path stringByExpandingTildeInPath]];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
    }
    else if (!path)
        [fDefaults setBool: NO forKey: @"AutoImport"];
    
    [fImportFolderPopUp selectItemAtIndex: 0];
}

@end
