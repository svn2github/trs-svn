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

#include "transmission.h"

#define SSDP_ADDR               "239.255.255.250"
#define SSDP_PORT               1900
#define SSDP_TYPE               "upnp:rootdevice"
#define SSDP_SUBTYPE            "ssdp:alive"
#define SSDP_FIRST_DELAY        3750    /* 3 3/4 seconds */
#define SSDP_MAX_DELAY          1800000 /* 30 minutes */
#define UPNP_SERVICE_TYPE       "urn:schemas-upnp-org:service:WANIPConnection:1"
#define SOAP_ENVELOPE           "http://schemas.xmlsoap.org/soap/envelope/"
#define LOOP_DETECT_THRESHOLD   10 /* error on 10 add/get/del state changes */
#define MAPPING_CHECK_INTERVAL  900000 /* 15 minutes */
#define HTTP_REQUEST_INTERVAL   500 /* half a second */
#define IGD_GENERIC_ERROR       500
#define IGD_GENERIC_FAILED      501
#define IGD_NO_MAPPING_EXISTS   714
#define IGD_ADD_CONFLICT        718
#define IGD_NO_DYNAMIC_MAPPING  725

typedef struct tr_upnp_action_s
{
    char * name;
    char * action;
    int    len;
    struct { char * name; char * var; char dir; } * args;
} tr_upnp_action_t;

typedef struct tr_upnp_device_s
{
    char                    * id;
    char                    * host;
    char                    * root;
    int                       port;
    char                    * soap;
    char                    * scpd;
    int                       mappedport;
    char                    * myaddr;
#define UPNPDEV_STATE_ROOT              1
#define UPNPDEV_STATE_SCPD              2
#define UPNPDEV_STATE_READY             3
#define UPNPDEV_STATE_ADD               4
#define UPNPDEV_STATE_GET               5
#define UPNPDEV_STATE_DEL               6
#define UPNPDEV_STATE_MAPPED            7
#define UPNPDEV_STATE_ERROR             8
    uint8_t                   state;
    uint8_t                   looping;
    uint64_t                  lastrequest;
    uint64_t                  lastcheck;
    unsigned int              soapretry : 1;
    tr_http_t               * http;
    tr_upnp_action_t          getcmd;
    tr_upnp_action_t          addcmd;
    tr_upnp_action_t          delcmd;
    struct tr_upnp_device_s * next;
} tr_upnp_device_t;

struct tr_upnp_s
{
    int                port;
    int                infd;
    int                outfd;
    uint64_t           lastdiscover;
    uint64_t           lastdelay;
    unsigned int       active : 1;
    unsigned int       discovering : 1;
    tr_upnp_device_t * devices;
    tr_fd_t          * fdlimit;
    tr_lock_t          lock;
};

static int
sendSSDP( tr_fd_t * fdlimit, int fd );
static int
mcastStart( tr_fd_t * fdlimit );
static void
killSock( tr_fd_t * fdlimit, int * sock );
static void
killHttp( tr_fd_t * fdlimit, tr_http_t ** http );
static int
watchSSDP( tr_upnp_device_t ** devices, int fd );
static tr_tristate_t
recvSSDP( int fd, char * buf, int * len );
static int
parseSSDP( char * buf, int len, tr_http_header_t * headers );
static void
deviceAdd( tr_upnp_device_t ** first, const char * id, int idLen,
           const char * url, int urlLen );
static void
deviceRemove( tr_upnp_device_t ** prevptr, tr_fd_t * fdlimit );
static int
deviceStop( tr_upnp_device_t * dev );
static int
devicePulse( tr_upnp_device_t * dev, tr_fd_t * fdlimit, int port );
static int
devicePulseHttp( tr_upnp_device_t * dev, tr_fd_t * fdlimit,
                 const char ** body, int * len );
static tr_http_t *
devicePulseGetHttp( tr_upnp_device_t * dev, tr_fd_t * fdlimit );
static int
parseRoot( const char *buf, int len, char ** soap, char ** scpd );
static int
parseScpd( const char *buf, int len, tr_upnp_action_t * getcmd,
           tr_upnp_action_t * addcmd, tr_upnp_action_t * delcmd );
static int
parseScpdArgs( const char * buf, const char * end,
               tr_upnp_action_t * action, char dir );
static int
parseMapping( tr_upnp_device_t * dev, const char * buf, int len );
static char *
joinstrs( const char *, const char *, const char * );
static tr_http_t *
soapRequest( int retry, const char * host, int port, const char * path,
             tr_upnp_action_t * action, ... );
static void
actionSetup( tr_upnp_action_t * action, const char * name, int prealloc );
static void
actionFree( tr_upnp_action_t * action );
static int
actionAdd( tr_upnp_action_t * action, char * name, char * var,
                      char dir );
#define actionLookupVar( act, nam, len, dir ) \
    ( actionLookup( (act), (nam), (len), (dir), 0 ) )
