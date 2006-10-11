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

#import "Torrent.h"
#import "StringAdditions.h"

#define BAR_HEIGHT 12.0

#define MAX_PIECES 324
#define BLANK_PIECE -99

@interface Torrent (Private)

- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        privateTorrent: (NSNumber *) privateTorrent publicTorrent: (NSNumber *) publicTorrent
        date: (NSDate *) date stopRatioSetting: (NSNumber *) stopRatioSetting
        ratioLimit: (NSNumber *) ratioLimit waitToStart: (NSNumber *) waitToStart
        orderValue: (NSNumber *) orderValue;

- (NSImage *) advancedBar;

- (void) trashFile: (NSString *) path;

@end

@implementation Torrent

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
#define BE OSSwapBigToHostConstInt32

static uint32_t kRed   = BE(0xFF6450FF), //255, 100, 80
                kBlue = BE(0x50A0FFFF), //80, 160, 255
                kBlue2 = BE(0x1E46B4FF), //30, 70, 180
                kGray  = BE(0x969696FF), //150, 150, 150
                kGreen1 = BE(0x99FFCCFF), //153, 255, 204
                kGreen2 = BE(0x66FF99FF), //102, 255, 153
                kGreen3 = BE(0x00FF66FF), //0, 255, 102
                kWhite = BE(0xFFFFFFFF); //255, 255, 255

- (id) initWithPath: (NSString *) path lib: (tr_handle_t *) lib
{
    self = [self initWithHash: nil path: path lib: lib privateTorrent: nil publicTorrent: nil
            date: nil stopRatioSetting: nil ratioLimit: nil waitToStart: nil orderValue: nil];
    
    if (self)
    {
        if (!fPublicTorrent)
            [self trashFile: path];
    }
    return self;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib
{
    self = [self initWithHash: [history objectForKey: @"TorrentHash"]
                path: [history objectForKey: @"TorrentPath"] lib: lib
                privateTorrent: [history objectForKey: @"PrivateCopy"]
                publicTorrent: [history objectForKey: @"PublicCopy"]
                date: [history objectForKey: @"Date"]
                stopRatioSetting: [history objectForKey: @"StopRatioSetting"]
                ratioLimit: [history objectForKey: @"RatioLimit"]
                waitToStart: [history objectForKey: @"WaitToStart"]
                orderValue: [history objectForKey: @"OrderValue"]];
    
    if (self)
    {
        NSString * downloadFolder;
        if (!(downloadFolder = [history objectForKey: @"DownloadFolder"]))
            downloadFolder = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];
        [self setDownloadFolder: downloadFolder];

        NSString * paused;
        if (!(paused = [history objectForKey: @"Paused"]) || [paused isEqualToString: @"NO"])
        {
            fStat = tr_torrentStat(fHandle);
            [self startTransfer];
        }
    }
    return self;
}

- (NSDictionary *) history
{
    NSMutableDictionary * history = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    [NSNumber numberWithBool: fPrivateTorrent], @"PrivateCopy",
                    [NSNumber numberWithBool: fPublicTorrent], @"PublicCopy",
                    [self downloadFolder], @"DownloadFolder",
                    [self isActive] ? @"NO" : @"YES", @"Paused",
                    [self date], @"Date",
                    [NSNumber numberWithInt: fStopRatioSetting], @"StopRatioSetting",
                    [NSNumber numberWithFloat: fRatioLimit], @"RatioLimit",
                    [NSNumber numberWithBool: fWaitToStart], @"WaitToStart",
                    [self orderValue], @"OrderValue", nil];
            
    if (fPrivateTorrent)
        [history setObject: [self hashString] forKey: @"TorrentHash"];

    if (fPublicTorrent)
        [history setObject: [self publicTorrentLocation] forKey: @"TorrentPath"];
    
    return history;
}

