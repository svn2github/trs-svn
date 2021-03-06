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

#include <assert.h>
#include <ctype.h> /* isdigit, isprint */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <event.h> /* evbuffer */

#include "transmission.h"
#include "bencode.h"
#include "list.h"
#include "ptrarray.h"
#include "utils.h" /* tr_new(), tr_free() */

/**
***
**/

int
tr_bencIsType( const tr_benc * val, int type )
{
    return ( ( val != NULL ) && ( val->type == type ) );
}

static int
isContainer( const tr_benc * val )
{
    return tr_bencIsList(val) || tr_bencIsDict(val);
}
static int
isSomething( const tr_benc * val )
{
    return isContainer(val) || tr_bencIsInt(val) || tr_bencIsString(val);
}

/***
****  tr_bencParse()
****  tr_bencLoad()
***/

/**
 * The initial i and trailing e are beginning and ending delimiters.
 * You can have negative numbers such as i-3e. You cannot prefix the
 * number with a zero such as i04e. However, i0e is valid.  
 * Example: i3e represents the integer "3" 
 * NOTE: The maximum number of bit of this integer is unspecified,
 * but to handle it as a signed 64bit integer is mandatory to handle
 * "large files" aka .torrent for more that 4Gbyte 
 */
int
tr_bencParseInt( const uint8_t  * buf,
                 const uint8_t  * bufend,
                 const uint8_t ** setme_end, 
                 int64_t        * setme_val )
{
    int err = TR_OK;
    char * endptr;
    const void * begin;
    const void * end;
    int64_t val;

    if( buf >= bufend )
        return TR_ERROR;
    if( *buf != 'i' )
        return TR_ERROR;

    begin = buf + 1;
    end = memchr( begin, 'e', (bufend-buf)-1 );
    if( end == NULL )
        return TR_ERROR;

    errno = 0;
    val = strtoll( begin, &endptr, 10 );
    if( errno || ( endptr != end ) ) /* incomplete parse */
        err = TR_ERROR;
    else if( val && *(const char*)begin=='0' ) /* no leading zeroes! */
        err = TR_ERROR;
    else {
        *setme_end = end + 1;
        *setme_val = val;
    }

    return err;
}


/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam" 
 */
int
tr_bencParseStr( const uint8_t  * buf,
                 const uint8_t  * bufend,
                 const uint8_t ** setme_end,
                 uint8_t       ** setme_str,
                 size_t         * setme_strlen )
{
    size_t len;
    const void * end;
    char * endptr;

    if( buf >= bufend )
        return TR_ERROR;

    if( !isdigit( *buf  ) )
        return TR_ERROR;

    end = memchr( buf, ':', bufend-buf );
    if( end == NULL )
        return TR_ERROR;

    errno = 0;
    len = strtoul( (const char*)buf, &endptr, 10 );
    if( errno || endptr!=end )
        return TR_ERROR;

    if( (const uint8_t*)end + 1 + len > bufend )
        return TR_ERROR;

    *setme_end = end + 1 + len;
    *setme_str = (uint8_t*) tr_strndup( end + 1, len );
    *setme_strlen = len;
    return TR_OK;
}

/* setting to 1 to help expose bugs with tr_bencListAdd and tr_bencDictAdd */
#define LIST_SIZE 8 /* number of items to increment list/dict buffer by */

static int
makeroom( tr_benc * val, int count )
{
    assert( TYPE_LIST == val->type || TYPE_DICT == val->type );

    if( val->val.l.count + count > val->val.l.alloc )
    {
        /* We need a bigger boat */
        const int len = val->val.l.alloc + count +
            ( count % LIST_SIZE ? LIST_SIZE - ( count % LIST_SIZE ) : 0 );
        void * new = realloc( val->val.l.vals, len * sizeof( tr_benc ) );
        if( NULL == new )
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = new;
    }

    return 0;
}

