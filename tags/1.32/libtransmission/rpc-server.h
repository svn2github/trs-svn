/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_RPC_SERVER_H
#define TR_RPC_SERVER_H

typedef struct tr_rpc_server tr_rpc_server;

tr_rpc_server * tr_rpcInit       ( struct tr_handle     * session,
                                   int                    isEnabled,
                                   int                    port,
                                   const char           * acl,
                                   int                    isPasswordEnabled,
                                   const char           * username,
                                   const char           * password );

void    tr_rpcClose              ( tr_rpc_server       ** freeme );

void    tr_rpcSetEnabled         ( tr_rpc_server        * server,
                                   int                    isEnabled );

int     tr_rpcIsEnabled          ( const tr_rpc_server  * server );

void    tr_rpcSetPort            ( tr_rpc_server        * server,
                                   int                    port );

int     tr_rpcGetPort            ( const tr_rpc_server  * server );

int     tr_rpcSetTest            ( const tr_rpc_server  * server,
                                   const char           * acl,
                                   char                ** allocme_errmsg );

int     tr_rpcTestACL            ( const tr_rpc_server  * server,
                                   const char           * acl,
                                   char                ** allocme_errmsg );

int     tr_rpcSetACL             ( tr_rpc_server        * server,
                                   const char           * acl,
                                   char                ** allocme_errmsg );

char*   tr_rpcGetACL             ( const tr_rpc_server  * server );

void    tr_rpcSetPassword        ( tr_rpc_server        * server,
                                   const char           * password );

char*   tr_rpcGetPassword        ( const tr_rpc_server  * server );

void    tr_rpcSetUsername        ( tr_rpc_server        * server,
                                   const char           * username );

char*   tr_rpcGetUsername        ( const tr_rpc_server  * server );

void    tr_rpcSetPasswordEnabled ( tr_rpc_server        * server,
                                   int                    isEnabled );

int     tr_rpcIsPasswordEnabled  ( const tr_rpc_server  * session );

#endif