- (void) dealloc
{
    if (fHandle)
    {
        tr_torrentClose(fLib, fHandle);
        
        if (fPublicTorrentLocation)
            [fPublicTorrentLocation release];
        
        [fDate release];
        
        [fIcon release];
        [fIconFlipped release];
        [fIconSmall release];
        
        [fProgressString release];
        [fStatusString release];
        [fShortStatusString release];
        [fRemainingTimeString release];
        
        
        [fBitmap release];
        free(fPieces);
    }
    [super dealloc];
}

- (void) setDownloadFolder: (NSString *) path
{
    tr_torrentSetFolder(fHandle, [path UTF8String]);
}

- (NSString *) downloadFolder
{
    return [NSString stringWithUTF8String: tr_torrentGetFolder(fHandle)];
}

- (void) getAvailability: (int8_t *) tab size: (int) size
{
    tr_torrentAvailability(fHandle, tab, size);
}

- (void) update
{
    fStat = tr_torrentStat(fHandle);
    
    //notification when downloading finished
    if ([self justFinished])
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFinishedDownloading" object: self];
    
    //check to stop for ratio
    if ([self isSeeding] && ((fStopRatioSetting == RATIO_CHECK && [self ratio] >= fRatioLimit)
            || (fStopRatioSetting == RATIO_GLOBAL && [fDefaults boolForKey: @"RatioCheck"]
            && [self ratio] >= [fDefaults floatForKey: @"RatioLimit"])))
    {
        [self stopTransfer];
        [self setStopRatioSetting: RATIO_NO_CHECK];
        fFinishedSeeding = YES;
        
        fStat = tr_torrentStat(fHandle);
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentStoppedForRatio" object: self];
    }

    [fProgressString setString: @""];
    if ([self progress] < 1.0)
        [fProgressString appendFormat: @"%@ of %@ (%.2f%%)", [NSString stringForFileSize:
                [self downloadedValid]], [NSString stringForFileSize: [self size]], 100.0 * [self progress]];
    else
        [fProgressString appendFormat: @"%@, uploaded %@ (Ratio: %@)", [NSString stringForFileSize:
                [self size]], [NSString stringForFileSize: [self uploadedTotal]],
                [NSString stringForRatioWithDownload: [self downloadedTotal] upload: [self uploadedTotal]]];

    switch (fStat->status)
    {
        NSString * tempString;
    
        case TR_STATUS_PAUSE:
            if (fFinishedSeeding)
                tempString = @"Seeding complete";
            else if (fWaitToStart)
                tempString = [@"Waiting to start" stringByAppendingEllipsis];
            else
                tempString = @"Paused";
            
            [fStatusString setString: tempString];
            [fShortStatusString setString: tempString];
            
            break;

        case TR_STATUS_CHECK:
            tempString = [@"Checking existing files" stringByAppendingEllipsis];
            
            [fStatusString setString: tempString];
            [fShortStatusString setString: tempString];
            [fRemainingTimeString setString: tempString];
            
            break;

        case TR_STATUS_DOWNLOAD:
            [fStatusString setString: @""];
            [fStatusString appendFormat:
                @"Downloading from %d of %d peer%s", [self peersUploading], [self totalPeers],
                [self totalPeers] == 1 ? "" : "s"];
            
            [fRemainingTimeString setString: @""];
            int eta = [self eta];
            if (eta < 0)
            {
                [fRemainingTimeString setString: @"Unknown"];
                [fProgressString appendString: @" - remaining time unknown"];
            }
            else
            {
                if (eta < 60)
                    [fRemainingTimeString appendFormat: @"%d sec", eta];
                else if (eta < 3600) //60 * 60
                    [fRemainingTimeString appendFormat: @"%d min %02d sec", eta / 60, eta % 60];
                else if (eta < 86400) //24 * 60 * 60
                    [fRemainingTimeString appendFormat: @"%d hr %02d min", eta / 3600, (eta / 60) % 60];
                else
                    [fRemainingTimeString appendFormat: @"%d day%s %d hr",
                                                eta / 86400, eta / 86400 == 1 ? "" : "s", (eta / 3600) % 24];
                
                [fProgressString appendFormat: @" - %@ remaining", fRemainingTimeString];
            }
            
            break;

        case TR_STATUS_SEED:
            [fStatusString setString: @""];
            [fStatusString appendFormat:
                @"Seeding to %d of %d peer%s",
                [self peersDownloading], [self totalPeers], [self totalPeers] == 1 ? "" : "s"];
            
            break;

        case TR_STATUS_STOPPING:
            tempString = [@"Stopping" stringByAppendingEllipsis];
        
            [fStatusString setString: tempString];
            [fShortStatusString setString: tempString];
            
            break;
    }
    
    if (fStat->error & TR_ETRACKER)
    {
        [fStatusString setString: [@"Error: " stringByAppendingString: [NSString stringWithUTF8String: fStat->trackerError]]];
        if (!fError && [self isActive])
        {
            fError = YES;
            if (![self isSeeding])
                [[NSNotificationCenter defaultCenter] postNotificationName: @"StoppedDownloading" object: self];
        }
    }
    else
    {
        if (fError)
            fError = NO;
    }

    if ([self isActive])
    {
        NSString * stringToAppend = @"";
        if ([self progress] < 1.0)
        {
            stringToAppend = [NSString stringWithFormat: @"DL: %@, ", [NSString stringForSpeed: [self downloadRate]]];
            [fShortStatusString setString: @""];
        }
        else
        {
            NSString * ratioString = [NSString stringForRatioWithDownload: [self downloadedTotal]
                                                upload: [self uploadedTotal]];
        
            [fShortStatusString setString: [NSString stringWithFormat: @"Ratio: %@, ", ratioString]];
            [fRemainingTimeString setString: [@"Ratio: " stringByAppendingString: ratioString]];
        }
        
        stringToAppend = [stringToAppend stringByAppendingString: [@"UL: " stringByAppendingString:
                                                [NSString stringForSpeed: [self uploadRate]]]];

        [fStatusString appendFormat: @" - %@", stringToAppend];
        [fShortStatusString appendString: stringToAppend];
    }
}

