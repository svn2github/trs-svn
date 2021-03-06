/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include "transmission.h"
#include "list.h"
#include "utils.h"

static tr_list_t*
node_alloc( void )
{
    return tr_new0( tr_list_t, 1 );
}

static void
node_free( tr_list_t* node )
{
    tr_free( node );
}

/***
****
***/

void
tr_list_free( tr_list_t* list )
{
    while( list )
    {
        tr_list_t * node = list;
        list = list->next;
        node_free( node );
    }
}

tr_list_t*
tr_list_prepend( tr_list_t * list, void * data )
{
    tr_list_t * node = node_alloc ();
    node->data = data;
    node->next = list;
    if( list )
        list->prev = node;
    return node;
}

tr_list_t*
tr_list_append( tr_list_t * list, void * data )
{
    tr_list_t * node = node_alloc( );
    node->data = data;
    if( !list )
        return node;
    else {
        tr_list_t * l = list;
        while( l->next )
            l = l->next;
        l->next = node;
        node->prev = l;
        return list;
    }
}

tr_list_t*
tr_list_find_data ( tr_list_t * list, const void * data )
{
    for(; list; list=list->next )
        if( list->data == data )
            return list;

    return NULL;
}

tr_list_t*
tr_list_remove_data ( tr_list_t * list, const void * data )
{
    tr_list_t * node = tr_list_find_data( list, data );
    tr_list_t * prev = node ? node->prev : NULL;
    tr_list_t * next = node ? node->next : NULL;
    if( prev ) prev->next = next;
    if( next ) next->prev = prev;
    if( list == node ) list = next;
    node_free( node );
    return list;
}

tr_list_t*
tr_list_find ( tr_list_t * list , TrListCompareFunc func, const void * b )
{
    for( ; list; list=list->next )
        if( !func( list->data, b ) )
            return list;

    return NULL;
}

void
tr_list_foreach( tr_list_t * list, TrListForeachFunc func )
{
    while( list )
    {
        func( list->data );
        list = list->next;
    }
}