#define actionLookupName( act, var, len, dir ) \
    ( actionLookup( (act), (var), (len), (dir), 1 ) )
static const char *
actionLookup( tr_upnp_action_t * action, const char * key, int len,
              char dir, int getname );

tr_upnp_t *
tr_upnpInit( tr_fd_t * fdlimit )
{
    tr_upnp_t * upnp;

    upnp = calloc( 1, sizeof( *upnp ) );
    if( NULL == upnp )
    {
        return NULL;
    }

    upnp->infd     = -1;
    upnp->outfd    = -1;
    upnp->fdlimit  = fdlimit;

    tr_lockInit( &upnp->lock );

    return upnp;
}

void
tr_upnpStart( tr_upnp_t * upnp )
{
    tr_lockLock( &upnp->lock );

    if( !upnp->active )
    {
        tr_inf( "starting upnp" );
        upnp->active = 1;
        upnp->discovering = 1;
        upnp->infd = mcastStart( upnp->fdlimit );
        upnp->lastdiscover = 0;
        upnp->lastdelay = SSDP_FIRST_DELAY / 2;
    }

    tr_lockUnlock( &upnp->lock );
}

void
tr_upnpStop( tr_upnp_t * upnp )
{
    tr_lockLock( &upnp->lock );

    if( upnp->active )
    {
        tr_inf( "stopping upnp" );
        upnp->active = 0;
        killSock( upnp->fdlimit, &upnp->infd );
        killSock( upnp->fdlimit, &upnp->outfd );
    }

    tr_lockUnlock( &upnp->lock );
}

int
tr_upnpStatus( tr_upnp_t * upnp )
{
    tr_upnp_device_t * ii;
    int                ret;

    tr_lockLock( &upnp->lock );

    if( !upnp->active )
    {
        ret = ( NULL == upnp->devices ?
                TR_NAT_TRAVERSAL_DISABLED : TR_NAT_TRAVERSAL_UNMAPPING );
    }
    else if( NULL == upnp->devices )
    {
        ret = TR_NAT_TRAVERSAL_NOTFOUND;
    }
    else
    {
        ret = TR_NAT_TRAVERSAL_MAPPING;
        for( ii = upnp->devices; NULL != ii; ii = ii->next )
        {
            if( UPNPDEV_STATE_ERROR == ii->state )
            {
                ret = TR_NAT_TRAVERSAL_ERROR;
            }
            else if( 0 < ii->mappedport )
            {
                ret = TR_NAT_TRAVERSAL_MAPPED;
                break;
            }
        }
    }

    tr_lockUnlock( &upnp->lock );

    return ret;
}

void
tr_upnpForwardPort( tr_upnp_t * upnp, int port )
{
    tr_lockLock( &upnp->lock );
    tr_dbg( "upnp port changed from %i to %i", upnp->port, port );
    upnp->port = port;
    tr_lockUnlock( &upnp->lock );
}

void
tr_upnpClose( tr_upnp_t * upnp )
{
    tr_upnpStop( upnp );

    tr_lockLock( &upnp->lock );
    while( NULL != upnp->devices )
    {
        deviceRemove( &upnp->devices, upnp->fdlimit );
    }

    tr_lockClose( &upnp->lock );
    free( upnp );
}

void
tr_upnpPulse( tr_upnp_t * upnp )
{
    tr_upnp_device_t ** ii;

    tr_lockLock( &upnp->lock );

    if( upnp->active )
    {
        /* pulse on all known devices */
        upnp->discovering = 1;
        for( ii = &upnp->devices; NULL != *ii; ii = &(*ii)->next )
        {
            if( devicePulse( *ii, upnp->fdlimit, upnp->port ) )
            {
                upnp->discovering = 0;
            }
        }

        /* send an SSDP discover message */
        if( upnp->discovering &&
            upnp->lastdelay + upnp->lastdiscover < tr_date() )
        {
            upnp->outfd = sendSSDP( upnp->fdlimit, upnp->outfd );
            upnp->lastdiscover = tr_date();
            upnp->lastdelay = MIN( upnp->lastdelay * 2, SSDP_MAX_DELAY );
        }

        /* try to receive SSDP messages */
        watchSSDP( &upnp->devices, upnp->infd );
        if( watchSSDP( &upnp->devices, upnp->outfd ) )
        {
            killSock( upnp->fdlimit, &upnp->outfd );
        }
    }
    else
    {
        /* delete all mappings then delete devices */
        ii = &upnp->devices;
        while( NULL != *ii )
        {
            if( deviceStop( *ii ) )
            {
                deviceRemove( ii, upnp->fdlimit );
            }
            else
            {
                devicePulse( *ii, upnp->fdlimit, 0 );
                ii = &(*ii)->next;
            }
        }
    }

    tr_lockUnlock( &upnp->lock );
}