- (NSDictionary *) infoForCurrentView
{
    NSMutableDictionary * info = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                    [self name], @"Name",
                                    [NSNumber numberWithBool: [self isSeeding]], @"Seeding",
                                    [NSNumber numberWithFloat: [self progress]], @"Progress",
                                    [NSNumber numberWithBool: [self isActive]], @"Active",
                                    [NSNumber numberWithBool: [self isError]], @"Error", nil];
    
    if (![fDefaults boolForKey: @"SmallView"])
    {
        [info setObject: fIconFlipped forKey: @"Icon"];
        [info setObject: [self progressString] forKey: @"ProgressString"];
        [info setObject: [self statusString] forKey: @"StatusString"];
    }
    else
    {
        [info setObject: fIconSmall forKey: @"Icon"];
        [info setObject: [self remainingTimeString] forKey: @"RemainingTimeString"];
        [info setObject: [self shortStatusString] forKey: @"ShortStatusString"];
    }
    
    if ([fDefaults boolForKey: @"UseAdvancedBar"])
        [info setObject: [self advancedBar] forKey: @"AdvancedBar"];
    
    return info;
}

- (void) startTransfer
{
    fWaitToStart = NO;
    fFinishedSeeding = NO;
    
    if (![self isActive] && [self remainingDiskSpaceForTorrent])
        tr_torrentStart(fHandle);
}

- (void) stopTransfer
{
    fError = NO;
    
    if ([self isActive])
    {
        BOOL wasSeeding = [self isSeeding];
    
        tr_torrentStop(fHandle);

        if (!wasSeeding)
            [[NSNotificationCenter defaultCenter] postNotificationName: @"StoppedDownloading" object: self];
    }
}

- (void) stopTransferForQuit
{
    if ([self isActive])
        tr_torrentStop(fHandle);
}

