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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include <arpa/inet.h> /* inet_addr */

#ifndef WIN32
#include <netdb.h>
#include <fcntl.h>
#endif

#include <evutil.h>

#include "transmission.h"
#include "fdlimit.h"
#include "natpmp.h"
#include "net.h"
#include "platform.h"
#include "utils.h"


void
tr_netInit( void )
{
    static int initialized = FALSE;
    if( !initialized )
    {
#ifdef WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
        initialized = TRUE;
    }
}

/***********************************************************************
 * DNS resolution
 * 
 * Synchronous "resolution": only works with character strings
 * representing numbers expressed in the Internet standard `.' notation.
 * Returns a non-zero value if an error occurs.
 **********************************************************************/
int tr_netResolve( const char * address, struct in_addr * addr )
{
    addr->s_addr = inet_addr( address );
    return ( addr->s_addr == 0xFFFFFFFF );
}


/***********************************************************************
 * TCP/UDP sockets
 **********************************************************************/

static int
makeSocketNonBlocking( int fd )
{
    if( fd >= 0 )
    {
#if defined(__BEOS__)
        int flags = 1;
        if( setsockopt( fd, SOL_SOCKET, SO_NONBLOCK,
                        &flags, sizeof( int ) ) < 0 )
#else
        if( evutil_make_socket_nonblocking( fd ) )
#endif
        {
            tr_err( "Couldn't set socket to non-blocking mode (%s)",
                    strerror( sockerrno ) );
            tr_netClose( fd );
            fd = -1;
        }
    }

    return fd;
}

static int
createSocket( int type, int priority )
{
    return makeSocketNonBlocking( tr_fdSocketCreate( type, priority ) );
}

static int
tr_netOpen( const struct in_addr * addr, tr_port_t port,
            int type, int priority )
{
    int s;
    struct sockaddr_in sock;

    if( ( s = createSocket( type, priority ) ) < 0 )
    {
        return -1;
    }

    memset( &sock, 0, sizeof( sock ) );
    sock.sin_family      = AF_INET;
    sock.sin_addr.s_addr = addr->s_addr;
    sock.sin_port        = port;

    if( ( connect( s, (struct sockaddr *) &sock,
                   sizeof( struct sockaddr_in ) ) < 0 )
#ifdef WIN32
        && ( sockerrno != WSAEWOULDBLOCK )
#endif
        && ( sockerrno != EINPROGRESS ) )
    {
        tr_err( "Couldn't connect socket (%s)", strerror( sockerrno ) );
        tr_netClose( s );
        return -1;
    }

    return s;
}
  
int
tr_netOpenTCP( const struct in_addr * addr, tr_port_t port, int priority )
{
    return tr_netOpen( addr, port, SOCK_STREAM, priority );
}

int
tr_netOpenUDP( const struct in_addr * addr, tr_port_t port, int priority )
{
    return tr_netOpen( addr, port, SOCK_DGRAM, priority );
}

#ifdef IP_ADD_MEMBERSHIP
int tr_netMcastOpen( int port, const struct in_addr * addr )
{
    int fd;
    struct ip_mreq req;

    fd = tr_netBindUDP( port );
    if( 0 > fd )
    {
        return -1;
    }

    memset( &req, 0, sizeof( req ) );
    req.imr_multiaddr.s_addr = addr->s_addr;
    req.imr_interface.s_addr = htonl( INADDR_ANY );
    if( setsockopt( fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&req, sizeof ( req ) ) )
    {
        tr_err( "Couldn't join multicast group (%s)", strerror( sockerrno ) );
        tr_netClose( fd );
        return -1;
    }

    return fd;
}
#else /* IP_ADD_MEMBERSHIP */
int tr_netMcastOpen( int port UNUSED, const struct in_addr * addr UNUSED )
{
    return -1;
}
#endif /* IP_ADD_MEMBERSHIP */

static int
tr_netBind( int port, int type )
{
    int s;
    struct sockaddr_in sock;
#if defined( SO_REUSEADDR ) || defined( SO_REUSEPORT )
    int optval;
#endif

    if( ( s = createSocket( type, 1 ) ) < 0 )
    {
        return -1;
    }

#ifdef SO_REUSEADDR
    optval = 1;
    setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof( optval ) );
#endif

#ifdef SO_REUSEPORT
    if( SOCK_DGRAM == type )
    {
        optval = 1;
        setsockopt( s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof( optval ) );
    }
#endif

    memset( &sock, 0, sizeof( sock ) );
    sock.sin_family      = AF_INET;

    /* Leopard closes a SO_REUSEADDR + INADDR_ANY hole, so we can't
     * use INADDR_ANY when binding for nat-pmp. For details, refer to
     * http://www.uwsg.indiana.edu/hypermail/linux/kernel/9902.1/0828.html .
     * This can probably be done cleaner, but since we're only using SOCK_DGRAM
     * for nat-pmp, this quick fix should work. */
    if ( SOCK_DGRAM == type )
        sock.sin_addr.s_addr = inet_addr( PMP_MCAST_ADDR );
    else
        sock.sin_addr.s_addr = INADDR_ANY;

    sock.sin_port        = htons( port );

    if( bind( s, (struct sockaddr *) &sock,
               sizeof( struct sockaddr_in ) ) )
    {
        tr_err( "Couldn't bind port %d: %s", port, strerror(sockerrno) );
        tr_netClose( s );
        return -1;
    }

    return s;
}

int
tr_netBindTCP( int port )
{
    return tr_netBind( port, SOCK_STREAM );
}

int
tr_netBindUDP( int port )
{
    return tr_netBind( port, SOCK_DGRAM );
}


int
tr_netAccept( int b, struct in_addr * addr, tr_port_t * port )
{
    return makeSocketNonBlocking( tr_fdSocketAccept( b, addr, port ) );
}

int
tr_netSend( int s, const void * buf, int size )
{
    const int ret = send( s, buf, size, 0 );
    if( ret >= 0 )
        return ret;

    if( sockerrno == ENOTCONN || sockerrno == EAGAIN || sockerrno == EWOULDBLOCK )
        return TR_NET_BLOCK;

    return TR_NET_CLOSE;
}

int tr_netRecvFrom( int s, uint8_t * buf, int size, struct sockaddr_in * addr )
{
    socklen_t len;
    int       ret;

    len = ( NULL == addr ? 0 : sizeof( *addr ) );
    ret = recvfrom( s, buf, size, 0, ( struct sockaddr * ) addr, &len );
    if( ret < 0 )
    {
        if( sockerrno == EAGAIN || sockerrno == EWOULDBLOCK )
        {
            ret = TR_NET_BLOCK;
        }
        else
        {
            ret = TR_NET_CLOSE;
        }
    }
    if( !ret )
    {
        ret = TR_NET_CLOSE;
    }

    return ret;
}

void
tr_netClose( int s )
{
    tr_fdSocketClose( s );
}

void
tr_netNtop( const struct in_addr * addr, char * buf, int len )
{
    const uint8_t * cast;

    cast = (const uint8_t *)addr;
    snprintf( buf, len, "%hhu.%hhu.%hhu.%hhu",
              cast[0], cast[1], cast[2], cast[3] );
}
