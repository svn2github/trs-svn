/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_UPNP_H
#define TR_UPNP_H 1

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

typedef struct tr_upnp tr_upnp;

tr_upnp * tr_upnpInit( void );

void      tr_upnpClose( tr_upnp * );

int       tr_upnpPulse(         tr_upnp *,
                            int port,
                            int isEnabled,
                            int doPortCheck );
/* @} */
#endif