static tr_benc*
getNode( tr_benc * top, tr_ptrArray * parentStack, int type )
{
    tr_benc * parent;

    assert( top != NULL );
    assert( parentStack != NULL );

    if( tr_ptrArrayEmpty( parentStack ) )
        return top;

    parent = tr_ptrArrayBack( parentStack );
    assert( parent != NULL );

    /* dictionary keys must be strings */
    if( ( parent->type == TYPE_DICT )
        && ( type != TYPE_STR )
        && ( ! ( parent->val.l.count % 2 ) ) )
        return NULL;

    makeroom( parent, 1 );
    return parent->val.l.vals + parent->val.l.count++;
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
int
tr_bencParse( const void     * buf_in,
              const void     * bufend_in,
              tr_benc        * top,
              const uint8_t ** setme_end )
{
    int err;
    const uint8_t * buf = buf_in;
    const uint8_t * bufend = bufend_in;
    tr_ptrArray * parentStack = tr_ptrArrayNew( );

    tr_bencInit( top, 0 );

    while( buf != bufend )
    {
        if( buf > bufend ) /* no more text to parse... */
            return 1;

        if( *buf=='i' ) /* int */
        {
            int64_t val;
            const uint8_t * end;
            int err;
            tr_benc * node;

            if(( err = tr_bencParseInt( buf, bufend, &end, &val )))
                return err;

            node = getNode( top, parentStack, TYPE_INT );
            if( !node )
                return TR_ERROR;

            tr_bencInitInt( node, val );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( *buf=='l' ) /* list */
        {
            tr_benc * node = getNode( top, parentStack, TYPE_LIST );
            if( !node )
                return TR_ERROR;
            tr_bencInit( node, TYPE_LIST );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf=='d' ) /* dict */
        {
            tr_benc * node = getNode( top, parentStack, TYPE_DICT );
            if( !node )
                return TR_ERROR;
            tr_bencInit( node, TYPE_DICT );
            tr_ptrArrayAppend( parentStack, node );
            ++buf;
        }
        else if( *buf=='e' ) /* end of list or dict */
        {
            tr_benc * node;
            ++buf;
            if( tr_ptrArrayEmpty( parentStack ) )
                return TR_ERROR;

            node = tr_ptrArrayBack( parentStack );
            if( tr_bencIsDict( node ) && ( node->val.l.count % 2 ) )
                return TR_ERROR; /* odd # of children in dict */

            tr_ptrArrayPop( parentStack );
            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else if( isdigit(*buf) ) /* string? */
        {
            const uint8_t * end;
            uint8_t * str;
            size_t str_len;
            int err;
            tr_benc * node;

            if(( err = tr_bencParseStr( buf, bufend, &end, &str, &str_len )))
                return err;

            node = getNode( top, parentStack, TYPE_STR );
            if( !node )
                return TR_ERROR;

            tr_bencInitStr( node, str, str_len, 0 );
            buf = end;

            if( tr_ptrArrayEmpty( parentStack ) )
                break;
        }
        else /* invalid bencoded text... march past it */
        {
            ++buf;
        }
    }

    err = !isSomething( top ) || !tr_ptrArrayEmpty( parentStack );

    if( !err && ( setme_end != NULL ) )
        *setme_end = buf;

    tr_ptrArrayFree( parentStack, NULL );
    return err;
}

int
tr_bencLoad( const void  * buf_in,
             int           buflen,
             tr_benc     * setme_benc,
             char       ** setme_end )
{
    const uint8_t * buf = buf_in;
    const uint8_t * end;
    const int ret = tr_bencParse( buf, buf+buflen, setme_benc, &end );
    if( !ret && setme_end )
        *setme_end = (char*) end;
    return ret;
}

/***
****
***/

tr_benc *
tr_bencDictFind( tr_benc * val, const char * key )
{
    int len, ii;

    if( !tr_bencIsDict( val ) )
        return NULL;

    len = strlen( key );
    
    for( ii = 0; ii + 1 < val->val.l.count; ii += 2 )
    {
        if( TYPE_STR  != val->val.l.vals[ii].type ||
            len       != val->val.l.vals[ii].val.s.i ||
            0 != memcmp( val->val.l.vals[ii].val.s.s, key, len ) )
        {
            continue;
        }
        return &val->val.l.vals[ii+1];
    }

    return NULL;
}

tr_benc*
tr_bencDictFindType( tr_benc * val, const char * key, int type )
{
    tr_benc * ret = tr_bencDictFind( val, key );
    return ret && ret->type == type ? ret : NULL;
}

tr_benc *
tr_bencDictFindFirst( tr_benc * val, ... )
{
    const char * key;
    tr_benc * ret;
    va_list      ap;

    ret = NULL;
    va_start( ap, val );
    while(( key = va_arg( ap, const char * )))
        if(( ret = tr_bencDictFind( val, key )))
            break;
    va_end( ap );

    return ret;
}

int
tr_bencListSize( const tr_benc * list )
{
    return tr_bencIsList( list ) ? list->val.l.count : 0;
}

tr_benc*
tr_bencListChild( tr_benc * val, int i )
{
    tr_benc * ret = NULL;
    if( tr_bencIsList( val ) && ( i >= 0 ) && ( i < val->val.l.count ) )
        ret = val->val.l.vals + i;
    return ret;
}

int
tr_bencGetInt ( const tr_benc * val, int64_t * setme )
{
    const int success = tr_bencIsInt( val );
    if( success )
        *setme = val->val.i ;
    return success;
}

int
tr_bencGetStr( const tr_benc * val, const char ** setme )
{
    const int success = tr_bencIsString( val );
    if( success )
        *setme = val->val.s.s;
    return success;
}

int
tr_bencDictFindInt( tr_benc * dict, const char * key, int64_t * setme )
{
    int found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_INT );
    if( child )
        found = tr_bencGetInt( child, setme );
    return found;
}

int
tr_bencDictFindDouble( tr_benc * dict, const char * key, double * setme )
{
    const char * str;
    const int success = tr_bencDictFindStr( dict, key, &str );
    if( success )
        *setme = strtod( str, NULL );
    return success;
}

int
tr_bencDictFindList( tr_benc * dict, const char * key, tr_benc ** setme )
{
    int found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_LIST );
    if( child ) {
        *setme = child;
        found = TRUE;
    }
    return found;
}

int
tr_bencDictFindDict( tr_benc * dict, const char * key, tr_benc ** setme )
{
    int found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_DICT );
    if( child ) {
        *setme = child;
        found = TRUE;
    }
    return found;
}

