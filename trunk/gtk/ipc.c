/*
  Copyright (c) 2006 Joshua Elsasser. All rights reserved.
   
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   
  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "bencode.h"
#include "conf.h"
#include "io.h"
#include "ipc.h"
#include "util.h"

#define PROTOVERS               1       /* IPC protocol version */

/* int, IPC protocol version */
#define MSG_VERSION             ("version")
/* list of strings, full paths to torrent files to load */
#define MSG_ADDFILES            ("addfiles")

enum contype { CON_SERV, CON_ADDFILE };

struct constate_serv {
  void *wind;
  add_torrent_func_t addfunc;
  torrents_added_func_t donefunc;
  void *cbdata;
};

struct constate_addfile {
  GMainLoop *loop;
  GList *files;
  gboolean *succeeded;
  unsigned int addid;
};

struct constate;
typedef void (*handler_func_t)(struct constate*, const char*, benc_val_t *);
struct handlerdef {char *name; handler_func_t func;};

struct constate {
  GSource *source;
  int fd;
  const struct handlerdef *funcs;
  enum contype type;
  union {
    struct constate_serv serv;
    struct constate_addfile addfile;
  } u;
};

void
ipc_socket_setup(void *parent, add_torrent_func_t addfunc,
                 torrents_added_func_t donefunc, void *cbdata);
gboolean
ipc_sendfiles_blocking(GList *files);
static void
serv_bind(struct constate *con);
static void
rmsock(void);
static gboolean
client_connect(char *path, struct constate *con);
static void
srv_io_accept(GSource *source, int fd, struct sockaddr *sa, socklen_t len,
              void *vdata);
static int
send_msg(struct constate *con, const char *name, benc_val_t *val);
static int
send_msg_int(struct constate *con, const char *name, int num);
static unsigned int
all_io_received(GSource *source, char *data, unsigned int len, void *vdata);
static void
destroycon(struct constate *con);
static void
all_io_closed(GSource *source, void *vdata);
static void
srv_addfile(struct constate *con, const char *name, benc_val_t *val);
static void
afc_version(struct constate *con, const char *name, benc_val_t *val);
static void
afc_io_sent(GSource *source, unsigned int id, void *vdata);

static const struct handlerdef gl_funcs_serv[] = {
  {MSG_ADDFILES, srv_addfile},
  {NULL, NULL}
};

static const struct handlerdef gl_funcs_addfile[] = {
  {MSG_VERSION, afc_version},
  {NULL, NULL}
};

/* this is only used on the server */
static char *gl_sockpath = NULL;

void
ipc_socket_setup(void *parent, add_torrent_func_t addfunc,
                 torrents_added_func_t donefunc, void *cbdata) {
  struct constate *con;

  con = g_new0(struct constate, 1);
  con->source = NULL;
  con->fd = -1;
  con->funcs = gl_funcs_serv;
  con->type = CON_SERV;
  con->u.serv.wind = parent;
  con->u.serv.addfunc = addfunc;
  con->u.serv.donefunc = donefunc;
  con->u.serv.cbdata = cbdata;
  
  serv_bind(con);
}

gboolean
ipc_sendfiles_blocking(GList *files) {
  struct constate *con;
  char *path;
  gboolean ret = FALSE;

  con = g_new0(struct constate, 1);
  con->source = NULL;
  con->fd = -1;
  con->funcs = gl_funcs_addfile;
  con->type = CON_ADDFILE;
  con->u.addfile.loop = g_main_loop_new(NULL, TRUE);
  con->u.addfile.files = files;
  con->u.addfile.succeeded = &ret;
  con->u.addfile.addid = 0;

  path = cf_sockname();
  if(!client_connect(path, con)) {
    g_free(path);
    destroycon(con);
    return FALSE;
  }

  g_main_loop_run(con->u.addfile.loop);

  return ret;
}

