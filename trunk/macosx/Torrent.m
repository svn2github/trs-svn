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

#import "Torrent.h"
#import "StringAdditions.h"
#import "Utils.h"

@interface Torrent (Private)

- (void) trashPath: (NSString *) path;

@end


@implementation Torrent

- (id) initWithPath: (NSString *) path lib: (tr_handle_t *) lib date: (NSDate *) date
{
    fLib = lib;

    int error;
    fHandle = tr_torrentInit( fLib, [path UTF8String], &error );
    if( !fHandle )
    {
        [self release];
        return nil;
    }

    fDate = [date retain];
    fInfo = tr_torrentInfo( fHandle );

    NSString * fileType = ( fInfo->fileCount > 1 ) ?
        NSFileTypeForHFSTypeCode('fldr') : [[self name] pathExtension];
    fIcon = [[NSWorkspace sharedWorkspace] iconForFileType: fileType];
    [fIcon setFlipped: YES];
    [fIcon retain];
    fIconNonFlipped = [[NSWorkspace sharedWorkspace] iconForFileType: fileType];
    [fIconNonFlipped retain];

    fStatusString   = [[NSMutableString alloc] initWithCapacity: 50];
    fInfoString     = [[NSMutableString alloc] initWithCapacity: 50];
    fDownloadString = [[NSMutableString alloc] initWithCapacity: 10];
    fUploadString   = [[NSMutableString alloc] initWithCapacity: 10];

    [self update];
    return self;
}

- (id) initWithPath: (NSString *) path lib: (tr_handle_t *) lib
{
    return [self initWithPath: path lib: lib date: [NSDate date]];
}

- (void) dealloc
{
    if( fHandle )
    {
        tr_torrentClose( fLib, fHandle );
        [fDate release];
        [fIcon release];
        [fIconNonFlipped release];
        [fStatusString release];
        [fInfoString release];
        [fDownloadString release];
        [fUploadString release];
    }
    [super dealloc];
}

- (void) setFolder: (NSString *) path
{
    tr_torrentSetFolder( fHandle, [path UTF8String] );
}

- (NSString *) getFolder
{
    return [NSString stringWithUTF8String: tr_torrentGetFolder( fHandle )];
}

- (void) getAvailability: (int8_t *) tab size: (int) size
{
    tr_torrentAvailability( fHandle, tab, size );
}

- (void) update
{
    fStat = tr_torrentStat( fHandle );

    [fStatusString setString: @""];
    [fInfoString   setString: @""];

    switch( fStat->status )
    {
        case TR_STATUS_PAUSE:
            [fStatusString appendFormat: @"Paused (%.2f %%)",
                100 * fStat->progress];
            break;

        case TR_STATUS_CHECK:
            [fStatusString appendFormat:
                @"Checking existing files (%.2f %%)",
                100 * fStat->progress];
            break;

        case TR_STATUS_DOWNLOAD:
            if( fStat->eta < 0 )
            {
                [fStatusString appendFormat:
                    @"Finishing in --:--:-- (%.2f %%)",
                    100 * fStat->progress];
            }
            else
            {
                [fStatusString appendFormat:
                    @"Finishing in %02d:%02d:%02d (%.2f %%)",
                    fStat->eta / 3600, ( fStat->eta / 60 ) % 60,
                    fStat->eta % 60, 100 * fStat->progress];
            }
            [fInfoString appendFormat:
                @"Downloading from %d of %d peer%s",
                fStat->peersUploading, fStat->peersTotal,
                ( fStat->peersTotal == 1 ) ? "" : "s"];
            break;

        case TR_STATUS_SEED:
            [fStatusString appendFormat:
                @"Seeding, uploading to %d of %d peer%s",
                fStat->peersDownloading, fStat->peersTotal,
                ( fStat->peersTotal == 1 ) ? "" : "s"];
            break;

        case TR_STATUS_STOPPING:
            [fStatusString setString: [@"Stopping"
                stringByAppendingString: NS_ELLIPSIS]];
            break;
    }

    if( fStat->error & TR_ETRACKER )
    {
        [fInfoString setString: [@"Error: " stringByAppendingString:
            [NSString stringWithUTF8String: fStat->trackerError]]];
    }

    if( fStat->progress == 1.0 )
    {
        [fDownloadString setString: [@"Ratio: " stringByAppendingString:
            [NSString stringForRatio: fStat->downloaded
            upload: fStat->uploaded]]];
    }
    else
    {
        [fDownloadString setString: [@"DL: " stringByAppendingString:
            [NSString stringForSpeed: fStat->rateDownload]]];
    }
    [fUploadString setString: [@"UL: " stringByAppendingString:
        [NSString stringForSpeed: fStat->rateUpload]]];
}