int
tr_bencDictFindStr( tr_benc * dict, const char * key, const char ** setme )
{
    int found = FALSE;
    tr_benc * child = tr_bencDictFindType( dict, key, TYPE_STR );
    if( child ) {
        *setme = child->val.s.s;
        found = TRUE;
    }
    return found;
}

/***
****
***/

void
_tr_bencInitStr( tr_benc * val, char * str, int len, int nofree )
{
    tr_bencInit( val, TYPE_STR );
    val->val.s.s      = str;
    val->val.s.nofree = nofree;
    if( 0 >= len )
    {
        len = ( NULL == str ? 0 : strlen( str ) );
    }
    val->val.s.i = len;
}

void
tr_bencInitRaw( tr_benc * val, const void * src, size_t byteCount )
{
    tr_bencInit( val, TYPE_STR );
    val->val.s.i = byteCount;
    val->val.s.s = tr_new( char, byteCount );
    val->val.s.nofree = 0;
    memcpy( val->val.s.s, src, byteCount );
}

int
tr_bencInitStrDup( tr_benc * val, const char * str )
{
    char * newStr = tr_strdup( str );
    if( newStr == NULL )
        return 1;

    _tr_bencInitStr( val, newStr, 0, 0 );
    return 0;
}

void
tr_bencInitInt( tr_benc * val, int64_t num )
{
    tr_bencInit( val, TYPE_INT );
    val->val.i = num;
}

int
tr_bencInitList( tr_benc * val, int reserveCount )
{
    tr_bencInit( val, TYPE_LIST );
    return tr_bencListReserve( val, reserveCount );
}

int
tr_bencListReserve( tr_benc * val, int count )
{
    assert( tr_bencIsList( val ) );
    return makeroom( val, count );
}

int
tr_bencInitDict( tr_benc * val, int reserveCount )
{
    tr_bencInit( val, TYPE_DICT );
    return tr_bencDictReserve( val, reserveCount );
}

int
tr_bencDictReserve( tr_benc * val, int count )
{
    assert( tr_bencIsDict( val ) );
    return makeroom( val, count * 2 );
}