static int
sendSSDP( tr_fd_t * fdlimit, int fd )
{
    char buf[102];
    int  len;
    struct sockaddr_in sin;

    if( 0 > fd )
    {
        if( tr_fdSocketWillCreate( fdlimit, 0 ) )
        {
            return -1;
        }
        fd = tr_netBindUDP( 0 );
        if( 0 > fd )
        {
            tr_fdSocketClosed( fdlimit, 0 );
            return -1;
        }
    }

    tr_dbg( "sending upnp ssdp discover message" );

    len = snprintf( buf, sizeof( buf ),
                    "M-SEARCH * HTTP/1.1\r\n"
                    "Host: %s:%i\r\n"
                    "Man: \"ssdp:discover\"\r\n"
                    "ST: %s\r\n"
                    "MX: 3\r\n"
                    "\r\n",
                    SSDP_ADDR, SSDP_PORT, SSDP_TYPE );

    /* if this assertion ever fails then just increase the size of buf */
    assert( (int) sizeof( buf ) > len );

    memset( &sin, 0, sizeof( sin ) );
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = inet_addr( SSDP_ADDR );
    sin.sin_port        = htons( SSDP_PORT );

    if( 0 > sendto( fd, buf, len, 0,
                    (struct sockaddr*) &sin, sizeof( sin ) ) )
    {
        if( EAGAIN != errno )
        {
            tr_err( "Could not send SSDP discover message (%s)",
                    strerror( errno ) );
        }
        killSock( fdlimit, &fd );
        return -1;
    }

    return fd;
}

static int
mcastStart( tr_fd_t * fdlimit )
{
    int fd;
    struct in_addr addr;

    if( tr_fdSocketWillCreate( fdlimit, 0 ) )
    {
        return -1;
    }

    addr.s_addr = inet_addr( SSDP_ADDR );
    fd = tr_netMcastOpen( SSDP_PORT, addr );
    if( 0 > fd )
    {
        tr_fdSocketClosed( fdlimit, 0 );
        return -1;
    }

    return fd;
}

static void
killSock( tr_fd_t * fdlimit, int * sock )
{
    if( 0 <= *sock )
    {
        tr_netClose( *sock );
        *sock = -1;
        tr_fdSocketClosed( fdlimit, 0 );
    }
}

static void
killHttp( tr_fd_t * fdlimit, tr_http_t ** http )
{
    tr_httpClose( *http );
    *http = NULL;
    tr_fdSocketClosed( fdlimit, 0 );
}

static int
watchSSDP( tr_upnp_device_t ** devices, int fd )
{
    /* XXX what if we get a huge SSDP packet? */
    char buf[512];
    int len;
    tr_http_header_t hdr[] = {
        /* first one must be type and second must be subtype */
        { NULL,            NULL, 0 },
        { "NTS",           NULL, 0 },
        /* XXX should probably look at this
           { "Cache-control", NULL, 0 }, */
        { "Location",      NULL, 0 },
        { "USN",           NULL, 0 },
        { NULL,            NULL, 0 }
    };
    enum { OFF_TYPE = 0, OFF_SUBTYPE, OFF_LOC, OFF_ID };
    int ret;

    if( 0 > fd )
    {
        return 0;
    }

    ret = 0;
    for(;;)
    {
        len = sizeof( buf );
        switch( recvSSDP( fd, buf, &len ) )
        {
            case TR_WAIT:
                return ret;
            case TR_ERROR:
                return 1;
            case TR_OK:
                ret = 1;
                if( parseSSDP( buf, len, hdr ) &&
                    NULL != hdr[OFF_LOC].data &&
                    NULL != hdr[OFF_ID].data )
                {
                    deviceAdd( devices, hdr[OFF_ID].data, hdr[OFF_ID].len,
                               hdr[OFF_LOC].data, hdr[OFF_LOC].len );
                }
        }
    }
}

static tr_tristate_t
recvSSDP( int fd, char * buf, int * len )
{
    if( 0 > fd )
    {
        return TR_ERROR;
    }

    *len = tr_netRecv( fd, ( uint8_t * ) buf, *len );
    if( TR_NET_BLOCK & *len )
    {
        return TR_WAIT;
    }
    else if( TR_NET_CLOSE & *len )
    {
        tr_err( "Could not receive SSDP message (%s)", strerror( errno ) );
        return TR_ERROR;
    }
    else
    {
        return TR_OK;
    }
}

static int
parseSSDP( char * buf, int len, tr_http_header_t * hdr )
{
    char *method, *uri, *body;
    int code;

    body = NULL;
    /* check for an HTTP NOTIFY request */
    if( 0 <= tr_httpRequestType( buf, len, &method, &uri ) )
    {
        if( 0 == tr_strcasecmp( method, "NOTIFY" ) && 0 == strcmp( uri, "*" ) )
        {
            hdr[0].name = "NT";
            body = tr_httpParse( buf, len, hdr );
            if( NULL == hdr[1].name ||
                0 != tr_strncasecmp( SSDP_SUBTYPE, hdr[1].data, hdr[1].len ) )
            {
                body = NULL;
            }
            else
            {
                tr_dbg( "found upnp ssdp notify request" );
            }
        }
        free( method );
        free( uri );
    }
    else
    {
        /* check for a response to our HTTP M-SEARCH request */
        code = tr_httpResponseCode( buf, len );
        if( TR_HTTP_STATUS_OK( code ) )
        {
            hdr[0].name = "ST";
            body = tr_httpParse( buf, len, hdr );
            if( NULL != body )
            {
                tr_dbg( "found upnp ssdp m-search response" );
            }
        }
    }

    /* did we find enough information to be useful? */
    if( NULL != body )
    {
        /* the first header is the type */
        if( NULL != hdr[0].data &&
            0 == tr_strncasecmp( SSDP_TYPE, hdr[0].data, hdr[0].len ) )
        {
            return 1;
        }
    }

    return 0;
}

