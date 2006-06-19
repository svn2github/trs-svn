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
#import "Torrent.h"

@interface InfoWindowController : NSWindowController
{
    NSMutableArray * fTorrents, * fFiles;
    NSImage * fAppIcon;
    
    IBOutlet NSTabView * fTabView;

    IBOutlet NSImageView * fImageView;
    IBOutlet NSTextField * fNameField, * fSizeField, * fTrackerField,
                        * fAnnounceField, * fPieceSizeField, * fPiecesField,
                        * fHashField,
                        * fTorrentLocationField, * fDataLocationField,
                        * fDateStartedField,
#if 0
                        * fStateField, * fPercentField,
                        * fDownloadRateField, * fUploadRateField,
#endif
                        * fDownloadedValidField, * fDownloadedTotalField, * fUploadedTotalField,
                        * fRatioField, * fSeedersField, * fLeechersField,
                        * fConnectedPeersField, * fDownloadingFromField, * fUploadingToField;

    IBOutlet NSTableView * fFileTable;
    
    IBOutlet NSMatrix * fRatioMatrix;
    IBOutlet NSTextField * fRatioLimitField;
}

- (void) updateInfoForTorrents: (NSArray *) torrents;
- (void) updateInfoStats;

- (void) setNextTab;
- (void) setPreviousTab;
- (void) revealFile: (id) sender;

- (void) setRatioCheck: (id) sender;
- (void) setRatioLimit: (id) sender;

@end