- (void) removeForever
{
    if (fPrivateTorrent)
        tr_torrentRemoveSaved(fHandle);
}

- (void) sleep
{
    if ((fResumeOnWake = [self isActive]))
        tr_torrentStop(fHandle);
}

- (void) wakeUp
{
    if (fResumeOnWake)
        tr_torrentStart(fHandle);
}

- (float) ratio
{
    float downloaded = [self downloadedTotal];
    return downloaded > 0 ? (float)[self uploadedTotal] / downloaded : -1;
}

- (int) stopRatioSetting
{
	return fStopRatioSetting;
}

- (void) setStopRatioSetting: (int) setting
{
    fStopRatioSetting = setting;
}

- (float) ratioLimit
{
    return fRatioLimit;
}

- (void) setRatioLimit: (float) limit
{
    if (limit >= 0)
        fRatioLimit = limit;
}

- (void) setWaitToStart: (BOOL) wait
{
    fWaitToStart = wait;
}

- (BOOL) waitingToStart
{
    return fWaitToStart;
}

- (void) revealData
{
    [[NSWorkspace sharedWorkspace] selectFile: [self dataLocation] inFileViewerRootedAtPath: nil];
}

- (void) trashData
{
    [self trashFile: [self dataLocation]];
}

- (void) trashTorrent
{
    if (fPublicTorrent)
        [self trashFile: [self publicTorrentLocation]];
}

- (BOOL) remainingDiskSpaceForTorrent
{
    if ([self progress] >= 1.0)
        return YES;
    
    NSString * location = [self dataLocation],
                * volume = [[[NSFileManager defaultManager] componentsToDisplayForPath: location] objectAtIndex: 0];
    NSDictionary * fsAttributes = [[NSFileManager defaultManager] fileSystemAttributesAtPath: location];
    uint64_t remainingSpace = [[fsAttributes objectForKey: NSFileSystemFreeSize] unsignedLongLongValue],
            torrentRemaining = [self size] - (uint64_t)[self downloadedValid];
    
    NSLog(@"Volume: %@", volume);
    NSLog(@"Remaining disk space: %qu (%@)", remainingSpace, [NSString stringForFileSize: remainingSpace]);
    NSLog(@"Torrent remaining size: %qu (%@)", torrentRemaining, [NSString stringForFileSize: torrentRemaining]);
    
    if (volume && remainingSpace <= torrentRemaining)
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat: @"Not enough remaining disk space to download \"%@\" completely.",
                                    [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                        @"The transfer will be paused. Clear up space on %@ to continue.", volume]];
        [alert addButtonWithTitle: @"OK"];
        [alert addButtonWithTitle: @"Download Anyway"];
        
        if ([alert runModal] == NSAlertFirstButtonReturn)
        {
            [[NSNotificationCenter defaultCenter] postNotificationName: @"StoppedDownloading" object: self];
            return NO;
        }
        else
            return YES;
    }
    return YES;
}

- (NSImage *) icon
{
    return fIcon;
}

- (NSImage *) iconFlipped
{
    return fIconFlipped;
}

- (NSImage *) iconSmall
{
    return fIconSmall;
}

- (NSString *) name
{
    return [NSString stringWithUTF8String: fInfo->name];
}

- (uint64_t) size
{
    return fInfo->totalSize;
}

- (NSString *) tracker
{
    return [NSString stringWithFormat: @"%s:%d", fInfo->trackerAddress, fInfo->trackerPort];
}

- (NSString *) announce
{
    return [NSString stringWithUTF8String: fInfo->trackerAnnounce];
}

- (int) pieceSize
{
    return fInfo->pieceSize;
}

- (int) pieceCount
{
    return fInfo->pieceCount;
}

- (NSString *) hashString
{
    return [NSString stringWithUTF8String: fInfo->hashString];
}

- (NSString *) torrentLocation
{
    return [NSString stringWithUTF8String: fInfo->torrent];
}