static void
deviceAdd( tr_upnp_device_t ** first, const char * id, int idLen,
           const char * url, int urlLen )
{
    tr_upnp_device_t * ii;

    for( ii = *first; NULL != ii; ii = ii->next )
    {
        if( 0 == tr_strncasecmp( ii->id, id, idLen ) )
        {
            /* this device may have gone away and came back, recheck it */
            ii->lastcheck = 0;
            return;
        }
    }

    ii = malloc( sizeof( *ii ) );
    if( NULL == ii )
    {
        return;
    }
    memset( ii, 0, sizeof( *ii ) );
    if( tr_httpParseUrl( url, urlLen, &ii->host, &ii->port, &ii->root ) )
    {
        free( ii );
        return;
    }
    ii->id = tr_dupstr( id, idLen );
    ii->state = UPNPDEV_STATE_ROOT;
    actionSetup( &ii->getcmd, "GetSpecificPortMappingEntry", 8 );
    actionSetup( &ii->addcmd, "AddPortMapping", 8 );
    actionSetup( &ii->delcmd, "DeletePortMapping", 3 );
    ii->next = *first;
    *first = ii;

    tr_inf( "new upnp device %s, port %i, path %s",
            ii->host, ii->port, ii->root );
}

static void
deviceRemove( tr_upnp_device_t ** prevptr, tr_fd_t * fdlimit )
{
    tr_upnp_device_t * dead;

    dead = *prevptr;
    *prevptr = dead->next;

    tr_inf( "forgetting upnp device %s", dead->host );

    free( dead->id );
    free( dead->host );
    free( dead->root );
    free( dead->soap );
    free( dead->scpd );
    free( dead->myaddr );
    if( NULL != dead->http )
    {
        killHttp( fdlimit, &dead->http );
    }
    actionFree( &dead->getcmd );
    actionFree( &dead->addcmd );
    actionFree( &dead->delcmd );
    free( dead );
}

static int
deviceStop( tr_upnp_device_t * dev )
{
    switch( dev->state )
    {
        case UPNPDEV_STATE_READY:
        case UPNPDEV_STATE_ERROR:
            return 1;
        case UPNPDEV_STATE_MAPPED:
            tr_dbg( "upnp device %s: stopping upnp, state mapped -> delete",
                dev->host );
            dev->state = UPNPDEV_STATE_DEL;
            return 0;
        default:
            return 0;
    }
}