tr_benc *
tr_bencListAdd( tr_benc * list )
{
    tr_benc * item;

    assert( tr_bencIsList( list ) );

    if( list->val.l.count == list->val.l.alloc )
        tr_bencListReserve( list, LIST_SIZE );

    assert( list->val.l.count < list->val.l.alloc );

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    tr_bencInit( item, TYPE_INT );

    return item;
}
tr_benc *
tr_bencListAddInt( tr_benc * list, int64_t val )
{
    tr_benc * node = tr_bencListAdd( list );
    tr_bencInitInt( node, val );
    return node;
}
tr_benc *
tr_bencListAddStr( tr_benc * list, const char * val )
{
    tr_benc * node = tr_bencListAdd( list );
    tr_bencInitStrDup( node, val );
    return node;
}
tr_benc*
tr_bencListAddList( tr_benc * list, int reserveCount )
{
    tr_benc * child = tr_bencListAdd( list );
    tr_bencInitList( child, reserveCount );
    return child;
}
tr_benc*
tr_bencListAddDict( tr_benc * list, int reserveCount )
{
    tr_benc * child = tr_bencListAdd( list );
    tr_bencInitDict( child, reserveCount );
    return child;
}

tr_benc *
tr_bencDictAdd( tr_benc * dict, const char * key )
{
    tr_benc * keyval, * itemval;

    assert( tr_bencIsDict( dict ) );
    if( dict->val.l.count + 2 > dict->val.l.alloc )
        makeroom( dict, 2 );
    assert( dict->val.l.count + 2 <= dict->val.l.alloc );

    keyval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInitStrDup( keyval, key );

    itemval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInit( itemval, TYPE_INT );

    return itemval;
}
tr_benc*
tr_bencDictAddInt( tr_benc * dict, const char * key, int64_t val )
{
    tr_benc * child = tr_bencDictAdd( dict, key );
    tr_bencInitInt( child, val );
    return child;
}
tr_benc*
tr_bencDictAddStr( tr_benc * dict, const char * key, const char * val )
{
    tr_benc * child = tr_bencDictAdd( dict, key );
    tr_bencInitStrDup( child, val );
    return child;
}
tr_benc*
tr_bencDictAddDouble( tr_benc * dict, const char * key, double d )
{
    char buf[128];
    snprintf( buf, sizeof( buf ), "%f", d );
    return tr_bencDictAddStr( dict, key, buf );
}
tr_benc*
tr_bencDictAddList( tr_benc * dict, const char * key, int reserveCount )
{
    tr_benc * child = tr_bencDictAdd( dict, key );
    tr_bencInitList( child, reserveCount );
    return child;
}
tr_benc*
tr_bencDictAddDict( tr_benc * dict, const char * key, int reserveCount )
{
    tr_benc * child = tr_bencDictAdd( dict, key );
    tr_bencInitDict( child, reserveCount );
    return child;
}
tr_benc*
tr_bencDictAddRaw( tr_benc * dict, const char * key, const void * src, size_t len )
{
    tr_benc * child = tr_bencDictAdd( dict, key );
    tr_bencInitRaw( child, src, len );
    return child;
}

/***
****  BENC WALKING
***/

struct KeyIndex
{
    const char * key;
    int index;
};

static int
compareKeyIndex( const void * va, const void * vb )
{
    const struct KeyIndex * a = va;
    const struct KeyIndex * b = vb;
    return strcmp( a->key, b->key );
}

struct SaveNode
{
    const tr_benc * val;
    int valIsVisited;
    int childCount;
    int childIndex;
    int * children;
};

static struct SaveNode*
nodeNewDict( const tr_benc * val )
{
    int i, j;
    int nKeys;
    struct SaveNode * node;
    struct KeyIndex * indices;

    assert( tr_bencIsDict( val ) );

    nKeys = val->val.l.count / 2;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->children = tr_new0( int, nKeys * 2 );

    /* ugh, a dictionary's children have to be sorted by key... */
    indices = tr_new( struct KeyIndex, nKeys );
    for( i=j=0; i<(nKeys*2); i+=2, ++j ) {
        indices[j].key = val->val.l.vals[i].val.s.s;
        indices[j].index = i;
    }
    qsort( indices, j, sizeof(struct KeyIndex), compareKeyIndex );
    for( i=0; i<j; ++i ) {
        const int index = indices[i].index;
        node->children[ node->childCount++ ] = index;
        node->children[ node->childCount++ ] = index + 1;
    }

    assert( node->childCount == nKeys * 2 );
    tr_free( indices );
    return node;
}

static struct SaveNode*
nodeNewList( const tr_benc * val )
{
    int i, n;
    struct SaveNode * node;

    assert( tr_bencIsList( val ) );

    n = val->val.l.count;
    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    node->childCount = n;
    node->children = tr_new0( int, n );
    for( i=0; i<n; ++i ) /* a list's children don't need to be reordered */
        node->children[i] = i;

    return node;
}