- (NSString *) publicTorrentLocation
{
    return fPublicTorrentLocation;
}

- (NSString *) torrentLocationString
{
    return fPrivateTorrent ? @"Transmission Support Folder" : [fPublicTorrentLocation stringByAbbreviatingWithTildeInPath];
}

- (NSString *) dataLocation
{
    return [[self downloadFolder] stringByAppendingPathComponent: [self name]];
}

- (BOOL) publicTorrent
{
    return fPublicTorrent;
}

- (BOOL) privateTorrent
{
    return fPrivateTorrent;
}

- (NSString *) stateString
{
    switch( fStat->status )
    {
        case TR_STATUS_PAUSE:
            return @"Paused";
            break;

        case TR_STATUS_CHECK:
            return [@"Checking existing files" stringByAppendingEllipsis];
            break;

        case TR_STATUS_DOWNLOAD:
            return @"Downloading";
            break;

        case TR_STATUS_SEED:
            return @"Seeding";
            break;

        case TR_STATUS_STOPPING:
            return [@"Stopping" stringByAppendingEllipsis];
            break;
        
        default:
            return @"N/A";
    }
}

- (float) progress
{
    return fStat->progress;
}

- (int) eta
{
    return fStat->eta;
}

- (BOOL) isActive
{
    return fStat->status & TR_STATUS_ACTIVE;
}

- (BOOL) isSeeding
{
    return fStat->status == TR_STATUS_SEED;
}

- (BOOL) isPaused
{
    return fStat->status == TR_STATUS_PAUSE;
}

- (BOOL) isError
{
    return fStat->error & TR_ETRACKER;
}

- (BOOL) justFinished
{
    return tr_getFinished(fHandle);
}

- (NSArray *) peers
{
    int totalPeers, i;
    tr_peer_stat_t * peers = tr_torrentPeers(fHandle, & totalPeers);
    
    NSMutableArray * peerDics = [NSMutableArray arrayWithCapacity: totalPeers];
    tr_peer_stat_t peer;
    NSString * client;
    for (i = 0; i < totalPeers; i++)
    {
        peer = peers[i];
        [peerDics addObject: [NSDictionary dictionaryWithObjectsAndKeys:
            [NSNumber numberWithBool: peer.isConnected], @"Connected",
            [NSNumber numberWithBool: peer.isIncoming], @"Incoming",
            [NSString stringWithCString: (char *) peer.addr encoding: NSUTF8StringEncoding], @"IP",
            [NSString stringWithCString: (char *) peer.client encoding: NSUTF8StringEncoding], @"Client",
            [NSNumber numberWithBool: peer.isDownloading], @"UL To",
            [NSNumber numberWithBool: peer.isUploading], @"DL From", nil]];
    }
    
    tr_torrentPeersFree(peers, totalPeers);
    
    return peerDics;
}

- (NSString *) progressString
{
    return fProgressString;
}

- (NSString *) statusString
{
    return fStatusString;
}

- (NSString *) shortStatusString
{
    return fShortStatusString;
}

- (NSString *) remainingTimeString
{
    return fRemainingTimeString;
}

- (int) seeders
{
    return fStat->seeders;
}

- (int) leechers
{
    return fStat->leechers;
}

- (int) totalPeers
{
    return fStat->peersTotal;
}

//peers uploading to you
- (int) peersUploading
{
    return fStat->peersUploading;
}

//peers downloading from you
- (int) peersDownloading
{
    return fStat->peersDownloading;
}

- (float) downloadRate
{
    return fStat->rateDownload;
}

- (float) uploadRate
{
    return fStat->rateUpload;
}

- (float) downloadedValid
{
    return [self progress] * [self size];
}

- (uint64_t) downloadedTotal
{
    return fStat->downloaded;
}

- (uint64_t) uploadedTotal
{
    return fStat->uploaded;
}

- (float) swarmSpeed
{
    return fStat->swarmspeed;
}

- (NSNumber *) orderValue
{
    return [NSNumber numberWithInt: fOrderValue];
}