static int
devicePulse( tr_upnp_device_t * dev, tr_fd_t * fdlimit, int port )
{
    const char * body;
    int          len, code;
    uint8_t      laststate;

    switch( dev->state )
    {
        case UPNPDEV_STATE_READY:
            if( 0 < port )
            {
                tr_dbg( "upnp device %s: want mapping, state ready -> get",
                        dev->host );
                dev->mappedport = port;
                dev->state = UPNPDEV_STATE_GET;
                break;
            }
            return 1;
        case UPNPDEV_STATE_MAPPED:
            if( port != dev->mappedport )
            {
                tr_dbg( "upnp device %s: change mapping, "
                        "state mapped -> delete", dev->host );
                dev->state = UPNPDEV_STATE_DEL;
                break;
            }
            if( tr_date() > dev->lastcheck + MAPPING_CHECK_INTERVAL )
            {
                tr_dbg( "upnp device %s: check mapping, "
                        "state mapped -> get", dev->host );
                dev->state = UPNPDEV_STATE_GET;
            }
            return 1;
        case UPNPDEV_STATE_ERROR:
            return 0;
    }

    code = devicePulseHttp( dev, fdlimit, &body, &len );
    if( 0 > code )
    {
        return 1;
    }

    if( LOOP_DETECT_THRESHOLD <= dev->looping )
    {
        tr_dbg( "upnp device %s: loop detected, state %hhu -> error",
                dev->host, dev->state );
        dev->state = UPNPDEV_STATE_ERROR;
        dev->looping = 0;
        killHttp( fdlimit, &dev->http );
        return 1;
    }

    laststate = dev->state;
    dev->state = UPNPDEV_STATE_ERROR;
    switch( laststate ) 
    {
        case UPNPDEV_STATE_ROOT:
            if( !TR_HTTP_STATUS_OK( code ) )
            {
                tr_dbg( "upnp device %s: fetch root failed with http code %i",
                        dev->host, code );
            }
            else if( parseRoot( body, len, &dev->soap, &dev->scpd ) )
            {
                tr_dbg( "upnp device %s: parse root failed", dev->host );
            }
            else
            {
                tr_dbg( "upnp device %s: parsed root, state root -> scpd",
                        dev->host );
                dev->state = UPNPDEV_STATE_SCPD;
            }
            break;

        case UPNPDEV_STATE_SCPD:
            if( !TR_HTTP_STATUS_OK( code ) )
            {
                tr_dbg( "upnp device %s: fetch scpd failed with http code %i",
                        dev->host, code );
            }
            else if( parseScpd( body, len, &dev->getcmd,
                                &dev->addcmd, &dev->delcmd ) )
            {
                tr_dbg( "upnp device %s: parse scpd failed", dev->host );
            }
            else
            {
                tr_dbg( "upnp device %s: parsed scpd, state scpd -> ready",
                        dev->host );
                dev->state = UPNPDEV_STATE_READY;
                dev->looping = 0;
            }
            break;

        case UPNPDEV_STATE_ADD:
            dev->looping++;
            if( IGD_ADD_CONFLICT == code )
            {
                tr_dbg( "upnp device %s: add conflict, state add -> delete",
                        dev->host );
                dev->state = UPNPDEV_STATE_DEL;
            }
            else if( TR_HTTP_STATUS_OK( code ) ||
                     IGD_GENERIC_ERROR == code || IGD_GENERIC_FAILED == code )
            {
                tr_dbg( "upnp device %s: add attempt, state add -> get",
                        dev->host );
                dev->state = UPNPDEV_STATE_GET;
            }
            else
            {
                tr_dbg( "upnp device %s: add failed with http code %i",
                        dev->host, code );
            }
            break;

        case UPNPDEV_STATE_GET:
            dev->looping++;
            if( TR_HTTP_STATUS_OK( code ) )
            {
                switch( parseMapping( dev, body, len ) )
                {
                    case -1:
                        break;
                    case 0:
                        tr_dbg( "upnp device %s: invalid mapping, "
                                "state get -> delete", dev->host );
                        dev->state = UPNPDEV_STATE_DEL;
                        break;
                    case 1:
                        tr_dbg( "upnp device %s: good mapping, "
                                "state get -> mapped", dev->host );
                        dev->state = UPNPDEV_STATE_MAPPED;
                        dev->looping = 0;
                        dev->lastcheck = tr_date();
                        tr_inf( "upnp successful for port %i",
                                dev->mappedport );
                        break;
                    default:
                        assert( 0 );
                        break;
                }
            }
            else if( IGD_NO_MAPPING_EXISTS == code ||
                     IGD_GENERIC_ERROR == code || IGD_GENERIC_FAILED == code )
            {
                tr_dbg( "upnp device %s: no mapping, state get -> add",
                        dev->host );
                dev->state = UPNPDEV_STATE_ADD;
            }
            else
            {
                tr_dbg( "upnp device %s: get failed with http code %i",
                        dev->host, code );
            }
            break;

        case UPNPDEV_STATE_DEL:
            dev->looping++;
            if( TR_HTTP_STATUS_OK( code ) || IGD_NO_MAPPING_EXISTS == code ||
                IGD_GENERIC_ERROR == code || IGD_GENERIC_FAILED == code )
            {
                tr_dbg( "upnp device %s: deleted, state delete -> ready",
                        dev->host );
                dev->state = UPNPDEV_STATE_READY;
                dev->looping = 0;
            }
            else
            {
                tr_dbg( "upnp device %s: del failed with http code %i",
                        dev->host, code );
            }
            break;

        default:
            assert( 0 );
            break;
    }

    dev->lastrequest = tr_date();
    killHttp( fdlimit, &dev->http );

    if( UPNPDEV_STATE_ERROR == dev->state )
    {
        tr_dbg( "upnp device %s: error, state %hhu -> error",
                dev->host, laststate );
        return 0;
    }

    return 1;
}