/* open a local socket for clients connections */
static void
serv_bind(struct constate *con) {
  struct sockaddr_un sa;

  rmsock();
  gl_sockpath = cf_sockname();

  if(0 > (con->fd = socket(AF_LOCAL, SOCK_STREAM, 0)))
    goto fail;

  bzero(&sa, sizeof(sa));
  sa.sun_family = AF_LOCAL;
  strncpy(sa.sun_path, gl_sockpath, sizeof(sa.sun_path) - 1);

  /* unlink any existing socket file before trying to create ours */
  unlink(gl_sockpath);
  if(0 > bind(con->fd, (struct sockaddr *)&sa, SUN_LEN(&sa))) {
    /* bind may fail if there was already a socket, so try twice */
    unlink(gl_sockpath);
    if(0 > bind(con->fd, (struct sockaddr *)&sa, SUN_LEN(&sa)))
      goto fail;
  }

  if(0 > listen(con->fd, 5))
    goto fail;

  con->source = io_new_listening(con->fd, sizeof(struct sockaddr_un),
                                 srv_io_accept, all_io_closed, con);

  g_atexit(rmsock);

  return;

 fail:
  errmsg(con->u.serv.wind, _("Failed to set up socket:\n%s"),
         strerror(errno));
  if(0 <= con->fd)
    close(con->fd);
  con->fd = -1;
  rmsock();
}

static void
rmsock(void) {
  if(NULL != gl_sockpath) {
    unlink(gl_sockpath);
    g_free(gl_sockpath);
  }
}

