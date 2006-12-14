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

#include "transmission.h"

#define TR_MAX_OPEN_FILES 16 /* That is, real files, not sockets */
#define TR_RESERVED_FDS   16 /* Number of sockets reserved for
                                connections to trackers */

typedef struct tr_openFile_s
{
    char       folder[MAX_PATH_LENGTH];
    char       name[MAX_PATH_LENGTH];
    int        file;
    int        write;

#define STATUS_INVALID 1
#define STATUS_UNUSED  2
#define STATUS_USED    4
#define STATUS_CLOSING 8
    int        status;

    uint64_t   date;

} tr_openFile_t;

struct tr_fd_s
{
    tr_lock_t       lock;
    tr_cond_t       cond;
    
    int             reserved;

    int             normal;
    int             normalMax;

    tr_openFile_t   open[TR_MAX_OPEN_FILES];
};

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int  ErrorFromErrno();
static void CloseFile( tr_fd_t * f, int i );
static int  CheckFolder( char * folder, char * name, int write );



/***********************************************************************
 * tr_fdInit
 **********************************************************************/
tr_fd_t * tr_fdInit()
{
    tr_fd_t * f;
    int i, j, s[4096];

    f = calloc( sizeof( tr_fd_t ), 1 );

    /* Init lock and cond */
    tr_lockInit( &f->lock );
    tr_condInit( &f->cond );

    /* Detect the maximum number of open files or sockets */
    for( i = 0; i < 4096; i++ )
    {
        if( ( s[i] = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
        {
            break;
        }
    }
    for( j = 0; j < i; j++ )
    {
        tr_netClose( s[j] );
    }

    tr_dbg( "%d usable file descriptors", i );

    f->reserved  = 0;
    f->normal    = 0;

    f->normalMax = i - TR_RESERVED_FDS - 10;
        /* To be safe, in case the UI needs to write a preferences file
           or something */

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        f->open[i].status = STATUS_INVALID;
    }

    return f;
}

/***********************************************************************
 * tr_fdFileOpen
 **********************************************************************/
int tr_fdFileOpen( tr_fd_t * f, char * folder, char * name, int write )
{
    int i, winner, ret;
    uint64_t date;
    char * path;

    tr_lockLock( &f->lock );

    /* Is it already open? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].status & STATUS_INVALID ||
            strcmp( folder, f->open[i].folder ) ||
            strcmp( name, f->open[i].name ) )
        {
            continue;
        }
        if( f->open[i].status & STATUS_CLOSING )
        {
            /* File is being closed by another thread, wait until
             * it's done before we reopen it */
            tr_condWait( &f->cond, &f->lock );
            i = -1;
            continue;
        }
        if( f->open[i].write < write )
        {
            /* File is open read-only and needs to be closed then
             * re-opened read-write */
            CloseFile( f, i );
            continue;
        }
        winner = i;
        goto done;
    }

    /* We'll need to open it, make sure that we can */
    if( ( ret = CheckFolder( folder, name, write ) ) )
    {
        tr_err( "Can not open %s in %s (%d)", name, folder, ret );
        tr_lockUnlock( &f->lock );
        return ret;
    }

    /* Can we open one more file? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].status & STATUS_INVALID )
        {
            winner = i;
            goto open;
        }
    }

    for( ;; )
    {
        /* Close the oldest currently unused file */
        date   = tr_date() + 1;
        winner = -1;

        for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
        {
            if( !( f->open[i].status & STATUS_UNUSED ) )
            {
                continue;
            }
            if( f->open[i].date < date )
            {
                winner = i;
                date   = f->open[i].date;
            }
        }

        if( winner >= 0 )
        {
            CloseFile( f, winner );
            goto open;
        }

        /* All used! Wait a bit and try again */
        tr_condWait( &f->cond, &f->lock );
    }

open:
    tr_dbg( "Opening %s in %s", name, folder );
    asprintf( &path, "%s/%s", folder, name );
    f->open[winner].file = open( path, write ? O_RDWR|O_CREAT : O_RDONLY, 0 );
    free( path );
    if( f->open[winner].file < 0 )
    {
        ret = ErrorFromErrno();
        tr_lockUnlock( &f->lock );
        tr_err( "Could not open %s in %s (%d)", name, folder, ret );
        return ret;
    }
    snprintf( f->open[winner].folder, MAX_PATH_LENGTH, "%s", folder );
    snprintf( f->open[winner].name, MAX_PATH_LENGTH, "%s", name );
    f->open[winner].write = write;