static tr_http_t *
devicePulseGetHttp( tr_upnp_device_t * dev, tr_fd_t * fdlimit )
{
    tr_http_t  * ret;
    char numstr[6];

    if( tr_fdSocketWillCreate( fdlimit, 0 ) )
    {
        return NULL;
    }

    switch( dev->state ) 
    {
        case UPNPDEV_STATE_ROOT:
            if( !dev->soapretry )
            {
                ret = tr_httpClient( TR_HTTP_GET, dev->host,
                                     dev->port, "%s", dev->root );
            }
            break;
        case UPNPDEV_STATE_SCPD:
            if( !dev->soapretry )
            {
                ret = tr_httpClient( TR_HTTP_GET, dev->host,
                                     dev->port, "%s", dev->scpd );
            }
            break;
        case UPNPDEV_STATE_ADD:
            if( NULL == dev->myaddr )
            {
                ret = NULL;
                break;
            }
            snprintf( numstr, sizeof( numstr ), "%i", dev->mappedport );
            ret = soapRequest( dev->soapretry, dev->host, dev->port, dev->soap,
                               &dev->addcmd,
                               "PortMappingEnabled", "1",
                               "PortMappingLeaseDuration", "0",
                               "RemoteHost", "",
                               "ExternalPort", numstr,
                               "InternalPort", numstr,
                               "PortMappingProtocol", "TCP",
                               "InternalClient", dev->myaddr,
                               "PortMappingDescription",
                                   "Added by Transmission",
                               NULL );
            break;
        case UPNPDEV_STATE_GET:
            snprintf( numstr, sizeof( numstr ), "%i", dev->mappedport );
            ret = soapRequest( dev->soapretry, dev->host, dev->port, dev->soap,
                               &dev->getcmd,
                               "RemoteHost", "",
                               "ExternalPort", numstr,
                               "PortMappingProtocol", "TCP",
                               NULL );
            break;
        case UPNPDEV_STATE_DEL:
            snprintf( numstr, sizeof( numstr ), "%i", dev->mappedport );
            ret = soapRequest( dev->soapretry, dev->host, dev->port, dev->soap,
                               &dev->delcmd,
                               "RemoteHost", "",
                               "ExternalPort", numstr,
                               "PortMappingProtocol", "TCP",
                               NULL );
            break;
        default:
            assert( 0 );
            break;
    }

    if( NULL == ret )
    {
        tr_fdSocketClosed( fdlimit, 0 );
    }

    return ret;
}

static int
devicePulseHttp( tr_upnp_device_t * dev, tr_fd_t * fdlimit,
                 const char ** body, int * len )
{
    const char * headers;
    int          hlen, code;

    if( NULL == dev->http )
    {
        if( tr_date() < dev->lastrequest + HTTP_REQUEST_INTERVAL )
        {
            return -1;
        }
        dev->lastrequest = tr_date();
        dev->http = devicePulseGetHttp( dev, fdlimit );
        if( NULL == dev->http )
        {
            tr_dbg( "upnp device %s: http init failed, state %hhu -> error",
                    dev->host, dev->state );
            dev->state = UPNPDEV_STATE_ERROR;
            dev->soapretry = 0;
            return -1;
        }
    }

    if( NULL == dev->myaddr )
    {
        dev->myaddr = tr_httpWhatsMyAddress( dev->http );
    }

    switch( tr_httpPulse( dev->http, &headers, &hlen ) )
    {
        case TR_OK:
            code = tr_httpResponseCode( headers, hlen );
            if( TR_HTTP_STATUS_FAIL( code ) && !dev->soapretry )
            {
                dev->soapretry = 1;
                killHttp( fdlimit, &dev->http );
                break;
            }
            dev->soapretry = 0;
            *body = tr_httpParse( headers, hlen, NULL );
            *len = ( NULL == body ? 0 : hlen - ( *body - headers ) );
            return code;
        case TR_ERROR:
            killHttp( fdlimit, &dev->http );
            if( dev->soapretry )
            {
                tr_dbg( "upnp device %s: http pulse failed, state %hhu -> error",
                        dev->host, dev->state );
                dev->state = UPNPDEV_STATE_ERROR;
                dev->soapretry = 0;
            }
            else
            {
                dev->soapretry = 1;
            }
            break;
        case TR_WAIT:
            break;
    }

    return -1;
}