- (void) setOrderValue: (int) orderValue
{
    fOrderValue = orderValue;
}

- (NSArray *) fileList
{
    int count = fInfo->fileCount, i;
    tr_file_t file;
    NSMutableArray * files = [NSMutableArray arrayWithCapacity: count];
    
    for (i = 0; i < count; i++)
    {
        file = fInfo->files[i];
        [files addObject: [NSDictionary dictionaryWithObjectsAndKeys:
            [[self downloadFolder] stringByAppendingPathComponent: [NSString stringWithUTF8String: file.name]], @"Name",
            [NSNumber numberWithUnsignedLongLong: file.length], @"Size", nil]];
    }
    
    return files;
}

- (NSDate *) date
{
    return fDate;
}

- (NSNumber *) stateSortKey
{
    if (![self isActive])
        return [NSNumber numberWithInt: 0];
    else if ([self isSeeding])
        return [NSNumber numberWithInt: 1];
    else
        return [NSNumber numberWithInt: 2];
}

- (NSNumber *) progressSortKey
{
    //if finished downloading sort by ratio instead of progress
    float progress = [self progress];
    return [NSNumber numberWithFloat: progress < 1.0 ? progress : 2.0 + [self ratio]];
}

@end


@implementation Torrent (Private)

//if a hash is given, attempt to load that; otherwise, attempt to open file at path
- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        privateTorrent: (NSNumber *) privateTorrent publicTorrent: (NSNumber *) publicTorrent
        date: (NSDate *) date stopRatioSetting: (NSNumber *) stopRatioSetting
        ratioLimit: (NSNumber *) ratioLimit waitToStart: (NSNumber *) waitToStart
        orderValue: (NSNumber *) orderValue
{
    if (!(self = [super init]))
        return nil;

    fLib = lib;
    fDefaults = [NSUserDefaults standardUserDefaults];

    fPrivateTorrent = privateTorrent ? [privateTorrent boolValue] : [fDefaults boolForKey: @"SavePrivateTorrent"];
    fPublicTorrent = !fPrivateTorrent || (publicTorrent ? [publicTorrent boolValue]
                                            : ![fDefaults boolForKey: @"DeleteOriginalTorrent"]);

    int error;
    if (fPrivateTorrent && hashString)
        fHandle = tr_torrentInitSaved(fLib, [hashString UTF8String], TR_FSAVEPRIVATE, & error);
    
    if (!fHandle && path)
        fHandle = tr_torrentInit(fLib, [path UTF8String], fPrivateTorrent ? TR_FSAVEPRIVATE : 0, & error);

    if (!fHandle)
    {
        [self release];
        return nil;
    }
    
    fInfo = tr_torrentInfo( fHandle );

    if (fPublicTorrent)
        fPublicTorrentLocation = [path retain];

    fDate = date ? [date retain] : [[NSDate alloc] init];
    
    fStopRatioSetting = stopRatioSetting ? [stopRatioSetting intValue] : -1;
    fRatioLimit = ratioLimit ? [ratioLimit floatValue] : [fDefaults floatForKey: @"RatioLimit"];
    fFinishedSeeding = NO;
    
    fWaitToStart = waitToStart ? [waitToStart boolValue] : [fDefaults boolForKey: @"AutoStartDownload"];
    fOrderValue = orderValue ? [orderValue intValue] : tr_torrentCount(fLib) - 1;
    fError = NO;
    
    NSString * fileType = fInfo->multifile ? NSFileTypeForHFSTypeCode('fldr') : [[self name] pathExtension];
    fIcon = [[NSWorkspace sharedWorkspace] iconForFileType: fileType];
    [fIcon retain];
    
    fIconFlipped = [fIcon copy];
    [fIconFlipped setFlipped: YES];
    
    fIconSmall = [fIconFlipped copy];
    [fIconSmall setScalesWhenResized: YES];
    [fIconSmall setSize: NSMakeSize(16.0, 16.0)];

    fProgressString = [[NSMutableString alloc] initWithCapacity: 50];
    fStatusString = [[NSMutableString alloc] initWithCapacity: 75];
    fShortStatusString = [[NSMutableString alloc] initWithCapacity: 30];
    fRemainingTimeString = [[NSMutableString alloc] initWithCapacity: 30];
    
    //set up advanced bar
    fBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
        pixelsWide: MAX_PIECES pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
        isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    fPieces = malloc(MAX_PIECES);
    int i;
    for (i = 0; i < MAX_PIECES; i++)
        fPieces[i] = BLANK_PIECE;

    [self update];
    return self;
}

