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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>

#ifdef WIN32
 #include <winsock2.h> /* inet_addr */
 #include <WS2tcpip.h>
#else
 #include <arpa/inet.h> /* inet_addr */
 #include <netdb.h>
 #include <fcntl.h>
#endif
#include <unistd.h>

#include <stdarg.h> /* 1.4.x versions of evutil.h need this */
#include <evutil.h>

#include "transmission.h"
#include "fdlimit.h"
#include "natpmp.h"
#include "net.h"
#include "peer-io.h"
#include "platform.h"
#include "session.h"
#include "utils.h"

#ifndef IN_MULTICAST
#define IN_MULTICAST( a ) ( ( ( a ) & 0xf0000000 ) == 0xe0000000 )
#endif

const tr_address tr_in6addr_any = { TR_AF_INET6, { IN6ADDR_ANY_INIT } }; 
const tr_address tr_inaddr_any = { TR_AF_INET, { { { { INADDR_ANY, 0x00, 0x00, 0x00 } } } } }; 

#ifdef WIN32
static const char *
inet_ntop( int af, const void *src, char *dst, socklen_t cnt )
{
    if (af == AF_INET)
    {
        struct sockaddr_in in;
        memset( &in, 0, sizeof( in ) );
        in.sin_family = AF_INET;
        memcpy( &in.sin_addr, src, sizeof( struct in_addr ) );
        getnameinfo((struct sockaddr *)&in, sizeof(struct sockaddr_in),
                    dst, cnt, NULL, 0, NI_NUMERICHOST);
        return dst;
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 in;
        memset( &in, 0, sizeof( in ) );
        in.sin6_family = AF_INET6;
        memcpy( &in.sin6_addr, src, sizeof( struct in_addr6 ) );
        getnameinfo((struct sockaddr *)&in, sizeof(struct sockaddr_in6),
                    dst, cnt, NULL, 0, NI_NUMERICHOST);
        return dst;
    }
    return NULL;
}

static int
inet_pton(int af, const char *src, void *dst)
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *ressave;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;

    if (getaddrinfo(src, NULL, &hints, &res) != 0)
        return -1;

    ressave = res;

    while (res)
    {
        memcpy(dst, res->ai_addr, res->ai_addrlen);
        res = res->ai_next;
    }

    freeaddrinfo(ressave);
    return 0;
}

#endif


void
tr_netInit( void )
{
    static int initialized = FALSE;

    if( !initialized )
    {
#ifdef WIN32
        WSADATA wsaData;
        WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
#endif
        initialized = TRUE;
    }
}

const char * 
tr_ntop( const tr_address * src, char * dst, int size ) 
{
    assert( tr_isAddress( src ) );

    if( src->type == TR_AF_INET ) 
        return inet_ntop( AF_INET, &src->addr, dst, size ); 
    else 
        return inet_ntop( AF_INET6, &src->addr, dst, size ); 
} 

/* 
 * Non-threadsafe version of tr_ntop, which uses a static memory area for a buffer. 
 * This function is suitable to be called from libTransmission's networking code, 
 * which is single-threaded. 
 */ 
const char * 
tr_ntop_non_ts( const tr_address * src ) 
{ 
    static char buf[INET6_ADDRSTRLEN]; 
    return tr_ntop( src, buf, sizeof( buf ) ); 
} 

tr_address * 
tr_pton( const char * src, tr_address * dst ) 
{ 
    int retval = inet_pton( AF_INET, src, &dst->addr ); 
    assert( dst );
    if( retval < 0 ) 
        return NULL; 
    else if( retval == 0 ) 
        retval = inet_pton( AF_INET6, src, &dst->addr ); 
    else
    { 
        dst->type = TR_AF_INET; 
        return dst; 
    } 

    if( retval < 1 ) 
        return NULL; 
    dst->type = TR_AF_INET6; 
    return dst; 
}

/* 
 * Compare two tr_address structures. 
 * Returns: 
 * <0 if a < b 
 * >0 if a > b 
 * 0  if a == b 
 */ 