static int
parseRoot(const char *buf, int len, char ** soap, char ** scpd )
{
    const char * end, * ii, * jj, * kk, * urlbase;
    char       * joined;

    *soap = NULL;
    *scpd = NULL;
    end = buf + len;

    buf = tr_xmlFindTagContents( buf, end, "root" );
    urlbase = tr_xmlFindTag( buf, end, "urlBase" );
    buf = tr_xmlFindTagContents( buf, end, "device" );
    if( tr_xmlFindTagVerifyContents( buf, end, "deviceType",
            "urn:schemas-upnp-org:device:InternetGatewayDevice:1", 1 ) )
    {
        return 1;
    }
    buf = tr_xmlFindTag( buf, end, "deviceList" );
    ii = tr_xmlTagContents( buf, end );
    for( ; NULL != ii; ii = tr_xmlSkipTag( ii, end ) )
    {
        ii = tr_xmlFindTag( ii, end, "device" );
        buf = tr_xmlTagContents( ii, end );
        if( tr_xmlFindTagVerifyContents( buf, end, "deviceType",
                "urn:schemas-upnp-org:device:WANDevice:1", 1 ) )
        {
            continue;
        }
        buf = tr_xmlFindTag( buf, end, "deviceList" );
        jj = tr_xmlTagContents( buf, end );
        for( ; NULL != jj; jj = tr_xmlSkipTag( jj, end ) )
        {
            jj = tr_xmlFindTag( jj, end, "device" );
            buf = tr_xmlTagContents( jj, end );
            if( tr_xmlFindTagVerifyContents( buf, end, "deviceType",
                    "urn:schemas-upnp-org:device:WANConnectionDevice:1", 1 ) )
            {
                continue;
            }
            buf = tr_xmlFindTag( buf, end, "serviceList" );
            kk = tr_xmlTagContents( buf, end );
            for( ; NULL != kk; kk = tr_xmlSkipTag( kk, end ) )
            {
                kk = tr_xmlFindTag( kk, end, "service" );
                buf = tr_xmlTagContents( kk, end );
                if( !tr_xmlFindTagVerifyContents( buf, end, "serviceType",
                                                  UPNP_SERVICE_TYPE, 1 ) )
                {
                    *soap = tr_xmlDupTagContents( buf, end, "controlURL");
                    *scpd = tr_xmlDupTagContents( buf, end, "SCPDURL");
                    break;
                }
            }
        }
    }

    if( NULL != *soap && NULL != *scpd )
    {
        if( '/' != **soap || '/' != **scpd )
        {
            urlbase = tr_xmlDupContents( urlbase, end );
            if( NULL != urlbase )
            {
                if( '/' != **soap )
                {
                    joined = joinstrs( urlbase, "/", *soap );
                    free( *soap );
                    *soap = joined;
                }
                if( '/' != **scpd )
                {
                    joined = joinstrs( urlbase, "/", *scpd );
                    free( *scpd );
                    *scpd = joined;
                }
                free( (char*)urlbase );
            }
        }
        return 0;
    }

    return 1;
}

static int
parseScpd( const char * buf, int len, tr_upnp_action_t * getcmd,
           tr_upnp_action_t * addcmd, tr_upnp_action_t * delcmd )
{
    const char * end, * next, * sub, * name;

    end = buf + len;
    next = buf;

    next = tr_xmlFindTagContents( next, end, "scpd" );
    next = tr_xmlFindTagContents( next, end, "actionList" );

    while( NULL != next )
    {
        next = tr_xmlFindTag( next, end, "action" );
        sub = tr_xmlTagContents( next, end );
        name = tr_xmlFindTagContents( sub, end, "name" );
        sub = tr_xmlFindTagContents( sub, end, "argumentList" );
        if( !tr_xmlVerifyContents( name, end, getcmd->name, 1 ) )
        {
            if( parseScpdArgs( sub, end, getcmd, 'i' ) ||
                parseScpdArgs( sub, end, getcmd, 'o' ) )
            {
                return 1;
            }
        }
        else if( !tr_xmlVerifyContents( name, end, addcmd->name, 1 ) )
        {
            if( parseScpdArgs( sub, end, addcmd, 'i' ) )
            {
                return 1;
            }
        }
        else if( !tr_xmlVerifyContents( name, end, delcmd->name, 1 ) )
        {
            if( parseScpdArgs( sub, end, delcmd, 'i' ) )
            {
                return 1;
            }
        }
        next = tr_xmlSkipTag( next, end );
    }

    return 0;
}

static int
parseScpdArgs( const char * buf, const char * end,
               tr_upnp_action_t * action, char dir )
{
    const char * sub, * which;
    char       * name, * var;

    assert( 'i' == dir || 'o' == dir );
    which = ( 'i' == dir ? "in" : "out" );

    while( NULL != buf )
    {
        sub = tr_xmlFindTagContents( buf, end, "argument" );
        if( !tr_xmlFindTagVerifyContents( sub, end, "direction", which, 1 ) )
        {
            name = tr_xmlDupTagContents( sub, end, "name" );
            var = tr_xmlDupTagContents( sub, end, "relatedStateVariable" );
            if( NULL == name || NULL == var )
            {
                free( name );
                free( var );
            }
            else if( actionAdd( action, name, var, dir ) )
            {
                return 1;
            }
        }
        buf = tr_xmlSkipTag( buf, end );
    }

    return 0;
}

static int
parseMapping( tr_upnp_device_t * dev, const char * buf, int len )
{
    const char * end, * down, * next, * var;
    int          varlen, pret, cret, eret;
    char       * val;

    assert( 0 < dev->mappedport );

    if( NULL == dev->myaddr )
    {
        return -1;
    }

    pret = -1;
    cret = -1;
    eret = -1;

    end = buf + len;
    down = buf;
    down = tr_xmlFindTagContents( down, end, "Envelope" );
    down = tr_xmlFindTagContents( down, end, "Body" );
    down = tr_xmlFindTagContents( down, end,
                                  "GetSpecificPortMappingEntryResponse" );

    next = down;
    while( NULL != next )
    {
        var = tr_xmlTagName( next, end, &varlen );
        var = actionLookupVar( &dev->getcmd, var, varlen, 'o' );
        if( NULL != var )
        {
            val = tr_xmlDupContents( tr_xmlTagContents( next, end ), end );
            if( 0 == tr_strcasecmp( "InternalPort", var ) )
            {
                pret = ( strtol( val, NULL, 10 ) == dev->mappedport ? 1 : 0 );
            }
            else if( 0 == tr_strcasecmp( "InternalClient", var ) )
            {
                cret = ( 0 == strcmp( dev->myaddr, val ) ? 1 : 0 );
            }
            else if( 0 == tr_strcasecmp( "PortMappingEnabled", var ) )
            {
                eret = ( 0 == strcmp( "1", val ) ? 1 : 0 );
            }
            free( val );
        }
        next = tr_xmlSkipTag( next, end );
    }

    return MIN( MIN( pret, cret), eret );
}