- (void) start
{
    if( fStat->status & TR_STATUS_INACTIVE )
    {
        tr_torrentStart( fHandle );
    }
}

- (void) stop
{
    if( fStat->status & TR_STATUS_ACTIVE )
    {
        tr_torrentStop( fHandle );
    }
}

- (void) sleep
{
    if( ( fResumeOnWake = ( fStat->status & TR_STATUS_ACTIVE ) ) )
    {
        [self stop];
    }
}

- (void) wakeUp
{
    if( fResumeOnWake )
    {
        [self start];
    }
}

- (void) reveal
{
    [[NSWorkspace sharedWorkspace] selectFile: [[self getFolder]
        stringByAppendingPathComponent: [self name]]
        inFileViewerRootedAtPath: nil];
}

- (void) trashTorrent
{
    [self trashPath: [self path]];
}

- (void) trashData
{
    [self trashPath: [[self getFolder]
        stringByAppendingPathComponent: [self name]]];
}

- (NSImage *) icon
{
    return fIcon;
}

- (NSImage *) iconNonFlipped
{
    return fIconNonFlipped;
}

- (NSString *) path
{
    return [NSString stringWithUTF8String: fInfo->torrent];
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
    return [NSString stringWithFormat: @"%s:%d",
            fInfo->trackerAddress, fInfo->trackerPort];
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

- (NSString *) hash1
{
    NSMutableString * string = [NSMutableString
        stringWithCapacity: SHA_DIGEST_LENGTH];
    int i;
    for( i = 0; i < SHA_DIGEST_LENGTH / 2; i++ )
    {
        [string appendFormat: @"%02x", fInfo->hash[i]];
    }
    return string;
}
- (NSString *) hash2
{
    NSMutableString * string = [NSMutableString
        stringWithCapacity: SHA_DIGEST_LENGTH];
    int i;
    for( i = SHA_DIGEST_LENGTH / 2; i < SHA_DIGEST_LENGTH; i++ )
    {
        [string appendFormat: @"%02x", fInfo->hash[i]];
    }
    return string;
}

- (float) progress
{
    return fStat->progress;
}

- (BOOL) isActive
{
    return ( fStat->status & TR_STATUS_ACTIVE );
}

- (BOOL) isSeeding
{
    return ( fStat->status == TR_STATUS_SEED );
}

- (BOOL) isPaused
{
    return ( fStat->status == TR_STATUS_PAUSE );
}

- (BOOL) justFinished
{
    return tr_getFinished( fHandle );
}

- (NSString *) statusString
{
    return fStatusString;
}

- (NSString *) infoString
{
    return fInfoString;
}

- (NSString *) downloadString
{
    return fDownloadString;
}

- (NSString *) uploadString
{
    return fUploadString;
}

- (int) seeders
{
    return fStat->seeders;
}

- (int) leechers
{
    return fStat->leechers;
}

- (uint64_t) downloaded
{
    return fStat->downloaded;
}

- (uint64_t) uploaded
{
    return fStat->uploaded;
}

- (NSDate *) date
{
    return fDate;
}

@end


@implementation Torrent (Private)

- (void) trashPath: (NSString *) path
{
    if( ![[NSWorkspace sharedWorkspace] performFileOperation:
           NSWorkspaceRecycleOperation source:
           [path stringByDeletingLastPathComponent]
           destination: @""
           files: [NSArray arrayWithObject: [path lastPathComponent]]
           tag: nil] )
    {
        /* We can't move it to the trash, let's try just to delete it
           (will work if it is on a remote volume) */
        if( ![[NSFileManager defaultManager]
                removeFileAtPath: path handler: nil] )
        {
            NSLog( [@"Could not trash " stringByAppendingString: path] );
        }
    }
}

@end