static struct SaveNode*
nodeNewLeaf( const tr_benc * val )
{
    struct SaveNode * node;

    assert( !isContainer( val ) );

    node = tr_new0( struct SaveNode, 1 );
    node->val = val;
    return node;
}

static struct SaveNode*
nodeNew( const tr_benc * val )
{
    struct SaveNode * node;

    if( tr_bencIsList( val ) )
        node = nodeNewList( val );
    else if( tr_bencIsDict( val ) )
        node = nodeNewDict( val );
    else
        node = nodeNewLeaf( val );

    return node;
}

typedef void (*BencWalkFunc)( const tr_benc * val, void * user_data );

struct WalkFuncs
{
    BencWalkFunc intFunc;
    BencWalkFunc stringFunc;
    BencWalkFunc dictBeginFunc;
    BencWalkFunc listBeginFunc;
    BencWalkFunc containerEndFunc;
};

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
static void
bencWalk( const tr_benc   * top,
          struct WalkFuncs   * walkFuncs,
          void               * user_data )
{
    tr_ptrArray * stack = tr_ptrArrayNew( );
    tr_ptrArrayAppend( stack, nodeNew( top ) );

    while( !tr_ptrArrayEmpty( stack ) )
    {
        struct SaveNode * node = tr_ptrArrayBack( stack );
        const tr_benc * val;

        if( !node->valIsVisited )
        {
            val = node->val;
            node->valIsVisited = TRUE;
        }
        else if( node->childIndex < node->childCount )
        {
            const int index = node->children[ node->childIndex++ ];
            val = node->val->val.l.vals +  index;
        }
        else /* done with this node */
        {
            if( isContainer( node->val ) )
                walkFuncs->containerEndFunc( node->val, user_data );
            tr_ptrArrayPop( stack );
            tr_free( node->children );
            tr_free( node );
            continue;
        }

        switch( val->type )
        {
            case TYPE_INT:
                walkFuncs->intFunc( val, user_data );
                break;

            case TYPE_STR:
                walkFuncs->stringFunc( val, user_data );
                break;

            case TYPE_LIST:
                if( val != node->val )
                    tr_ptrArrayAppend( stack, nodeNew( val ) );
                else
                    walkFuncs->listBeginFunc( val, user_data );
                break;

            case TYPE_DICT:
                if( val != node->val )
                    tr_ptrArrayAppend( stack, nodeNew( val ) );
                else
                    walkFuncs->dictBeginFunc( val, user_data );
                break;

            default:
                /* did caller give us an uninitialized val? */
                tr_err( _( "Invalid metadata" ) );
                break;
        }
    }

    tr_ptrArrayFree( stack, NULL );
}

/****
*****
****/

static void
saveIntFunc( const tr_benc * val, void * evbuf )
{
    evbuffer_add_printf( evbuf, "i%"PRId64"e", val->val.i );
}
static void
saveStringFunc( const tr_benc * val, void * vevbuf )
{
    struct evbuffer * evbuf = vevbuf;
    evbuffer_add_printf( evbuf, "%d:", val->val.s.i );
    evbuffer_add( evbuf, val->val.s.s, val->val.s.i );
}
static void
saveDictBeginFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "d" );
}
static void
saveListBeginFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "l" );
}
static void
saveContainerEndFunc( const tr_benc * val UNUSED, void * evbuf )
{
    evbuffer_add_printf( evbuf, "e" );
}
char*
tr_bencSave( const tr_benc * top, int * len )
{
    char * ret;
    struct WalkFuncs walkFuncs;
    struct evbuffer * out = evbuffer_new( );

    walkFuncs.intFunc = saveIntFunc;
    walkFuncs.stringFunc = saveStringFunc;
    walkFuncs.dictBeginFunc = saveDictBeginFunc;
    walkFuncs.listBeginFunc = saveListBeginFunc;
    walkFuncs.containerEndFunc = saveContainerEndFunc;
    bencWalk( top, &walkFuncs, out );
    
    if( len != NULL )
        *len = EVBUFFER_LENGTH( out );
    ret = tr_strndup( (char*) EVBUFFER_DATA( out ), EVBUFFER_LENGTH( out ) );
    evbuffer_free( out );
    return ret;
}