static char *
joinstrs( const char * first, const char * delim, const char * second )
{
    char * ret;
    int    len1, len2, len3;

    len1 = strlen( first );
    len2 = strlen( delim );
    len3 = strlen( second );
    ret = malloc( len1 + len2 + len3 + 1 );
    if( NULL == ret )
    {
        return NULL;
    }

    memcpy( ret, first, len1 );
    memcpy( ret + len1, delim, len2 );
    memcpy( ret + len1 + len2, second, len3 );
    ret[len1 + len2 + len3] = '\0';

    return ret;
}

static tr_http_t *
soapRequest( int retry, const char * host, int port, const char * path,
             tr_upnp_action_t * action, ... )
{
    tr_http_t  * http;
    va_list      ap;
    const char * name, * value;
    int          method;

    method = ( retry ? TR_HTTP_M_POST : TR_HTTP_POST );
    http = tr_httpClient( method, host, port, "%s", path );
    if( NULL != http )
    {
        tr_httpAddHeader( http, "Content-type", "text/xml" );
        tr_httpAddHeader( http, "SOAPAction", action->action );
        if( retry )
        {
            tr_httpAddHeader( http, "Man", "\"" SOAP_ENVELOPE "\"" );
        }
        tr_httpAddBody( http, 
"<s:Envelope"
"    xmlns:s='" SOAP_ENVELOPE "'"
"    s:encodingStyle='http://schemas.xmlsoap.org/soap/encoding/'>"
"  <s:Body>"
"    <u:%s xmlns:u='" UPNP_SERVICE_TYPE "'>", action->name );

        va_start( ap, action );
        do
        {
            name = va_arg( ap, const char * );
            name = actionLookupName( action, name, -1, 'i' );
            if( NULL != name )
            {
                value = va_arg( ap, const char * );
                if( NULL != value )
                {
                    tr_httpAddBody( http,
"      <u:%s>%s</u:%s>", name, value, name );
                }
                else 
                {
                    tr_httpAddBody( http,
"      <u:%s></u:%s>", name, name );
                }
            }
        }
        while( NULL != name && NULL != value );
        va_end( ap );

        tr_httpAddBody( http,
"    </u:%s>"
"  </s:Body>"
"</s:Envelope>", action->name );
    }

    return http;
}

static void
actionSetup( tr_upnp_action_t * action, const char * name, int prealloc )
{
    action->name = strdup( name );
    action->action = joinstrs( UPNP_SERVICE_TYPE, "#", name );
    assert( NULL == action->args );
    action->args = malloc( sizeof( *action->args ) * prealloc );
    memset( action->args, 0, sizeof( *action->args ) * prealloc );
    action->len = prealloc;
}

static void
actionFree( tr_upnp_action_t * act )
{
    free( act->name );
    free( act->action );
    while( 0 < act->len )
    {
        act->len--;
        free( act->args[act->len].name );
        free( act->args[act->len].var );
    }
    free( act->args );
}

static int
actionAdd( tr_upnp_action_t * act, char * name, char * var, char dir )
{
    int    ii;
    void * newbuf;

    assert( 'i' == dir || 'o' == dir );

    ii = 0;
    while( ii < act->len && NULL != act->args[ii].name )
    {
        ii++;
    }

    if( ii == act->len )
    {
        newbuf = realloc( act->args, sizeof( *act->args ) * ( act->len + 1 ) );
        if( NULL == newbuf )
        {
            return 1;
        }
        act->args = newbuf;
        act->len++;
    }

    act->args[ii].name = name;
    act->args[ii].var = var;
    act->args[ii].dir = dir;

    return 0;
}

static const char *
actionLookup( tr_upnp_action_t * act, const char * key, int len,
              char dir, int getname )
{
    int ii;

    assert( 'i' == dir || 'o' == dir );

    if( NULL == key || 0 == len )
    {
        return NULL;
    }
    if( 0 > len )
    {
        len = strlen( key );
    }

    for( ii = 0; ii < act->len; ii++ )
    {
        if( NULL != act->args[ii].name && dir == act->args[ii].dir )
        {
            if( !getname &&
                0 == tr_strncasecmp( act->args[ii].name, key, len ) )
            {
                return act->args[ii].var;
            }
            else if( getname &&
                     0 == tr_strncasecmp( act->args[ii].var, key, len ) )
            {
                return act->args[ii].name;
            }
        }
    }

    return NULL;
}