int
tr_compareAddresses( const tr_address * a, const tr_address * b)
{
    static const int sizes[2] = { sizeof(struct in_addr), sizeof(struct in6_addr) };

    assert( tr_isAddress( a ) );
    assert( tr_isAddress( b ) );

    /* IPv6 addresses are always "greater than" IPv4 */ 
    if( a->type != b->type )
        return a->type == TR_AF_INET ? 1 : -1;

    return memcmp( &a->addr, &b->addr, sizes[a->type] );
} 

/***********************************************************************
 * TCP sockets
 **********************************************************************/

int
tr_netSetTOS( int s, int tos )
{
#ifdef IP_TOS
    return setsockopt( s, IPPROTO_IP, IP_TOS, (char*)&tos, sizeof( tos ) );
#else
    return 0;
#endif
}

static socklen_t
setup_sockaddr( const tr_address        * addr,
                tr_port                   port,
                struct sockaddr_storage * sockaddr)
{
    assert( tr_isAddress( addr ) );

    if( addr->type == TR_AF_INET )
    {
        struct sockaddr_in  sock4;
        memset( &sock4, 0, sizeof( sock4 ) );
        sock4.sin_family      = AF_INET;
        sock4.sin_addr.s_addr = addr->addr.addr4.s_addr;
        sock4.sin_port        = port;
        memcpy( sockaddr, &sock4, sizeof( sock4 ) );
        return sizeof( struct sockaddr_in );
    }
    else
    {
        struct sockaddr_in6 sock6;
        memset( &sock6, 0, sizeof( sock6 ) );
        sock6.sin6_family   = AF_INET6;
        sock6.sin6_port     = port;
        sock6.sin6_flowinfo = 0;
        sock6.sin6_addr     = addr->addr.addr6;
        memcpy( sockaddr, &sock6, sizeof( sock6 ) );
        return sizeof( struct sockaddr_in6 );
    }
}

static tr_bool
isMulticastAddress( const tr_address * addr )
{
    if( addr->type == TR_AF_INET && IN_MULTICAST( htonl( addr->addr.addr4.s_addr ) ) )
        return TRUE;

    if( addr->type == TR_AF_INET6 && ( addr->addr.addr6.s6_addr[0] == 0xff ) )
        return TRUE;

    return FALSE;
}

static TR_INLINE tr_bool
isIPv4MappedOrCompatAddress( const tr_address * addr )
{
    if( addr->type == TR_AF_INET6 )
    {
        if( IN6_IS_ADDR_V4MAPPED( &addr->addr.addr6 ) ||
            IN6_IS_ADDR_V4COMPAT( &addr->addr.addr6 ) )
            return TRUE;
    }
    return FALSE;
}

static TR_INLINE tr_bool
isIPv6LinkLocalAddress( const tr_address * addr )
{
    if( addr->type == TR_AF_INET6 &&
        IN6_IS_ADDR_LINKLOCAL( &addr->addr.addr6 ) )
        return TRUE;
    return FALSE;
}

tr_bool
tr_isValidPeerAddress( const tr_address * addr, tr_port port )
{
    if( isMulticastAddress( addr ) || isIPv6LinkLocalAddress( addr ) ||
        isIPv4MappedOrCompatAddress( addr ) )
        return FALSE;

    if( port == 0 )
        return FALSE;

    return TRUE;
}

int
tr_netOpenTCP( tr_session        * session,
               const tr_address  * addr,
               tr_port             port )
{
    static const int domains[NUM_TR_AF_INET_TYPES] = { AF_INET, AF_INET6 };
    int                     s;
    struct sockaddr_storage sock;
    socklen_t               addrlen;
    const tr_address      * source_addr;
    socklen_t               sourcelen;
    struct sockaddr_storage source_sock;

    assert( tr_isAddress( addr ) );

    if( isMulticastAddress( addr ) || isIPv6LinkLocalAddress( addr ) )
        return -EINVAL;

    s = tr_fdSocketCreate( domains[addr->type], SOCK_STREAM );
    if( s < 0 )
        return -1;

    if( evutil_make_socket_nonblocking( s ) < 0 ) {
        tr_netClose( s );
        return -1;
    }

    addrlen = setup_sockaddr( addr, port, &sock );
    
    /* set source address */
    source_addr = tr_sessionGetPublicAddress( session, addr->type );
    assert( source_addr );
    sourcelen = setup_sockaddr( source_addr, 0, &source_sock );
    if( bind( s, ( struct sockaddr * ) &source_sock, sourcelen ) )
    {
        tr_err( _( "Couldn't set source address %s on %d: %s" ),
                tr_ntop_non_ts( source_addr ), s, tr_strerror( errno ) );
        return -errno;
    }

    if( ( connect( s, (struct sockaddr *) &sock,
                  addrlen ) < 0 )
#ifdef WIN32
      && ( sockerrno != WSAEWOULDBLOCK )
#endif
      && ( sockerrno != EINPROGRESS ) )
    {
        int tmperrno;
        tmperrno = sockerrno;
        if( ( tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH )
                || addr->type == TR_AF_INET )
            tr_err( _( "Couldn't connect socket %d to %s, port %d (errno %d - %s)" ),
                    s, tr_ntop_non_ts( addr ), (int)port, tmperrno,
                    tr_strerror( tmperrno ) );
        tr_netClose( s );
        s = -tmperrno;
    }

    tr_deepLog( __FILE__, __LINE__, NULL, "New OUTGOING connection %d (%s)",
               s, tr_peerIoAddrStr( addr, port ) );

    return s;
}

