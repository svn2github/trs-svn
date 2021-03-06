/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>

#include "transmission.h"
#include "natpmp.h"
#include "net.h"
#include "peer-mgr.h"
#include "port-forwarding.h"
#include "torrent.h"
#include "trevent.h"
#include "upnp.h"
#include "utils.h"

static const char * getKey( void ) { return _( "Port Forwarding" ); }

struct tr_shared
{
    unsigned int isEnabled      : 1;
    unsigned int isShuttingDown : 1;

    tr_port_forwarding natpmpStatus;
    tr_port_forwarding upnpStatus;

    int bindPort;
    int bindSocket;
    int publicPort;

    tr_handle * h;
    tr_timer  * pulseTimer;

    tr_upnp    * upnp;
    tr_natpmp  * natpmp;
};

/***
****
***/

static const char*
getNatStateStr( int state )
{
    switch( state )
    {
        /* we're in the process of trying to set up port forwarding */
        case TR_PORT_MAPPING:   return _( "Starting" );
        /* we've successfully forwarded the port */
        case TR_PORT_MAPPED:    return _( "Forwarded" );
        /* we're cancelling the port forwarding */
        case TR_PORT_UNMAPPING: return _( "Stopping" );
        /* the port isn't forwarded */
        case TR_PORT_UNMAPPED:  return _( "Not forwarded" );
        case TR_PORT_ERROR:     return "???";
    }

    return "notfound";
}

static void
natPulse( tr_shared * s )
{
    const int port = s->publicPort;
    const int isEnabled = s->isEnabled && !s->isShuttingDown;
    int oldStatus;
    int newStatus;
    
    oldStatus = tr_sharedTraversalStatus( s );
    s->natpmpStatus = tr_natpmpPulse( s->natpmp, port, isEnabled );
    s->upnpStatus = tr_upnpPulse( s->upnp, port, isEnabled );
    newStatus = tr_sharedTraversalStatus( s );

    if( newStatus != oldStatus )
        tr_ninf( getKey(), _( "State changed from \"%s\" to \"%s\"" ),
                 getNatStateStr(oldStatus),
                 getNatStateStr(newStatus) );
}

static void
incomingPeersPulse( tr_shared * s )
{
    if( s->bindSocket >= 0 && ( s->bindPort != s->publicPort ) )
    {
        tr_ninf( getKey(), _( "Closing port %d" ), s->bindPort );
        tr_netClose( s->bindSocket );
        s->bindSocket = -1;
    }

    if( ( s->publicPort > 0 ) && ( s->bindPort != s->publicPort ) )
    {
        int socket;
        errno = 0;
        socket = tr_netBindTCP( s->publicPort );
        if( socket >= 0 ) {
            tr_ninf( getKey(), _( "Opened port %d to listen for incoming peer connections" ), s->publicPort );
            s->bindPort = s->publicPort;
            s->bindSocket = socket;
            listen( s->bindSocket, 5 );
        } else {
            tr_nerr( getKey(), _( "Couldn't open port %d to listen for incoming peer connections (errno %d - %s)" ),
                     s->publicPort, errno, tr_strerror(errno) );
            s->bindPort = -1;
            s->bindSocket = -1;
        }
    }
    
    for( ;; ) /* check for new incoming peer connections */    
    {
        int socket;
        uint16_t port;
        struct in_addr addr;

        if( s->bindSocket < 0 )
            break;

        socket = tr_netAccept( s->bindSocket, &addr, &port );
        if( socket < 0 )
            break;

        tr_peerMgrAddIncoming( s->h->peerMgr, &addr, port, socket );
    }
}

static int
sharedPulse( void * vshared )
{
    int keepPulsing = 1;
    tr_shared * shared = vshared;

    natPulse( shared );

    if( !shared->isShuttingDown )
    {
        incomingPeersPulse( shared );
    }
    else
    {
        tr_ninf( getKey(), _( "Stopped" ) );
        tr_timerFree( &shared->pulseTimer );
        tr_netClose( shared->bindSocket );
        tr_natpmpClose( shared->natpmp );
        tr_upnpClose( shared->upnp );
        shared->h->shared = NULL;
        tr_free( shared );
        keepPulsing = 0;
    }

    return keepPulsing;
}

/***
****
***/

tr_shared *
tr_sharedInit( tr_handle * h, int isEnabled, int publicPort )
{
    tr_shared * s = tr_new0( tr_shared, 1 );

    s->h            = h;
    s->publicPort   = publicPort;
    s->bindPort     = -1;
    s->bindSocket   = -1;
    s->natpmp       = tr_natpmpInit();
    s->upnp         = tr_upnpInit();
    s->pulseTimer   = tr_timerNew( h, sharedPulse, s, 1000 );
    s->isEnabled    = isEnabled ? 1 : 0;
    s->upnpStatus   = TR_PORT_UNMAPPED;
    s->natpmpStatus = TR_PORT_UNMAPPED;

    return s;
}

void
tr_sharedShuttingDown( tr_shared * s )
{
    s->isShuttingDown = 1;
}

void
tr_sharedSetPort( tr_shared * s, int port )
{
    tr_torrent * tor = NULL;

    s->publicPort = port;

    while(( tor = tr_torrentNext( s->h, tor )))
        tr_torrentChangeMyPort( tor );
}

int
tr_sharedGetPeerPort( const tr_shared * s )
{
    return s->publicPort;
}

void
tr_sharedTraversalEnable( tr_shared * s, int isEnabled )
{
    s->isEnabled = isEnabled;
}

int
tr_sharedTraversalIsEnabled( const tr_shared * s )
{
    return s->isEnabled;
}

int
tr_sharedTraversalStatus( const tr_shared * s )
{
    return MAX( s->natpmpStatus, s->upnpStatus );
}