static gboolean
client_connect(char *path, struct constate *con) {
  struct sockaddr_un addr;

  if(0 > (con->fd = socket(AF_UNIX, SOCK_STREAM, 0))) {
    fprintf(stderr, _("failed to create socket: %s\n"), strerror(errno));
    return FALSE;
  }

  bzero(&addr, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  if(0 > connect(con->fd, (struct sockaddr*)&addr, SUN_LEN(&addr))) {
    fprintf(stderr, _("failed to connect to %s: %s\n"), path, strerror(errno));
    return FALSE;
  }

  con->source = io_new(con->fd, afc_io_sent, all_io_received,
                       all_io_closed, con);

  return TRUE;
}

static void
srv_io_accept(GSource *source SHUTUP, int fd, struct sockaddr *sa SHUTUP,
              socklen_t len SHUTUP, void *vdata) {
  struct constate *con = vdata;
  struct constate *newcon;

  newcon = g_new(struct constate, 1);
  memcpy(newcon, con, sizeof(*newcon));
  newcon->fd = fd;
  newcon->source = io_new(fd, NULL, all_io_received, all_io_closed, newcon);

  if(NULL != newcon->source)
    send_msg_int(newcon, MSG_VERSION, PROTOVERS);
  else {
    g_free(newcon);
    close(fd);
  }
}

static int
send_msg(struct constate *con, const char *name, benc_val_t *val) {
  char *buf;
  size_t used, total;
  benc_val_t dict;
  char stupid;

  /* construct a dictionary value */
  /* XXX I shouldn't be constructing benc_val_t's by hand */
  bzero(&dict, sizeof(dict));
  dict.type = TYPE_DICT;
  dict.val.l.alloc = 2;
  dict.val.l.count = 2;
  dict.val.l.vals = g_new0(benc_val_t, 2);
  dict.val.l.vals[0].type = TYPE_STR;
  dict.val.l.vals[0].val.s.i = strlen(name);
  dict.val.l.vals[0].val.s.s = (char*)name;
  dict.val.l.vals[1] = *val;

  /* bencode the dictionary, starting at offset 8 in the buffer */
  buf = malloc(9);
  g_assert(NULL != buf);
  total = 9;
  used = 8;
  if(tr_bencSave(&dict, &buf, &used, &total)) {
    g_assert_not_reached();
  }
  g_free(dict.val.l.vals);

  /* write the bencoded data length into the first 8 bytes of the buffer */
  stupid = buf[8];
  snprintf(buf, 9, "%08X", used - 8);
  buf[8] = stupid;

  /* send the data */
  return io_send_keepdata(con->source, buf, used);
}

static int
send_msg_int(struct constate *con, const char *name, int num) {
  benc_val_t val;

  bzero(&val, sizeof(val));
  val.type = TYPE_INT;
  val.val.i = num;

  return send_msg(con, name, &val);
}

static unsigned int
all_io_received(GSource *source, char *data, unsigned int len, void *vdata) {
  struct constate *con = vdata;
  size_t size;
  char stupid;
  char *end;
  benc_val_t msgs;
  int ii, jj;

  if(9 > len)
    return 0;

  stupid = data[8];
  data[8] = '\0';
  size = strtoul(data, NULL, 16);
  data[8] = stupid;

  if(size + 8 > len)
    return 0;

  if(!tr_bencLoad(data + 8, size, &msgs, &end) && TYPE_DICT == msgs.type) {
    for(ii = 0; msgs.val.l.count > ii + 1; ii += 2)
      if(TYPE_STR == msgs.val.l.vals[ii].type)
        for(jj = 0; NULL != con->funcs[jj].name; jj++)
          if(0 == strcmp(msgs.val.l.vals[ii].val.s.s, con->funcs[jj].name)) {
            con->funcs[jj].func(con, msgs.val.l.vals[ii].val.s.s,
                                msgs.val.l.vals + ii + 1);
            break;
          }
    tr_bencFree(&msgs);
  }

  return size + 8 +
    all_io_received(source, data + size + 8, len - size - 8, con);
}

static void
destroycon(struct constate *con) {
  con->source = NULL;

  if(0 <= con->fd)
    close(con->fd);
  con->fd = -1;

  switch(con->type) {
    case CON_SERV:
      break;
    case CON_ADDFILE:
      freestrlist(con->u.addfile.files);
      g_main_loop_quit(con->u.addfile.loop);
      break;
  }
}

static void
all_io_closed(GSource *source SHUTUP, void *vdata) {
  struct constate *con = vdata;

  destroycon(con);
}

static void
srv_addfile(struct constate *con, const char *name SHUTUP, benc_val_t *val) {
  struct constate_serv *srv = &con->u.serv;
  GList *errs = NULL;
  char *str;
  int ii;
  gboolean added;
  benc_val_t *file;

  if(TYPE_LIST == val->type) {
    added = FALSE;
    for(ii = 0; ii < val->val.l.count; ii++) {
      file = &val->val.l.vals[ii];
      if(TYPE_STR == file->type && g_utf8_validate(file->val.s.s, -1, NULL)) {
        /* XXX somehow escape invalid utf-8 */
        added = TRUE;
        srv->addfunc(srv->cbdata, file->val.s.s, NULL, FALSE, &errs);
      }
    }

    if(NULL != errs) {
      str = joinstrlist(errs, "\n");
      errmsg(srv->wind, ngettext("Failed to load the torrent file %s",
                                 "Failed to load the torrent files:\n%s",
                                 g_list_length(errs)), str);
      freestrlist(errs);
      g_free(str);
    }
    if(added)
      srv->donefunc(srv->cbdata);
  }
}

static void
afc_version(struct constate *con, const char *name SHUTUP, benc_val_t *val) {
  struct constate_addfile *afc = &con->u.addfile;
  GList *file;
  benc_val_t list, *str;

  if(TYPE_INT != val->type || PROTOVERS != val->val.i) {
    fprintf(stderr, _("bad IPC protocol version\n"));
    destroycon(con);
  } else {
    /* XXX handle getting a non-version tag, invalid data,
           or nothing (read timeout) */
    bzero(&list, sizeof(list));
    list.type = TYPE_LIST;
    list.val.l.alloc = g_list_length(afc->files);
    list.val.l.vals = g_new0(benc_val_t, list.val.l.alloc);
    for(file = afc->files; NULL != file; file = file->next) {
      str = list.val.l.vals + list.val.l.count;
      str->type = TYPE_STR;
      str->val.s.i = strlen(file->data);
      str->val.s.s = file->data;
      list.val.l.count++;
    }
    g_list_free(afc->files);
    afc->files = NULL;
    afc->addid = send_msg(con, MSG_ADDFILES, &list);
    tr_bencFree(&list);
  }
}

static void
afc_io_sent(GSource *source SHUTUP, unsigned int id, void *vdata) {
  struct constate_addfile *afc = &((struct constate*)vdata)->u.addfile;

  if(0 < id && afc->addid == id) {
    *(afc->succeeded) = TRUE;
    destroycon(vdata);
  }
}