static int
tr_netBindTCPImpl( const tr_address * addr, tr_port port, tr_bool suppressMsgs, int * errOut )
{
    static const int domains[NUM_TR_AF_INET_TYPES] = { AF_INET, AF_INET6 };
    struct sockaddr_storage sock;
    int fd;
    int addrlen;
    int optval;

    assert( tr_isAddress( addr ) );

    fd = socket( domains[addr->type], SOCK_STREAM, 0 );
    if( fd < 0 ) {
        *errOut = sockerrno;
        return -1;
    }

    if( evutil_make_socket_nonblocking( fd ) < 0 ) {
        *errOut = sockerrno;
        EVUTIL_CLOSESOCKET( fd );
        return -1;
    }

    optval = 1;
    setsockopt( fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval) );
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) );

#ifdef IPV6_V6ONLY
    if( addr->type == TR_AF_INET6 )
        if( setsockopt( fd, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof( optval ) ) == -1 )
            if( sockerrno != ENOPROTOOPT ) { /* if the kernel doesn't support it, ignore it */
                *errOut = sockerrno;
                return -1;
            }
#endif

    addrlen = setup_sockaddr( addr, htons( port ), &sock );
    if( bind( fd, (struct sockaddr *) &sock, addrlen ) ) {
        const int err = sockerrno;
        if( !suppressMsgs )
            tr_err( _( "Couldn't bind port %d on %s: %s" ),
                    port, tr_ntop_non_ts( addr ), tr_strerror( err ) );
        EVUTIL_CLOSESOCKET( fd );
        *errOut = err;
        return -1;
    }

    if( !suppressMsgs )
        tr_dbg( "Bound socket %d to port %d on %s", fd, port, tr_ntop_non_ts( addr ) );

    if( listen( fd, 128 ) == -1 ) {
        *errOut = sockerrno;
        EVUTIL_CLOSESOCKET( fd );
        return -1;
    }

    return fd;
}

int
tr_netBindTCP( const tr_address * addr, tr_port port, tr_bool suppressMsgs )
{
    int unused;
    return tr_netBindTCPImpl( addr, port, suppressMsgs, &unused );
}

tr_bool
tr_net_hasIPv6( tr_port port )
{
    static tr_bool result = FALSE;
    static tr_bool alreadyDone = FALSE;

    if( !alreadyDone )
    {
        int err;
        int fd = tr_netBindTCPImpl( &tr_in6addr_any, port, TRUE, &err );
        if( fd >= 0 || err != EAFNOSUPPORT ) /* we support ipv6 */
            result = TRUE;
        if( fd >= 0 )
            EVUTIL_CLOSESOCKET( fd );
        alreadyDone = TRUE;
    }

    return result;
}

int
tr_netAccept( tr_session  * session UNUSED,
              int           b,
              tr_address  * addr,
              tr_port     * port )
{
    int fd = tr_fdSocketAccept( b, addr, port );

    if( fd>=0 && evutil_make_socket_nonblocking(fd)<0 ) {
        tr_netClose( fd );
        fd = -1;
    }

    return fd;
}

void
tr_netClose( int s )
{
    tr_fdSocketClose( s );
}
