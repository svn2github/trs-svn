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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef SHARED_H
#define SHARED_H 1

#include "transmission.h"
#include "net.h"

/** 
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

struct tr_bindsockets;

typedef struct tr_shared tr_shared;

tr_shared* tr_sharedInit( tr_session*, tr_bool isEnabled );

void       tr_sharedClose( tr_session * );

void       tr_sharedPortChanged( tr_session * );

void       tr_sharedTraversalEnable( tr_shared *, tr_bool isEnabled );

tr_port    tr_sharedGetPeerPort( const tr_shared * s );

tr_bool    tr_sharedTraversalIsEnabled( const tr_shared * s );

int        tr_sharedTraversalStatus( const tr_shared * );

/** @} */
#endif