- (NSImage *) advancedBar
{
    int h, w;
    uint32_t * p;
    uint8_t * bitmapData = [fBitmap bitmapData];
    int bytesPerRow = [fBitmap bytesPerRow];

    int8_t * pieces = malloc(MAX_PIECES);
    [self getAvailability: pieces size: MAX_PIECES];
    
    //lines 2 to 14: blue, green, or gray depending on whether we have the piece or not
    int have = 0, avail = 0;
    uint32_t color;
    BOOL change;
    for (w = 0; w < MAX_PIECES; w++)
    {
        change = NO;
        if (pieces[w] < 0)
        {
            if (fPieces[w] != -1)
            {
                color = kBlue;
                fPieces[w] = -1;
                change = YES;
            }
            have++;
        }
        else if (pieces[w] == 0)
        {
            if (fPieces[w] != 0)
            {
                color = kGray;
                fPieces[w] = 0;
                change = YES;
            }
        }
        else
        {
            if (pieces[w] == 1)
            {
                if (fPieces[w] != 1)
                {
                    color = kGreen1;
                    fPieces[w] = 1;
                    change = YES;
                }
            }
            else if (pieces[w] == 2)
            {
                if (fPieces[w] != 2)
                {
                    color = kGreen2;
                    fPieces[w] = 2;
                    change = YES;
                }
            }
            else
            {
                if (fPieces[w] != 3)
                {
                    color = kGreen3;
                    fPieces[w] = 3;
                    change = YES;
                }
            }
            avail++;
        }
        
        if (change)
        {
            //point to pixel (w, 2) and draw "vertically"
            p = (uint32_t *)(bitmapData + 2 * bytesPerRow) + w;
            for (h = 2; h < BAR_HEIGHT; h++)
            {
                p[0] = color;
                p = (uint32_t *)((uint8_t *)p + bytesPerRow);
            }
        }
    }
    
    //first two lines: dark blue to show progression, green to show available
    p = (uint32_t *) bitmapData;
    for (w = 0; w < have; w++)
    {
        p[w] = kBlue2;
        p[w + bytesPerRow / 4] = kBlue2;
    }
    for (; w < avail + have; w++)
    {
        p[w] = kGreen3;
        p[w + bytesPerRow / 4] = kGreen3;
    }
    for (; w < MAX_PIECES; w++)
    {
        p[w] = kWhite;
        p[w + bytesPerRow / 4] = kWhite;
    }
    
    free(pieces);
    
    //actually draw image
    NSImage * bar = [[NSImage alloc] initWithSize: [fBitmap size]];
    [bar addRepresentation: fBitmap];
    [bar setScalesWhenResized: YES];
    
    return [bar autorelease];
}

- (void) trashFile: (NSString *) path
{
    //attempt to move to trash
    if (![[NSWorkspace sharedWorkspace] performFileOperation: NSWorkspaceRecycleOperation
            source: [path stringByDeletingLastPathComponent] destination: @""
            files: [NSArray arrayWithObject: [path lastPathComponent]] tag: nil])
    {
        //if cannot trash, just delete it (will work if it is on a remote volume)
        if (![[NSFileManager defaultManager] removeFileAtPath: path handler: nil])
            NSLog([@"Could not trash " stringByAppendingString: path]);
    }
}

@end