done:
    f->open[winner].status = STATUS_USED;
    f->open[winner].date   = tr_date();
    tr_lockUnlock( &f->lock );
    
    return f->open[winner].file;
}

/***********************************************************************
 * tr_fdFileRelease
 **********************************************************************/
void tr_fdFileRelease( tr_fd_t * f, int file )
{
    int i;
    tr_lockLock( &f->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].file == file )
        {
            f->open[i].status = STATUS_UNUSED;
            break;
        }
    }
    
    tr_condSignal( &f->cond );
    tr_lockUnlock( &f->lock );
}

/***********************************************************************
 * tr_fdFileClose
 **********************************************************************/
void tr_fdFileClose( tr_fd_t * f, char * folder, char * name )
{
    int i;

    tr_lockLock( &f->lock );

    /* Is it already open? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].status & STATUS_INVALID )
        {
            continue;
        }
        if( !strcmp( folder, f->open[i].folder ) &&
            !strcmp( name, f->open[i].name ) )
        {
            CloseFile( f, i );
        }
    }

    tr_lockUnlock( &f->lock );
}

/***********************************************************************
 * tr_fdSocketWillCreate
 **********************************************************************/
int tr_fdSocketWillCreate( tr_fd_t * f, int reserved )
{
    int ret;

    tr_lockLock( &f->lock );

    if( reserved )
    {
        if( f->reserved < TR_RESERVED_FDS )
        {
            ret = 0;
            (f->reserved)++;
        }
        else
        {
            ret = 1;
        }
    }
    else
    {
        if( f->normal < f->normalMax )
        {
            ret = 0;
            (f->normal)++;
        }
        else
        {
            ret = 1;
        }
    }

    tr_lockUnlock( &f->lock );

    return ret;
}

/***********************************************************************
 * tr_fdSocketClosed
 **********************************************************************/
void tr_fdSocketClosed( tr_fd_t * f, int reserved )
{
    tr_lockLock( &f->lock );

    if( reserved )
    {
        (f->reserved)--;
    }
    else
    {
        (f->normal)--;
    }

    tr_lockUnlock( &f->lock );
}

/***********************************************************************
 * tr_fdClose
 **********************************************************************/
void tr_fdClose( tr_fd_t * f )
{
    tr_lockClose( &f->lock );
    tr_condClose( &f->cond );
    free( f );
}


/***********************************************************************
 * Local functions
 **********************************************************************/

/***********************************************************************
 * ErrorFromErrno
 **********************************************************************/
static int ErrorFromErrno()
{
    if( errno == EACCES || errno == EROFS )
        return TR_ERROR_IO_PERMISSIONS;
    return TR_ERROR_IO_OTHER;
}

/***********************************************************************
 * CloseFile
 ***********************************************************************
 * We first mark it as closing then release the lock while doing so,
 * because close() may take same time and we don't want to block other
 * threads.
 **********************************************************************/
static void CloseFile( tr_fd_t * f, int i )
{
    if( !( f->open[i].status & STATUS_UNUSED ) )
    {
        tr_err( "CloseFile: status is %d, should be %d",
                f->open[i].status, STATUS_UNUSED );
    }
    tr_dbg( "Closing %s in %s", f->open[i].name, f->open[i].folder );
    f->open[i].status = STATUS_CLOSING;
    tr_lockUnlock( &f->lock );
    close( f->open[i].file );
    tr_lockLock( &f->lock );
    f->open[i].status = STATUS_INVALID;
    tr_condSignal( &f->cond );
}

/***********************************************************************
 * CheckFolder
 ***********************************************************************
 *
 **********************************************************************/
static int CheckFolder( char * folder, char * name, int write )
{
    struct stat sb;
    char * path, * p, * s;
    int ret = 0;

    if( stat( folder, &sb ) || !S_ISDIR( sb.st_mode ) )
    {
        return TR_ERROR_IO_PARENT;
    }

    asprintf( &path, "%s/%s", folder, name );

    p = path + strlen( folder ) + 1;;
    while( ( s = strchr( p, '/' ) ) )
    {
        *s = '\0';
        if( stat( folder, &sb ) )
        {
            /* Sub-folder does not exist */
            if( !write )
            {
                ret = TR_ERROR_IO_OTHER;
                break;
            }
            if( mkdir( path, 0777 ) )
            {
                ret = ErrorFromErrno();
                break;
            }
        }
        else
        {
            if( !S_ISDIR( sb.st_mode ) )
            {
                ret = TR_ERROR_IO_OTHER;
                break;
            }
        }
        *s = '/';
    }
    free( path );

    return ret;
}