/***
****
***/

static void
freeDummyFunc( const tr_benc * val UNUSED, void * buf UNUSED  )
{
}
static void
freeStringFunc( const tr_benc * val, void * freeme )
{
    if( !val->val.s.nofree )
        tr_ptrArrayAppend( freeme, val->val.s.s );
}
static void
freeContainerBeginFunc( const tr_benc * val, void * freeme )
{
    tr_ptrArrayAppend( freeme, val->val.l.vals );
}
void
tr_bencFree( tr_benc * val )
{
    if( val != NULL )
    {
        tr_ptrArray * freeme = tr_ptrArrayNew( );
        struct WalkFuncs walkFuncs;

        walkFuncs.intFunc = freeDummyFunc;
        walkFuncs.stringFunc = freeStringFunc;
        walkFuncs.dictBeginFunc = freeContainerBeginFunc;
        walkFuncs.listBeginFunc = freeContainerBeginFunc;
        walkFuncs.containerEndFunc = freeDummyFunc;
        bencWalk( val, &walkFuncs, freeme );

        tr_ptrArrayFree( freeme, tr_free );
    }
}

/***
****
***/

struct WalkPrint
{
    int depth;
    FILE * out;
};
static void
printLeadingSpaces( struct WalkPrint * data )
{
    const int width = data->depth * 2;
    fprintf( data->out, "%*.*s", width, width, " " );
}
static void
printIntFunc( const tr_benc * val, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "int:  %"PRId64"\n", val->val.i );
}
static void
printStringFunc( const tr_benc * val, void * vdata )
{
    int ii;
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "string:  " );
    for( ii = 0; val->val.s.i > ii; ii++ )
    {
        if( '\\' == val->val.s.s[ii] ) {
            putc( '\\', data->out );
            putc( '\\', data->out );
        } else if( isprint( val->val.s.s[ii] ) ) {
            putc( val->val.s.s[ii], data->out );
        } else {
            fprintf( data->out, "\\x%02x", val->val.s.s[ii] );
        }
    }
    fprintf( data->out, "\n" );
}
static void
printListBeginFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "list\n" );
    ++data->depth;
}
static void
printDictBeginFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    printLeadingSpaces( data );
    fprintf( data->out, "dict\n" );
    ++data->depth;
}
static void
printContainerEndFunc( const tr_benc * val UNUSED, void * vdata )
{
    struct WalkPrint * data = vdata;
    --data->depth;
}
void
tr_bencPrint( const tr_benc * val )
{
    struct WalkFuncs walkFuncs;
    struct WalkPrint walkPrint;

    walkFuncs.intFunc = printIntFunc;
    walkFuncs.stringFunc = printStringFunc;
    walkFuncs.dictBeginFunc = printDictBeginFunc;
    walkFuncs.listBeginFunc = printListBeginFunc;
    walkFuncs.containerEndFunc = printContainerEndFunc;

    walkPrint.out = stderr;
    walkPrint.depth = 0;
    bencWalk( val, &walkFuncs, &walkPrint );
}

/***
****
***/

struct ParentState
{
    int bencType;
    int childIndex;
    int childCount;
};
 
struct jsonWalk
{
    tr_list * parents;
    struct evbuffer * out;
};

static void
jsonChildFunc( struct jsonWalk * data )
{
    if( data->parents )
    {
        struct ParentState * parentState = data->parents->data;

        switch( parentState->bencType )
        {
            case TYPE_DICT: {
                const int i = parentState->childIndex++;
                if( ! ( i % 2 ) )
                    evbuffer_add_printf( data->out, ": " );
                else
                    evbuffer_add_printf( data->out, ", " );
                break;
            }

            case TYPE_LIST: {
                ++parentState->childIndex;
                evbuffer_add_printf( data->out, ", " );
                break;
            }

            default:
                break;
        }
    }
}

static void
jsonPushParent( struct jsonWalk * data, const tr_benc * benc )
{
    struct ParentState * parentState = tr_new( struct ParentState, 1 );
    parentState->bencType = benc->type;
    parentState->childIndex = 0;
    parentState->childCount = benc->val.l.count;
    tr_list_prepend( &data->parents, parentState );
}

static void
jsonPopParent( struct jsonWalk * data )
{
    tr_free( tr_list_pop_front( &data->parents ) );
}

