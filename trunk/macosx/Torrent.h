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

#import <Cocoa/Cocoa.h>
#import <transmission.h>

#define RATIO_CHECK 1
#define RATIO_GLOBAL -1
#define RATIO_NO_CHECK 0

@interface Torrent : NSObject
{
    tr_handle_t  * fLib;
    tr_torrent_t * fHandle;
    tr_info_t    * fInfo;
    tr_stat_t    * fStat;
    
    BOOL         fResumeOnWake;
    NSDate       * fDate;
    
    BOOL        fPrivateTorrent, fPublicTorrent;
    NSString    * fPublicTorrentLocation;

    NSUserDefaults  * fDefaults;

    NSImage         * fIcon, * fIconFlipped, * fIconSmall;
    NSMutableString * fNameString, * fProgressString, * fStatusString, * fShortStatusString, * fRemainingTimeString;
    
    int     fStopRatioSetting;
    float   fRatioLimit;
    BOOL    fFinishedSeeding, fWaitToStart, fError;
    
    int fOrderValue;
    
    NSBitmapImageRep * fBitmap;
    int8_t * fPieces;
}

- (id)  initWithPath: (NSString *) path lib: (tr_handle_t *) lib;
- (id)  initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib;

- (NSDictionary *) history;
                    
- (void)       setDownloadFolder: (NSString *) path;
- (NSString *) downloadFolder;

- (void) getAvailability: (int8_t *) tab size: (int) size;
- (void) getAmountFinished: (float *) tab size: (int) size;

- (void)            update;
- (NSDictionary *)  infoForCurrentView;

- (void)        startTransfer;
- (void)        stopTransfer;
- (void)        stopTransferForQuit;
- (void)        removeForever;
- (void)        sleep;
- (void)        wakeUp;

- (float)       ratio;
- (int)         stopRatioSetting;
- (void)        setStopRatioSetting: (int) setting;
- (float)       ratioLimit;
- (void)        setRatioLimit: (float) limit;

- (void) setWaitToStart: (BOOL) wait;
- (BOOL) waitingToStart;

- (void)    revealData;
- (void)    trashData;
- (void)    trashTorrent;

- (BOOL) remainingDiskSpaceForTorrent;

- (NSImage *)   icon;
- (NSImage *)   iconFlipped;
- (NSImage *)   iconSmall;

- (NSString *) name;
- (uint64_t)   size;
- (NSString *) tracker;
- (NSString *) announce;
- (int)        pieceSize;
- (int)        pieceCount;
- (NSString *) hashString;

- (NSString *) torrentLocation;
- (NSString *) publicTorrentLocation;
- (NSString *) torrentLocationString;
- (NSString *) dataLocation;

- (BOOL) publicTorrent;
- (BOOL) privateTorrent;

- (NSString *) stateString;

- (float)   progress;
- (int)     eta;

- (BOOL)    isActive;
- (BOOL)    isSeeding;
- (BOOL)    isPaused;
- (BOOL)    isError;
- (BOOL)    justFinished;

- (NSArray *) peers;

- (NSString *) progressString;
- (NSString *) statusString;
- (NSString *) shortStatusString;
- (NSString *) remainingTimeString;

- (int) seeders;
- (int) leechers;
- (int) totalPeers;
- (int) totalPeersIncoming;
- (int) totalPeersOutgoing;
- (int) peersUploading;
- (int) peersDownloading;

- (float)       downloadRate;
- (float)       uploadRate;
- (float)       downloadedValid;
- (uint64_t)    downloadedTotal;
- (uint64_t)    uploadedTotal;
- (float)       swarmSpeed;

- (NSNumber *) orderValue;
- (void) setOrderValue: (int) orderValue;

- (NSArray *) fileList;

- (NSDate *) date;
- (NSNumber *) stateSortKey;
- (NSNumber *) progressSortKey;

@end