static void
jsonIntFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;
    evbuffer_add_printf( data->out, "%"PRId64, val->val.i );
    jsonChildFunc( data );
}
static void
jsonStringFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;
    const char *it, *end;
    evbuffer_add_printf( data->out, "\"" );
    for( it=val->val.s.s, end=it+val->val.s.i; it!=end; ++it )
    {
        switch( *it ) {
            case '"' : evbuffer_add_printf( data->out, "\\\"" ); break;
            case '/' : evbuffer_add_printf( data->out, "\\/" ); break;
            case '\b': evbuffer_add_printf( data->out, "\\b" ); break;
            case '\f': evbuffer_add_printf( data->out, "\\f" ); break;
            case '\n': evbuffer_add_printf( data->out, "\\n" ); break;
            case '\r': evbuffer_add_printf( data->out, "\\n" ); break;
            case '\t': evbuffer_add_printf( data->out, "\\n" ); break;
            case '\\': evbuffer_add_printf( data->out, "\\\\" ); break;
            default: {
                if( isascii( *it ) )
                    evbuffer_add_printf( data->out, "%c", *it );
                else
                    evbuffer_add_printf( data->out, "\\u%0x", (unsigned int)*it );
                break;
            }
        }
    }
    evbuffer_add_printf( data->out, "\"" );
    jsonChildFunc( data );
}
static void
jsonDictBeginFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;
    jsonPushParent( data, val );
    evbuffer_add_printf( data->out, "{ " );
    if( !val->val.l.count )
        evbuffer_add_printf( data->out, "  " );
}
static void
jsonListBeginFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;
    jsonPushParent( data, val );
    evbuffer_add_printf( data->out, "[ " );
    if( !val->val.l.count )
        evbuffer_add_printf( data->out, "  " );
}
static void
jsonContainerEndFunc( const tr_benc * val, void * vdata )
{
    struct jsonWalk * data = vdata;
    EVBUFFER_LENGTH( data->out ) -= 2;
    if( tr_bencIsDict( val ) )
        evbuffer_add_printf( data->out, " }" );
    else /* list */
        evbuffer_add_printf( data->out, " ]" );
    jsonPopParent( data );
    jsonChildFunc( data );
}
char*
tr_bencSaveAsJSON( const tr_benc * top, int * len )
{
    char * ret;
    struct WalkFuncs walkFuncs;
    struct jsonWalk data;

    data.out = evbuffer_new( );
    data.parents = NULL;

    walkFuncs.intFunc = jsonIntFunc;
    walkFuncs.stringFunc = jsonStringFunc;
    walkFuncs.dictBeginFunc = jsonDictBeginFunc;
    walkFuncs.listBeginFunc = jsonListBeginFunc;
    walkFuncs.containerEndFunc = jsonContainerEndFunc;

    bencWalk( top, &walkFuncs, &data );
    
    if( len != NULL )
        *len = EVBUFFER_LENGTH( data.out );
    ret = tr_strndup( (char*) EVBUFFER_DATA( data.out ), EVBUFFER_LENGTH( data.out ) );
    evbuffer_free( data.out );
    return ret;
}

/***
****
***/

int
tr_bencSaveFile( const char * filename,  const tr_benc * b )
{
    int err = TR_OK;
    int len;
    char * content = tr_bencSave( b, &len );
    FILE * out = NULL;

    out = fopen( filename, "wb+" );
    if( !out )
    {
        tr_err( _( "Couldn't open \"%1$s\": %2$s" ),
                filename, tr_strerror( errno ) );
        err = TR_EINVALID;
    }
    else if( fwrite( content, sizeof( char ), len, out ) != (size_t)len )
    {
        tr_err( _( "Couldn't save file \"%1$s\": %2$s" ),
                filename, tr_strerror( errno ) );
        err = TR_EINVALID;
    }

    if( !err )
        tr_dbg( "tr_bencSaveFile saved \"%s\"", filename );
    tr_free( content );
    if( out )
        fclose( out );
    return err;
}

int
tr_bencLoadFile( const char * filename, tr_benc * b )
{
    int ret;
    size_t contentLen;
    uint8_t * content = tr_loadFile( filename, &contentLen );
    ret = content ? tr_bencLoad( content, contentLen, b, NULL )
                  : TR_ERROR_IO_OTHER;
    tr_free( content );
    return ret;
}
