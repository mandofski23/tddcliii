/*
    This file is part of telegram-cli.

    Telegram-cli is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-cli is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-cli.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013-2015
*/

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

//#include "queries.h"

#include "interface.h"
#include "telegram.h"

#ifdef EVENT_V2
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#else
#include <event.h>
#include "event-old.h"
#endif
//#include "auto/constants.h"
//#include "tools.h"
//#include "structures.h"

#ifdef USE_LUA
#  include "lua-tg.h"
#endif


//#include "mtproto-common.h"

#include "loop.h"

#include "tdc/tdlib-c-bindings.h"
#include "telegram-layout.h"


#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __APPLE__
#define OPEN_BIN "open '%s'"
#else
#define OPEN_BIN "xdg-open '%s'"
#endif

#ifdef USE_JSON
#  include <jansson.h>
#  include "json-tg.h"
#endif

#include <errno.h>

#include "tree.h"

extern struct event_base *ev_base;

int total_unread;
int disable_msg_preview;

void empty_cb (struct tdlib_state *TLS, void *extra, int success) {}

enum tdl_connection_state conn_state;

struct delayed_query {
  struct in_command *cmd;
  //char *line;
  //struct in_ev *ev;

  char *token;
  int action;
  int mode;
};

#define chat_alias_cmp(a,b) strcmp (a->name, b->name)
DEFINE_TREE (chat_alias, struct chat_alias *, chat_alias_cmp, NULL)

struct message_alias {
  int local_id;
  int message_id;
  long long chat_id;
};
#define message_alias_cmp_local(a,b) (a->local_id - b->local_id)
#define message_alias_cmp_global(a,b) memcmp(&a->message_id, &b->message_id, 12)
DEFINE_TREE (message_alias_local, struct message_alias *, message_alias_cmp_local, NULL)
DEFINE_TREE (message_alias_global, struct message_alias *, message_alias_cmp_global, NULL)

struct tree_message_alias_local *message_local_tree;
struct tree_message_alias_global *message_global_tree;
int message_local_last_id;

struct file_wait_cb {
  struct file_wait_cb *next;
  void (*callback)(struct tdlib_state *, void *, int, const char *);
  void *callback_extra;
};

struct file_wait {
  int id;
  struct file_wait_cb *first_cb;
  struct file_wait_cb *last_cb;
};

#define file_wait_cmp(a,b) (a->id - b->id)
DEFINE_TREE (file_wait, struct file_wait *, file_wait_cmp, NULL)
struct tree_file_wait *file_wait_tree;

struct message_alias *convert_local_to_global (int local_id) {
  struct message_alias M;
  M.local_id = local_id;
  return tree_lookup_message_alias_local (message_local_tree, &M);
}

struct message_alias *convert_global_to_local (long long chat_id, int message_id) {
  struct message_alias M;
  M.chat_id = chat_id;
  M.message_id = message_id;

  struct message_alias *A = tree_lookup_message_alias_global (message_global_tree, &M);
  if (A) { return A; }
  A = malloc (sizeof (*A));
  A->local_id = ++ message_local_last_id;
  A->chat_id = chat_id;
  A->message_id = message_id;
  message_global_tree = tree_insert_message_alias_global (message_global_tree, A, rand ());
  message_local_tree = tree_insert_message_alias_local (message_local_tree, A, rand ());

  return A;
}

struct tree_chat_alias *alias_tree;
struct chat_alias alias_queue;

void in_command_decref (struct in_command *cmd) {
  if (!--cmd->refcnt) {
    free (cmd->line);
  
    if (cmd->ev && !--cmd->ev->refcnt) {
      free (cmd->ev);
    }
    
    free (cmd);
  }
}


#define ALLOW_MULT 1
char *default_prompt = "> ";

extern int read_one_string;
extern int enable_json;
int disable_auto_accept;
int msg_num_mode;
int permanent_msg_id_mode;
int permanent_peer_id_mode;
int disable_colors;
extern int alert_sound;
extern int binlog_read;
extern char *home_directory;

int safe_quit;

int in_readline;
int readline_active;

int log_level;

char *line_ptr;

struct tdl_chat_info *cur_chat_mode_chat;
extern int readline_disabled;

extern int disable_output;

struct in_ev *notify_ev;

extern int usfd;
extern int sfd;
extern int use_ids;

extern int daemonize;

extern struct tdlib_state *TLS;
int readline_deactivated;

void add_alias (const char *name, int type, void *chat) {
  if (alias_queue.next == NULL) {
    alias_queue.next = alias_queue.prev = &alias_queue;
  }
  struct chat_alias *alias = malloc (sizeof (*alias));
  alias->name = strdup (name);
  alias->type = type;
  alias->chat = chat;
  alias_tree = tree_insert_chat_alias (alias_tree, alias, rand ());
  alias->next = &alias_queue;
  alias->prev = alias_queue.prev;
  alias->next->prev = alias->prev->next = alias;
}

void del_alias (const char *name) {
  struct chat_alias tmp;
  tmp.name = (void *)name;

  struct chat_alias *alias = tree_lookup_chat_alias (alias_tree, &tmp);
  if (alias) {
    alias_tree = tree_delete_chat_alias (alias_tree, alias);
    alias->prev->next = alias->next;
    alias->next->prev = alias->prev;
    free (alias->name);
    free (alias);
  }
}

struct chat_alias *get_by_alias (const char *name) {
  struct chat_alias tmp;
  tmp.name = (void *)name;

  return tree_lookup_chat_alias (alias_tree, &tmp);
}

void on_chat_update (struct tdlib_state *TLSR, struct tdl_chat_info *C);

void fail_interface (struct tdlib_state *TLS, struct in_command *cmd, int error_code, const char *format, ...) __attribute__ (( format (printf, 4, 5)));
void event_incoming (struct bufferevent *bev, short what, void *_arg);

int is_same_word (const char *s, size_t l, const char *word) {
  return s && word && strlen (word) == l && !memcmp (s, word, l);
}

static void skip_wspc (void) {
  while (*line_ptr && ((unsigned char)*line_ptr) <= ' ') {
    line_ptr ++;
  }
}

static char *cur_token;
static ssize_t cur_token_len;
static int cur_token_end_str;
static int cur_token_quoted;

#define SOCKET_ANSWER_MAX_SIZE (1 << 25)
static char socket_answer[SOCKET_ANSWER_MAX_SIZE + 1];
static int socket_answer_pos = -1;

void socket_answer_start (void) {
  socket_answer_pos = 0;
}

void socket_answer_add_printf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void socket_answer_add_printf (const char *format, ...) {
  if (socket_answer_pos < 0) { return; }
  va_list ap;
  va_start (ap, format);
  socket_answer_pos += vsnprintf (socket_answer + socket_answer_pos, SOCKET_ANSWER_MAX_SIZE - socket_answer_pos, format, ap);
  va_end (ap);
  if (socket_answer_pos > SOCKET_ANSWER_MAX_SIZE) { socket_answer_pos = -1; }
}

void socket_answer_end (struct in_ev *ev) {
  if (ev->bev) {
    static char s[100];
    sprintf (s, "ANSWER %d\n", socket_answer_pos);
    bufferevent_write (ev->bev, s, strlen (s));
    bufferevent_write (ev->bev, socket_answer, socket_answer_pos);
    bufferevent_write (ev->bev, "\n", 1);
  }
  socket_answer_pos = -1;
}

#define mprintf(ev,...) \
  if (ev) { socket_answer_add_printf (__VA_ARGS__); } \
  else { printf (__VA_ARGS__); } 

#define mprint_start(ev,...) \
  if (!ev) { print_start (__VA_ARGS__); } \
  else { socket_answer_start (); }
  
#define mprint_end(ev,...) \
  if (!ev) { print_end (__VA_ARGS__); } \
  else { socket_answer_end (ev); }

#define mpush_color(ev,...) \
  if (!ev) { push_color (__VA_ARGS__); }

#define mpop_color(ev,...) \
  if (!ev) { pop_color (__VA_ARGS__); }

static void unescape_token (char *start, char *end) {
  static char cur_token_buff[(1 << 20) + 1];
  cur_token_len = 0;
  cur_token = cur_token_buff;
  while (start < end) {
    assert (cur_token_len < (1 << 20));
    switch (*start) {
    case '\\':
      start ++;
      switch (*start) {
      case 'n':
        cur_token[cur_token_len ++] = '\n';
        break;
      case 'r':
        cur_token[cur_token_len ++] = '\r';
        break;
      case 't':
        cur_token[cur_token_len ++] = '\t';
        break;
      case 'b':
        cur_token[cur_token_len ++] = '\b';
        break;
      case 'a':
        cur_token[cur_token_len ++] = '\a';
        break;
      default:
        cur_token[cur_token_len ++] = *start;
        break;
      }
      break;
    default:
      cur_token[cur_token_len ++] = *start;;
      break;
    }
    start ++;
  }
  cur_token[cur_token_len] = 0;
}

int force_end_mode;
static void next_token (void) {
  skip_wspc ();
  cur_token_end_str = 0;
  cur_token_quoted = 0;
  if (!*line_ptr) {
    cur_token_len = 0;
    cur_token_end_str = 1;
    return;
  }
  char c = *line_ptr;
  char *start = line_ptr;
  if (c == '"' || c == '\'') {
    cur_token_quoted = 1;
    line_ptr ++;
    int esc = 0;
    while (*line_ptr && (esc || *line_ptr != c)) {
      if (*line_ptr == '\\') {
        esc = 1 - esc;
      } else {
        esc = 0;
      }
      line_ptr ++;
    }
    if (!*line_ptr) {
      cur_token_len = -2;
    } else {
      unescape_token (start + 1, line_ptr);
      line_ptr ++;
    }
  } else {
    while (*line_ptr && ((unsigned char)*line_ptr) > ' ') {
      line_ptr ++;
    }
    cur_token = start;
    cur_token_len = (line_ptr - start);
    cur_token_end_str = (!force_end_mode) && (*line_ptr == 0);
  }
}

void next_token_end (void) {
  skip_wspc ();
  
  if (*line_ptr && *line_ptr != '"' && *line_ptr != '\'') {
    cur_token_quoted = 0;
    cur_token = line_ptr;
    while (*line_ptr) { line_ptr ++; }
    cur_token_len = (line_ptr - cur_token);
    while (((unsigned char)cur_token[cur_token_len - 1]) <= ' ' && cur_token_len >= 0) { 
      cur_token_len --;
    }
    assert (cur_token_len > 0);
    cur_token_end_str = !force_end_mode;
    return;
  } else {
    if (*line_ptr) {
      next_token ();
      skip_wspc ();
      if (*line_ptr) {
        cur_token_len = -1; 
      }
    } else {
      next_token ();
    }
  }
}

void next_token_end_ac (void) {
  skip_wspc ();
  
  if (*line_ptr && *line_ptr != '"' && *line_ptr != '\'') {
    cur_token_quoted = 0;
    cur_token = line_ptr;
    while (*line_ptr) { line_ptr ++; }
    cur_token_len = (line_ptr - cur_token);
    assert (cur_token_len > 0);
    cur_token_end_str = !force_end_mode;
    return;
  } else {
    if (*line_ptr) {
      next_token ();
      skip_wspc ();
      if (*line_ptr) {
        cur_token_len = -1; 
      }
    } else {
      next_token ();
    }
  }
}

#define NOT_FOUND (int)0x80000000
tgl_peer_id_t TGL_PEER_NOT_FOUND = {.peer_id = NOT_FOUND};

long long cur_token_int (char *s) {
  if (cur_token_len <= 0) {
    return NOT_FOUND;
  } else {
    char *end;
    long long x = strtoll (s, &end, 0);    
    if (*end) {
      return NOT_FOUND;
    } else {
      return x;
    }
  }
}

int hex2int (char c) {
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  assert (0);
  return 0;
}

char *print_permanent_msg_id (tdl_message_id_t id) {
  static char buf[2 * sizeof (tdl_message_id_t) + 1];
  
  unsigned char *s = (void *)&id;
  int i;
  for (i = 0; i < (int)sizeof (tdl_message_id_t); i++) {
    sprintf (buf + 2 * i, "%02x", (unsigned)s[i]);
  }
  return buf;
}

char *print_permanent_peer_id (tgl_peer_id_t id) {
  static char buf[2 * sizeof (tgl_peer_id_t) + 2];
  buf[0] = '$';
  
  unsigned char *s = (void *)&id;
  int i;
  for (i = 0; i < (int)sizeof (tgl_peer_id_t); i++) {
    sprintf (buf + 1 + 2 * i, "%02x", (unsigned)s[i]);
  }
  return buf;
}

struct tdl_chat_info *cur_token_peer (char *s, int mode, struct in_command *cmd);

tdl_message_id_t cur_token_msg_id (char *s, struct in_command *cmd) {
  static tdl_message_id_t res;
  res.message_id = -1;
  if (!s) {
    return res;
  }   
  
  char *t;

  int x = (int)strtol (s, &t, 10);

  if (*t == 0) {
    struct message_alias *A = convert_local_to_global (x);
    if (A) {
      res.chat_id = A->chat_id;
      res.message_id = A->message_id;
      return res;
    } else {
      return res;
    }
  }

  t = s + 1;
  while (*t && *t != '@') {
    t ++;
  }

  if (!*t) {
    return res;
  }

  char *tt;    
  x = (int)strtol (t + 1, &tt, 10);

  if (*tt) {
    return res;
  }

  char c = *t;
  *t = 0;

  struct tdl_chat_info *C = cur_token_peer (s, -1, cmd);
  *t = c;

  if (C == (void *)-1l) {
    res.message_id = -2;
    return res;
  }
  if (!C) {
    return res;
  }
  res.chat_id = C->id;
  res.message_id = x;
  return res;
}

double cur_token_double (char *s) {
  if (!s) {
    return NOT_FOUND;
  } else {
    char *end;
    double x = strtod (s, &end);
    if (*end) {
      return NOT_FOUND;
    } else {
      return x;
    }
  }
}

void proceed_with_query (struct delayed_query *q) {
  interpreter_ex (q->cmd);
  in_command_decref (q->cmd);

  if (q->token) {
    free (q->token);
  }
  free (q);
}

void stop_query (struct delayed_query *q) {
  fail_interface (TLS, q->cmd, TLS->error_code, "Fail to resolve chat: %s", TLS->error);
  in_command_decref (q->cmd);
  
  if (q->token) {
    free (q->token);
  }
  free (q);
}

void process_with_query (struct delayed_query *q, int success) {
  if (!success) {
    stop_query (q);
  } else {
    proceed_with_query (q);
  }
}

void process_with_query_resolve_chat (struct tdlib_state *TLSR, void *extra, int success, struct tdl_chat_info *C) {
  if (success && C) {
    on_chat_update (TLS, C);
  }
  process_with_query (extra, success);
}

struct tdl_chat_info *cur_token_peer (char *s, int mode, struct in_command *cmd) {
  if (!s) {
    return NULL;
  }

  if (*s == '@') {
    int i;
    for (i = 0; i < cur_token_len; i++) {
      if (s[i] >= 'A' && s[i] <= 'Z') {
        s[i] = (char)(s[i] + 'a' - 'A');
      }
    }
  } 

  struct chat_alias *A = get_by_alias (s);
  if (A) {
    if (A->type == -1) {
      struct tdl_chat_info *C = A->chat;
      if (mode >= 0 && C->chat->type != mode) {
        return NULL;
      } else {
        return A->chat;
      }
    } else if (!cmd) {
      return NULL;
    } else {
      if (mode >= 0 && A->type != mode) {
        return NULL;
      }
      struct delayed_query *q = calloc (sizeof (*q), 1);
      cmd->refcnt ++;
      q->cmd = cmd;
      q->action = 1;
      q->mode = mode;
      switch (A->type) {
      case tdl_chat_type_user:
        tdlib_create_private_chat (TLS, process_with_query_resolve_chat, q, ((struct tdl_user *)A->chat)->id);
        break;
      case tdl_chat_type_group:
        tdlib_create_group_chat (TLS, process_with_query_resolve_chat, q, ((struct tdl_group *)A->chat)->id);
        break;
      case tdl_chat_type_channel:
        tdlib_create_channel_chat (TLS, process_with_query_resolve_chat, q, ((struct tdl_channel *)A->chat)->id);
        break;
      /*case tdl_chat_type_secret_chat:
        tdlib_get_secret_chat (TLS, process_with_query_resolve_chat, q, ((struct tdl_secret_chat *)q->chat)->id;
        break;*/
      default:
        assert (0);
        return NULL;
      }
      return (void *)-1l;
    }
  }

  if (!cmd) {
    return NULL;
  }
  
  if (*s == '@' && cur_token_len >= 2) {
    struct delayed_query *q = calloc (sizeof (*q), 1);
    cmd->refcnt ++;
    q->cmd = cmd;
    q->action = 2;
    q->mode = mode;
      
    tdlib_search_public_chat (TLS, process_with_query_resolve_chat, q, q->token);
    return (void *)-1l;
  }

  char *f[3] = { "user#id", "group#id", "channel#id" };
  enum tdl_chat_type ff[3] = {tdl_chat_type_user, tdl_chat_type_group, tdl_chat_type_channel};

  int i;
  for (i = 0; i < 3; i++) {
    if (mode >= 0 && mode != ff[i]) { continue; }
    if (!memcmp (s, f[i], strlen (f[i]))) {
      int id = atoi (s + strlen (f[i]));
      if (id != 0) {
        struct delayed_query *q = calloc (sizeof (*q), 1);
        q->action = 4;
        cmd->refcnt ++;
        q->cmd = cmd;

        switch (ff[i]) {
        case tdl_chat_type_user:
          tdlib_create_private_chat (TLS, process_with_query_resolve_chat, q, id);
          break;
        case tdl_chat_type_group:
          tdlib_create_group_chat (TLS, process_with_query_resolve_chat, q, id);
          break;
        case tdl_chat_type_channel:
          tdlib_create_channel_chat (TLS, process_with_query_resolve_chat, q, id);
          break;
        default:
          in_command_decref (cmd);
          free (q);
          break;
        }
        return (void *)-1l;
      }
    }
  }
  
  return NULL;
}

char *get_default_prompt (void) {
  static char buf[1000];
  int l = 0;
  switch (conn_state) {
  case tdl_connection_unknown:
    l += sprintf (buf + l, "[?] ");
    break;
  case tdl_connection_wait_net:
    l += sprintf (buf + l, "[W] ");
    break;
  case tdl_connection_connecting:
    l += sprintf (buf + l, "[C] ");
    break;
  case tdl_connection_updating:
    l += sprintf (buf + l, "[U] ");
    break;
  case tdl_connection_ready:
    l += sprintf (buf + l, "[R] ");
    break;
  }
  l += snprintf (buf + l, 999 - l, "%d ", total_unread);
  if (TLS->cur_uploading_bytes || TLS->cur_downloading_bytes) {
    l += snprintf (buf + l, 999 - l, COLOR_RED "[");
    int ok = 0;
    if (TLS->cur_uploading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += snprintf (buf + l, 999 - l, "%lld%%Up", 100 * TLS->cur_uploaded_bytes / TLS->cur_uploading_bytes);
    }
    if (TLS->cur_downloading_bytes) {
      if (ok) { *(buf + l) = ' '; l ++; }
      ok = 1;
      l += snprintf (buf + l, 999 - l, "%lld%%Down", 100 * TLS->cur_downloaded_bytes / TLS->cur_downloading_bytes);
    }
    l += snprintf (buf + l, 999 - l, "]" COLOR_NORMAL);
    l += snprintf (buf + l, 999 - l, "%s", default_prompt);
    return buf;
  } 
  if (cur_chat_mode_chat) {
    l += snprintf (buf + l, 999 - l, "%.*s ", 100, cur_chat_mode_chat->title);
  }
  if (l > 0 && buf[l - 1] == ' ') {
    l--;
  }
  l += snprintf (buf + l, 999 - l, "%s", default_prompt);
  return buf;
}

char *complete_none (const char *text, int state) {
  return 0;
}


void set_prompt (const char *s) {
  if (readline_disabled) { return; }
  rl_set_prompt (s);
}

void update_prompt (void) {
  if (readline_disabled) {
    fflush (stdout);
    return;
  }
  if (read_one_string) { return; }
  print_start ();
  set_prompt (get_default_prompt ());
  if (readline_active) {
    rl_redisplay ();
  }
  print_end ();
}

char *modifiers[] = {
  "[offline]",
  "[enable_preview]",
  "[disable_preview]",
  "[html]",
  "[reply=",
  "[id=",
  0
};

char *in_chat_commands[] = {
  "/exit",
  "/quit",
  "/history",
  0
};

void print_string_gw (struct in_command *cmd, int success, struct res_arg *args);
void open_filename_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_filename_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_user_list_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_chat_members_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_msg_list_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_dialog_list_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_group_info_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_channel_info_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_user_info_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_peer_info_gw (struct in_command *cmd, int success, struct res_arg *args);

void print_channel_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_user_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_chat_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_peer_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_secret_chat_gw (struct in_command *cmd, int success, struct res_arg *args);

void print_msg_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_msg_success_gw (struct in_command *cmd, int success, struct res_arg *args);
void print_success_gw (struct in_command *cmd, int success, struct res_arg *args);

void print_invite_link_gw (struct in_command *cmd, int success, struct res_arg *args);

struct command commands[];

long long find_modifier (int arg_num, struct arg args[], const char *name, int type) {
  int i;
  int name_len = (int)strlen (name);
  for (i = 0; i < arg_num; i++) {
    char *s = args[i].str;
    int len = s ? (int)strlen (s) : 0;
    if (s && len >= 3 && s[0] == '[' && s[len - 1] == ']' && !memcmp (s + 1, name, name_len)) {
      if (type == 0) {
        if (len == name_len + 2) {
          return 1;
        }
      } else if (type == 1) {
        return atoll (s + name_len + 1);
      } else if (s[name_len + 1] == '=') {
        return atoll (s + name_len + 2);
      }
    }
  }
  return 0;
}

void free_res_arg (struct res_arg *A) {
  if (!A->flags) { return; }
  if (A->flags == 1) {
    free (A->str);
    return;
  }
  assert (A->flags == 2);
  int i;
  for (i = 0; i < A->vec_len; i++) {
    free_res_arg (&A->vec[i]);
  }
  free (A->vec);
}

void free_res_arg_list (struct res_arg *A, int size) {
  int i;
  for (i = 0; i < size; i++) {
    free_res_arg (&A[i]);
  }
}

void tdcli_empty_cb (struct tdlib_state *TLS, void *extra, int success) {
  if (!extra) { return; }
  struct in_command *cmd = extra;

  struct res_arg args[11];
  memset (args, 0, sizeof (args));

  cmd->cb (cmd, success, args);
  
  free_res_arg_list (args, 11);
    
  in_command_decref (cmd);
}

void tdcli_string_cb (struct tdlib_state *TLS, void *extra, int success, const char *str) {
  if (!extra) { return; }
  struct in_command *cmd = extra;

  struct res_arg args[11];
  memset (args, 0, sizeof (args));

  if (str) {
    args[0].flags = 1;
    args[0].str = strdup (str);
  }

  cmd->cb (cmd, success, args);
  
  free_res_arg_list (args, 11);
    
  in_command_decref (cmd);
}

void tdcli_ptr_cb (struct tdlib_state *TLS, void *extra, int success, void *M) { 
  if (!extra) { return; }
  struct in_command *cmd = extra; 
  struct res_arg args[11];
  memset (args, 0, sizeof (args));
  args[0].ptr = M; 
  cmd->cb (cmd, success, args); 
  free_res_arg_list (args, 11); 
    
  in_command_decref (cmd);
}

void tdcli_int_cb (struct tdlib_state *TLS, void *extra, int success, int M) { 
  if (!extra) { return; }
  struct in_command *cmd = extra; 
  struct res_arg args[11];
  memset (args, 0, sizeof (args));
  args[0].num = M; 
  cmd->cb (cmd, success, args); 
  free_res_arg_list (args, 11); 
    
  in_command_decref (cmd);
}

void tdcli_vec_ptr_cb (struct tdlib_state *TLS, void *extra, int success, int size, void **M) { 
  if (!extra) { return; }
  struct in_command *cmd = extra; 
  struct res_arg args[11];
  memset (args, 0, sizeof (args));
  args[0].flags = 2;
  args[0].vec_len = size;
  args[0].vec = calloc (sizeof (struct res_arg), size);
  int i;
  for (i = 0; i < size; i++) {
    args[0].vec[i].ptr = M[i];
  }
  cmd->cb (cmd, success, args); 
  free_res_arg_list (args, 11); 
    
  in_command_decref (cmd);
}

void tdcli_vec_ptr_total_cb (struct tdlib_state *TLS, void *extra, int success, int total, int size, void **M) { 
  if (!extra) { return; }
  struct in_command *cmd = extra; 
  struct res_arg args[11];
  memset (args, 0, sizeof (args));
  args[0].num = total;
  args[1].flags = 2;
  args[1].vec_len = size;
  args[1].vec = calloc (sizeof (struct res_arg), size);
  int i;
  for (i = 0; i < size; i++) {
    args[1].vec[i].ptr = M[i];
  }
  cmd->cb (cmd, success, args); 
  free_res_arg_list (args, 11); 
    
  in_command_decref (cmd);
}

/* {{{ client methods */

void on_timer_alarm (evutil_socket_t fd, short what, void *arg) {
  tdcli_empty_cb (TLS, arg, 1);
}

void do_timer (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  struct event *ev = evtimer_new (ev_base, on_timer_alarm, cmd);

  double x = args[2].dval;
  struct timeval tv;
  tv.tv_sec = (int)x;
  x -= (int)x;
  tv.tv_usec = (int)(x * 1000000);
  event_add (ev, &tv);
}

void do_help (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  #define BL (1 << 14)
  char s[BL];
  int p = 0;
  s[p] = 0;
  mprint_start (cmd->ev);
  int total = 0;
  mpush_color (cmd->ev, COLOR_YELLOW);
  struct command *cmd_it = commands;

  const char *command_name = args[2].str;
  
  int max_len = 0;
  while (cmd_it->name) {  
    if (!command_name || !strcmp (command_name, cmd_it->name)) {
      int len = (int)strlen (cmd_it->name);
      
      struct command_argument_desc *D = cmd_it->args;
      while (D->type) {
        int period = D->type & ca_period;
        len += 3 + (int)strlen (D->name) + (period ? 4 : 0);
        D ++;
      }

      if (len > max_len) {
        max_len = len;
      }
    }
    cmd_it ++;
  }

  max_len ++;
  
  cmd_it = commands;
  while (cmd_it->name) {
    if (!command_name || !strcmp (command_name, cmd_it->name)) {
      p += snprintf (s + p, BL - p, "%s", cmd_it->name);
      if (p > BL) { p = BL; }
      int len = (int)strlen (cmd_it->name);
      struct command_argument_desc *D = cmd_it->args;
      while (D->type) {
        int opt = D->type & ca_optional;
        int period = D->type & ca_period;
        p += snprintf (s + p, BL - p, " %s%s%s%s", opt ? "[" : "<", D->name, period ? " ..." : "", opt ? "]" : ">");
        if (p > BL) { p = BL; }
        len += 3 + (int)strlen (D->name) + (period ? 4 : 0);
        D ++;
      }
      while (len < max_len) {
        len ++;
        p += snprintf (s + p, BL - p, " ");
        if (p > BL) { p = BL; }
      }
      p += snprintf (s + p, BL - p, "%s\n", cmd_it->desc);
      if (p > BL) { p = BL; }
      total ++;
    }
    cmd_it ++;
  }
  if (!total) {
    p += snprintf (s + p, BL - p, "Unknown command '%s'\n", command_name);
    if (p > BL) { p = BL; }
  }

  cmd->refcnt ++;
  tdcli_string_cb (TLS, cmd, 1, s);
  #undef BL
}

void do_show_license (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  static char *b = 
#include "LICENSE.h"
  ;
  cmd->refcnt ++;
  tdcli_string_cb (TLS, cmd, 1, b);
}

void do_quit (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (daemonize) {
    event_incoming (cmd->ev->bev, BEV_EVENT_EOF, cmd->ev);
  }
  do_halt (0);
  
  cmd->refcnt ++;
  tdcli_empty_cb (TLS, cmd, 1);
}

void do_safe_quit (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (daemonize) {
    event_incoming (cmd->ev->bev, BEV_EVENT_EOF, cmd->ev);
  }
  safe_quit = 1;
  
  cmd->refcnt ++;
  tdcli_empty_cb (TLS, cmd, 1);
}

void do_set (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  const char *var = args[2].str;
  int value = (int)args[2].num;

  if (!strcmp (var, "debug_verbosity")) {
    tdlib_set_logger_verbosity (value);
  } else if (!strcmp (var, "log_level")) {
    log_level = value;
  } else if (!strcmp (var, "msg_num")) {
    msg_num_mode = value;
  } else if (!strcmp (var, "alert")) {
    alert_sound = value;
  }
  
  cmd->refcnt ++;
  tdcli_empty_cb (TLS, cmd, 1);
}

void do_chat_with_peer (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (!cmd->ev) {
    cur_chat_mode_chat = args[2].chat;
  }
  
  cmd->refcnt ++;
  tdcli_empty_cb (TLS, cmd, 1);
}

void do_main_session (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (notify_ev && !--notify_ev->refcnt) {
    free (notify_ev);
  }
  notify_ev = cmd->ev;
  if (cmd->ev) { cmd->ev->refcnt ++; }
  
  cmd->refcnt ++;
  tdcli_empty_cb (TLS, cmd, 1);
}

void do_version (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  static char *s = "Telegram-cli version " TELEGRAM_CLI_VERSION " (uses tdlib)";
  
  cmd->refcnt ++;
  tdcli_string_cb (TLS, cmd, 1, s);
}
/* }}} */

/* {{{ WORK WITH ACCOUNT */

void do_set_password (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  //assert (arg_num == 1);
  //tgl_do_set_password (TLS, ARG2STR_DEF(0, "empty"), print_success_gw, ev);
}
/* }}} */

void try_download_cb (struct tdlib_state *TLS, void *extra, int success) {
  if (!success) {
    struct file_wait *F = extra;
    struct file_wait_cb *cb = F->first_cb;
    while (cb) {
      cb->callback (TLS, cb->callback_extra, 0, NULL);
      struct file_wait_cb *n = cb->next;
      free (cb);
      cb = n;
    }
    file_wait_tree = tree_delete_file_wait (file_wait_tree, F);
    free (F);
    return;
  }
}

void do_load_file (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int id = (int)args[2].num;
  struct file_wait *F = tree_lookup_file_wait (file_wait_tree, (void *)&id);
  if (!F) {
    F = calloc (sizeof (*F), 1);
    F->id = id;
    file_wait_tree = tree_insert_file_wait (file_wait_tree, F, rand ());
  }
  struct file_wait_cb *cb = calloc (sizeof (*cb), 1);
  cb->callback = tdcli_string_cb;
  cmd->refcnt ++;
  cb->callback_extra = cmd;
  if (F->first_cb) {
    F->last_cb->next = cb;
    F->last_cb = cb;
  } else {
    F->last_cb = F->first_cb = cb;
    tdlib_download_file (TLS, try_download_cb, F, id);
  }
}

/* {{{ SENDING MESSAGES */

void do_msg (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id;
  int reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat->id;
    reply_id = (int)find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  char *text = args[3].str;
  int do_html = (int)find_modifier (args[0].vec_len, args[0].vec, "html", 0);

  int disable_preview = disable_msg_preview;
  if (disable_preview) {
    if (find_modifier (args[0].vec_len, args[0].vec, "enable_preview", 0)) {
      disable_preview = 0;
    }
  } else {
    if (find_modifier (args[0].vec_len, args[0].vec, "disable_preview", 0)) {
      disable_preview = 1;
    }
  }

  union tdl_input_message_content *content = tdlib_create_input_message_content_text (TLS, text, do_html, disable_preview, 1);
  tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, chat_id, reply_id, 0, 0, NULL, content);
}

void do_compose (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char tempname[128] = "/tmp/tdcli-XXXXXX";
  int fd = mkstemp (tempname);

  if (fd < 0) {
    fail_interface (TLS, cmd, EINVAL, "Can not open temp file %s", tempname);
    return;
  }

  pid_t r = fork ();

  if (!r) {
    logprintf ("tempname = %s\n", tempname);
    if (execl ("/usr/bin/vim", "/usr/bin/vim", tempname, NULL) < 0) {
      perror ("execl");
      exit (73);
    }
  } else {
    int status;
    waitpid (r, &status, 0);
    logprintf ("status = %d\n", WEXITSTATUS (status));
  }

  char buf[1 << 17];
  int l = (int)pread (fd, buf, (1 << 17), 0);
  close (fd);
  unlink (tempname);

  if (l < 0) {
    fail_interface (TLS, cmd, EINVAL, "Can not read temp file %s", tempname);
    return;
  }

  args[3].flags = 1;
  args[3].str = strndup (buf, l);

  do_msg (command, arg_num, args, cmd);
}

void do_send_file (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id;
  int reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;    
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat->id;
    reply_id = (int)find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  char *media_type = args[3].str;
  enum tdl_media_type type;
  if (media_type && strlen (media_type)) {
    if (!strcmp (media_type, "animation")) {
      type = tdl_media_animation;
    } else if (!strcmp (media_type, "audio")) {
      type = tdl_media_audio;
    } else if (!strcmp (media_type, "document")) {
      type = tdl_media_document;
    } else if (!strcmp (media_type, "photo")) {
      type = tdl_media_photo;
    } else if (!strcmp (media_type, "sticker")) {
      type = tdl_media_sticker;
    } else if (!strcmp (media_type, "video")) {
      type = tdl_media_video;
    } else if (!strcmp (media_type, "voice")) {
      type = tdl_media_voice;
    } else {
      fail_interface (TLS, cmd, EINVAL, "Unknown media type");
      return;
    }
  } else {
    type = tdl_media_document;
  }
  
  char *file_name = args[4].str;
  char *caption = args[5].str;

  union tdl_input_file *f = tdlib_create_input_file_local (TLS, file_name);
  union tdl_input_message_content *content = tdlib_create_input_message_content_media (TLS, type, 0, 0, 0, NULL, caption, NULL, NULL, 0, f);
  tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, chat_id, reply_id, 0, 0, NULL, content);
}

void do_send_location (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id;
  int reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat->id;
    reply_id = (int)find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  double longitude = args[3].dval;
  double latitude = args[4].dval;

  union tdl_input_message_content *content = tdlib_create_input_message_content_venue (TLS, longitude, latitude, NULL, NULL, NULL, NULL);
  tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, chat_id, reply_id, 0, 0, NULL, content);
}

void do_send_contact (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id;
  int reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;    
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat->id;
    reply_id = (int)find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  char *phone = args[3].str;
  char *first_name = args[4].str;
  char *last_name = args[5].str;

  union tdl_input_message_content *content = tdlib_create_input_message_content_contact (TLS, phone, first_name, last_name, 0);
  tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, chat_id, reply_id, 0, 0, NULL, content);
}

void do_fwd (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id;
  int reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat->id;
    reply_id = (int)find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  long long from_chat_id = args[3].msg_id.chat_id;
  int msg_id = args[3].msg_id.message_id;
  
  union tdl_input_message_content *content = tdlib_create_input_message_content_forward (TLS, from_chat_id, msg_id);
  tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, chat_id, reply_id, 0, 0, NULL, content);
}

/* }}} */

/* {{{ EDITING SELF PROFILE */

void do_change_profile_photo (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  char *file_name = args[2].str;
  tdlib_set_profile_photo (TLS, tdcli_empty_cb, cmd, file_name,  NULL);
}

void do_change_profile_name (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  char *first_name = args[2].str;
  char *last_name = args[3].str;
  tdlib_change_name (TLS, tdcli_empty_cb, cmd, first_name, last_name);
}

void do_change_username (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  char *username = args[2].str;
  tdlib_change_username (TLS, tdcli_empty_cb, cmd, username);
}

/* }}} */

/* {{{ WORKING WITH GROUP CHATS */

void do_mute (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  union tdl_notification_settings_scope *scope = tdlib_create_notification_settings_scope (TLS, tdl_notification_settings_scope_chat, args[2].chat->id);
  
  int mute_for = (command->params[0]) ? (int)(args[3].num == NOT_FOUND ? 3600 : args[3].num) : 0;
  struct tdl_chat_info *C = args[2].chat;
  struct tdl_notification_settings *settings = tdlib_create_notification_settings (TLS, mute_for, C->notification_settings ? C->notification_settings->sound : "default", 0);
  
  tdlib_set_notification_settings (TLS, tdcli_empty_cb, cmd, scope, settings);
}

void do_chat_change_photo (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  long long chat_id = args[2].chat->id;
  char *file_name = args[3].str;
  tdlib_change_chat_photo (TLS, tdcli_empty_cb, cmd, chat_id, tdlib_create_input_file_local (TLS, file_name),  NULL);
}

void do_chat_change_title (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  long long chat_id = args[2].chat->id;
  char *title = args[3].str;
  tdlib_change_chat_title (TLS, tdcli_empty_cb, cmd, chat_id, title);
}

void do_chat_info (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  struct tdl_chat_info *C = args[2].chat;

  switch (C->chat->type) {
  case tdl_chat_type_user:
    tdlib_get_user_full (TLS, (void *)tdcli_ptr_cb, cmd, C->chat->user.id);
    break;
  case tdl_chat_type_group:
    tdlib_get_group_full (TLS, (void *)tdcli_ptr_cb, cmd, C->chat->group.id);
    break;
  case tdl_chat_type_channel:
    tdlib_get_channel_full (TLS, (void *)tdcli_ptr_cb, cmd, C->chat->channel.id);
    break;
  case tdl_chat_type_secret_chat:
    break;
  }
}

void do_chat_change_role (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  enum tdl_chat_member_role role;

  char *role_str = args[4].str;
  if (!strcmp (role_str, "creator")) {
    role = tdl_chat_member_role_creator;
  } else if (!strcmp (role_str, "editor")) {
    role = tdl_chat_member_role_editor;
  } else if (!strcmp (role_str, "moderator")) {
    role = tdl_chat_member_role_moderator;
  } else if (!strcmp (role_str, "general")) {
    role = tdl_chat_member_role_general;
  } else if (!strcmp (role_str, "kicked")) {
    role = tdl_chat_member_role_kicked;
  } else {
    fail_interface (TLS, cmd, EINVAL, "Unknown member role");
    return;
  }
  
  long long chat_id = args[2].chat->id;
  int user_id = args[3].chat->chat->user.id;

  cmd->refcnt ++;
  tdlib_chat_change_member_role (TLS, tdcli_empty_cb, cmd, chat_id, user_id, role);
}

void do_chat_add_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  long long chat_id = args[2].chat->id;
  int user_id = args[3].chat->chat->user.id;
  int fwd_msg_num = (args[4].num == NOT_FOUND) ? 0 : (int)args[4].num;

  tdlib_chat_add_member (TLS, tdcli_empty_cb,cmd, chat_id, user_id, fwd_msg_num);
}

void do_chat_del_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id = args[2].chat->id;
  int user_id = args[3].chat->chat->user.id;
  
  tdlib_chat_change_member_role (TLS, tdcli_empty_cb, cmd, chat_id, user_id, tdl_chat_member_role_kicked);
}

void do_chat_join (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id = args[2].chat->id;
  
  tdlib_chat_add_member (TLS, tdcli_empty_cb, cmd, chat_id, TLS->my_id, 0);
}

void do_chat_leave (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id = args[2].chat->id;
  
  tdlib_chat_change_member_role (TLS, tdcli_empty_cb, cmd, chat_id, TLS->my_id, tdl_chat_member_role_kicked);
}
    
void do_group_create (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *title = args[2].str;
  assert (args[3].vec_len <= 1000);
  
  int ids[1000];
  
  int i;
  for (i = 0; i < args[2].vec_len; i++) {
    ids[i] = args[3].vec[i].chat->chat->user.id;
  }

  cmd->refcnt ++;
  tdlib_create_new_group_chat (TLS, (void *)tdcli_ptr_cb, cmd, title, args[2].vec_len, ids);
}
    
void do_channel_create (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  char *title = args[2].str;
  char *about = args[3].str;
  
  tdlib_create_new_channel_chat (TLS, (void *)tdcli_ptr_cb, cmd, title, (int)command->params[0], (int)command->params[1], about);
}

void do_chat_export_link (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id = args[2].chat->id;
  
  tdlib_export_chat_invite_link (TLS, tdcli_string_cb, cmd, chat_id);
}

void do_chat_import_link (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  char *link = args[2].str;

  tdlib_import_chat_invite_link (TLS, tdcli_empty_cb, cmd, link);
}

void do_chat_check_link (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  char *link = args[2].str;

  tdlib_check_chat_invite_link (TLS, (void *)tdcli_ptr_cb, cmd, link);
}

void do_channel_get_members (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  int channel_id = args[2].chat->chat->channel.id;
  int limit = (args[3].num == NOT_FOUND) ? 0 : (int)args[3].num;
  int offset = (args[4].num == NOT_FOUND) ? 0 : (int)args[4].num;

  tdlib_get_channel_members (TLS, (void *)tdcli_vec_ptr_total_cb, cmd, channel_id, (int)command->params[0], offset, limit);
}

void do_group_upgrade (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  long long chat_id = args[2].chat->id;

  tdlib_migrate_group_to_channel (TLS, (void *)tdcli_ptr_cb, cmd, chat_id);
}


/* }}} */

/* {{{ WORKING WITH USERS */

void do_add_contact (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  char *phone = args[2].str;
  char *first_name = args[3].str;
  char *last_name = args[4].str;

  struct tdl_input_contact *C = tdlib_create_input_contact (TLS, phone, first_name, last_name);
  tdlib_import_contacts (TLS, (void *)tdcli_vec_ptr_cb, cmd, 1, &C);
}


void do_block_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  int user_id = args[2].chat->chat->user.id;

  tdlib_block_user (TLS, tdcli_empty_cb, cmd, user_id);
}

void do_unblock_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  int user_id = args[2].chat->chat->user.id;

  tdlib_unblock_user (TLS, tdcli_empty_cb, cmd, user_id);
}
/* }}} */

/* WORKING WITH CHANNELS {{{ */

void do_channel_change_about (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  int channel_id = args[2].chat->chat->channel.id;
  char *about = args[3].str;

  tdlib_change_channel_about (TLS, tdcli_empty_cb, cmd, channel_id, about);
}

void do_channel_change_username (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  int channel_id = args[2].chat->chat->channel.id;
  char *username = args[3].str;
  if (*username == '@') { username ++; }

  tdlib_change_channel_username (TLS, tdcli_empty_cb, cmd, channel_id, username);
}

void do_channel_edit (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *enabled = args[4].str;
  int mode;
  if (!strcmp (enabled, "yes")) {
    mode = 1;
  } else if (!strcmp (enabled, "no")) {
    mode = 0;
  } else {
    fail_interface (TLS, cmd, EINVAL, "yes/no expected as third argument");
    return;
  }
  
  cmd->refcnt ++;

  int channel_id = args[2].chat->chat->channel.id;
  char *scope = args[3].str;

  if (!strcmp (scope, "invites")) {
    tdlib_toggle_channel_invites (TLS, tdcli_empty_cb, cmd, channel_id, mode);
  } else if (!strcmp (args[1].str, "sign")) {
    tdlib_toggle_channel_sign_messages (TLS, tdcli_empty_cb, cmd, channel_id, mode);
  } else {
    in_command_decref (cmd);
    fail_interface (TLS, cmd, EINVAL, "invites/sign expected as second argument");
    return;
  }
}

/* }}} */

/* {{{ WORKING WITH DIALOG LIST */

void do_dialog_list (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  int limit = (args[2].num == NOT_FOUND) ? 10 : (int)args[2].num;
  int offset = (args[3].num == NOT_FOUND) ? 0 : (int)args[3].num;
  
  tdlib_get_chats (TLS, (void *)tdcli_vec_ptr_cb, cmd, (1ull << 63) - 1 - offset, 0, limit);
}

void do_resolve_username (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  void **T = malloc (sizeof (void *) * 2);
  T[0] = cmd;
  char *u = args[2].str;
  if (*u == '@') { u ++; }
  tdlib_search_public_chat (TLS, (void *)tdcli_ptr_cb, T, u);
}

void do_contact_list (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  tdlib_get_contacts (TLS, (void *)tdcli_vec_ptr_cb, cmd);
}

void do_contact_delete (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  int user_id = args[2].chat->chat->user.id;

  tdlib_delete_contacts (TLS, tdcli_empty_cb, cmd, 1, &user_id);
}

/* }}} */

/* {{{ WORKING WITH ONE DIALOG */

void do_mark_read (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id = args[2].chat->id;
  int id = args[2].chat->top_message ? args[2].chat->top_message->id : 0;
   
  tdlib_view_messages (TLS, tdcli_empty_cb, cmd, chat_id, 1, &id);
}

struct chat_history_extra {
  struct in_command *cmd;
  int current_size;
  int current_pos;
  int current_offset;
  int limit;
  int last_msg_id;
  long long chat_id;
  struct tdl_message **list;
};

void do_send_history_query (struct chat_history_extra *e);

void received_chat_history_slice (struct tdlib_state *TLS, void *extra, int success, int cnt, struct tdl_message **list) {
  struct chat_history_extra *e = extra;
  assert (cnt + e->current_pos <= e->limit);  
  if (cnt == 0 || !success) {
    if (success || e->current_pos) {
      tdcli_vec_ptr_cb (TLS, e->cmd, 1, e->current_pos, (void **)e->list);
    } else {
      tdcli_vec_ptr_cb (TLS, e->cmd, 0, 0, NULL);
    }
  
    if (e->current_pos > 0) {
      int id = e->list[0]->id;
      tdlib_view_messages (TLS, tdcli_empty_cb, NULL, e->chat_id, 1, &id);
    }

    int i;
    for (i = 0; i < e->current_pos; i++) {
      tdlib_free_message (TLS, e->list[i]);
    }
    free (e->list);
    free (e);
    return;
  }
  if (cnt + e->current_pos > e->current_size) {
    e->list = realloc (e->list, sizeof (void *) * (cnt + e->current_pos));
    e->current_size = cnt + e->current_pos;
  }
  int i;
  for (i = 0; i < cnt; i++) {
    e->list[e->current_pos + i] = list[i];
    e->list[e->current_pos + i]->refcnt ++; 
  }
  e->current_pos += cnt;

  if (e->current_pos >= e->limit) {
    tdcli_vec_ptr_cb (TLS, e->cmd, 1, e->current_pos, (void **)e->list);
  
    if (e->current_pos > 0) {
      int id = e->list[0]->id;
      tdlib_view_messages (TLS, tdcli_empty_cb, NULL, e->chat_id, 1, &id);
    }
    int i;
    for (i = 0; i < e->current_pos; i++) {
      tdlib_free_message (TLS, e->list[i]);
    }
    free (e->list);
    free (e);
    return;
  }
  
  if (cnt > 0) {
    e->last_msg_id = list[cnt - 1]->id;
    e->current_offset = 0;
  }

  do_send_history_query (e);
}

void do_send_history_query (struct chat_history_extra *e) {
  int p = e->limit - e->current_pos;
  if (p > 100) { p = 100; }
  assert (p >= 0);
  tdlib_get_chat_history (TLS, received_chat_history_slice, e, e->chat_id, e->last_msg_id, e->current_offset, p); 
}

void do_history (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  struct chat_history_extra *e = malloc (sizeof (*e));
  e->cmd = cmd;
  e->current_pos = 0;
  e->current_size = 0;
  e->current_offset = args[4].num != NOT_FOUND ? (int)args[4].num : 0;
  e->limit = args[3].num != NOT_FOUND ? (int)args[3].num : 40;
  e->last_msg_id = 0;
  e->chat_id = args[2].chat->id;
  e->list = NULL;
  
  do_send_history_query (e);
}

void do_send_typing (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  enum tdl_message_typing_action status;

  char *typing = args[3].str;
  if (!typing || !strcmp (typing, "typing")) {
    status = tdl_message_typing_action_typing;
  } else if (!strcmp (typing, "cancel")) {
    status = tdl_message_typing_action_cancel;
  } else if (!strcmp (typing, "record_video")) {
    status = tdl_message_typing_action_record_video;
  } else if (!strcmp (typing, "upload_video")) {
    status = tdl_message_typing_action_upload_video;
  } else if (!strcmp (typing, "record_voice")) {
    status = tdl_message_typing_action_record_voice;
  } else if (!strcmp (typing, "upload_voice")) {
    status = tdl_message_typing_action_upload_voice;
  } else if (!strcmp (typing, "upload_photo")) {
    status = tdl_message_typing_action_upload_photo;
  } else if (!strcmp (typing, "upload_document")) {
    status = tdl_message_typing_action_upload_document;
  } else if (!strcmp (typing, "choose_location")) {
    status = tdl_message_typing_action_send_location;
  } else if (!strcmp (typing, "choose_contact")) {
    status = tdl_message_typing_action_choose_contact;
  } else {
    fail_interface (TLS, cmd, ENOSYS, "illegal typing status");
    return;
  }

  cmd->refcnt ++;

  long long chat_id = args[2].chat->id;

  switch (status) {
  case tdl_message_typing_action_typing:
  case tdl_message_typing_action_cancel:
  case tdl_message_typing_action_record_video:
  case tdl_message_typing_action_record_voice:
  case tdl_message_typing_action_send_location:
  case tdl_message_typing_action_choose_contact:
    {
      union tdl_user_action *U = tdlib_create_user_typing_action_simple (TLS, status);
      tdlib_send_chat_action (TLS, tdcli_empty_cb, cmd, chat_id, U);
    }
    break;
  case tdl_message_typing_action_upload_video:
  case tdl_message_typing_action_upload_voice:
  case tdl_message_typing_action_upload_photo:
  case tdl_message_typing_action_upload_document:
    {
      int progress = args[4].num == NOT_FOUND ? 0 : (int)args[4].num;
      union tdl_user_action *U = tdlib_create_user_typing_action_upload (TLS, status, progress);
      tdlib_send_chat_action (TLS, tdcli_empty_cb, cmd, chat_id, U);
    }
    break;
  }
}

/* }}} */


/* {{{ ANOTHER MESSAGES FUNCTIONS */

void do_search (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int limit = (args[3].num == NOT_FOUND) ? 40 : (int)args[3].num;
  int from = (args[4].num == NOT_FOUND) ? 0 : (int)args[4].num;
  
  cmd->refcnt ++;
  long long chat_id = args[2].chat ? args[2].chat->id : 0;
  char *query = args[7].str;

  if (chat_id) {
    tdlib_search_chat_messages (TLS, (void *)tdcli_vec_ptr_cb, cmd, chat_id, query, from, limit, 0, tdl_search_messages_filter_empty);
  } else {
    tdlib_search_messages (TLS, (void *)tdcli_vec_ptr_cb, cmd, query, 0, 0, 0, limit);
  }
}

void do_delete_msg (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;

  long long chat_id = args[2].msg_id.chat_id;
  int msg_id = args[2].msg_id.message_id;

  tdlib_delete_messages (TLS, tdcli_empty_cb, cmd, chat_id, 1, &msg_id);
}

void do_get_message (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  long long chat_id = args[2].msg_id.chat_id;
  int msg_id = args[2].msg_id.message_id;

  tdlib_get_message (TLS, (void *)tdcli_ptr_cb, cmd, chat_id, msg_id);
}

/* }}} */

/* {{{ BOT */

void do_start_bot (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  cmd->refcnt ++;
  
  int user_id = args[2].chat->chat->user.id;
  long long chat_id = args[3].chat->id;
  char *token = args[4].str;

  tdlib_send_bot_start_message (TLS, (void *)tdcli_ptr_cb, cmd, user_id, chat_id, token);
}
/* }}} */

extern char *default_username;
extern char *config_filename;
extern char *prefix;
extern char *auth_file_name;
extern char *state_file_name;
extern char *secret_chat_file_name;
extern char *downloads_directory;
extern char *config_directory;
extern char *binlog_file_name;
extern char *lua_file;
extern char *python_file;
extern struct event *term_ev;

void do_clear (struct command *command, int arg_num, struct arg args[], struct in_ev *ev) {
  logprintf ("Do_clear\n");
  
  /*free (default_username);
  tfree_str (config_filename);
  //free (prefix);
  tfree_str (auth_file_name);
  tfree_str (state_file_name);
  tfree_str (secret_chat_file_name);
  tfree_str (downloads_directory);
  //tfree_str (config_directory);
  tfree_str (binlog_file_name);
  tfree_str (lua_file);
  tfree_str (python_file);
  if (home_directory) {
    tfree_str (home_directory);
  }
  clear_history ();
  event_free (term_ev);
  struct event_base *ev_base = TLS->ev_base;
  tgl_free_all (TLS);
  event_base_free (ev_base);
  logprintf ("Bytes left allocated: %lld\n", tgl_get_allocated_bytes ());
  do_halt (0);*/
}


#define MAX_COMMANDS_SIZE 1000
struct command commands[MAX_COMMANDS_SIZE] = {
  //{"accept_secret_chat", {ca_secret_chat, ca_none}, do_accept_secret_chat, "accept_secret_chat <secret chat>\tAccepts secret chat. Only useful with -E option", NULL},
  {"account_change_username", {{"username", ca_string}}, {}, do_change_username, print_success_gw, "Sets profile username", NULL, {}},
  {"account_change_name", {{"first_name", ca_string}, {"last_name", ca_string}}, {}, do_change_profile_name, print_success_gw, "Sets profile name", NULL, {}},
  {"account_change_photo", {{"file", ca_file_name_end}}, {}, do_change_profile_photo, print_success_gw, "Sets profile photo. Photo will be cropped to square", NULL, {}},

  {"add_contact", {{"phone", ca_string}, {"first_name", ca_string}, {"last_name", ca_string}}, {{"users", ra_vector | ra_user}}, do_add_contact, print_user_list_gw, "Tries to add user to contact list", NULL, {}},
  {"block_user", {{"user", ca_user}}, {}, do_block_user, print_success_gw, "Blocks user", NULL, {}},
  //{"broadcast", {ca_user, ca_period, ca_string_end, ca_none}, do_broadcast, "broadcast <user>+ <text>\tSends text to several users at once", NULL},
  
  {"channel_get_admins", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel admins", NULL, {tdl_channel_members_filter_admins}},
  {"channel_get_bots", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel bots", NULL, {tdl_channel_members_filter_bots}},
  {"channel_get_kicked", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel kicked members", NULL, {tdl_channel_members_filter_kicked}},
  {"channel_get_members", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel recent members", NULL, {tdl_channel_members_filter_recent}},
  {"channel_change_about", {{"channel", ca_channel}, {"about", ca_string_end}}, {}, do_channel_change_about, print_success_gw, "Changes channel about info", NULL, {}},
  {"channel_change_username", {{"channel", ca_channel}, {"username", ca_string}}, {}, do_channel_change_username, print_success_gw, "Changes channel username", NULL, {}},
  {"channel_edit", {{"channel", ca_channel}, {"param", ca_string}, {"enabled", ca_string}}, {}, do_channel_edit, print_success_gw, "changes value of basic channel parameters. param=sign|invites. enabled=yes|no", NULL, {}}, 
  
  {"chat_add_user", {{"chat", ca_chat}, {"user", ca_user}, {"msgs_to_forward", ca_number | ca_optional}}, {}, do_chat_add_user, print_success_gw, "Adds user to chat. Sends him last msgs_to_forward messages (only for group chats) from this chat", NULL, {}},  
  {"chat_change_photo", {{"chat", ca_chat}, {"file", ca_file_name_end}}, {}, do_chat_change_photo, print_success_gw, "Changes chat photo. Photo will be cropped to square", NULL, {}},  
  {"chat_change_title", {{"chat", ca_chat}, {"title", ca_string_end}}, {}, do_chat_change_title, print_success_gw, "Renames chat", NULL, {}},
  {"chat_change_role", {{"chat", ca_chat}, {"user", ca_user}, {"role", ca_string}}, {}, do_chat_change_role, print_success_gw, "changes user's role in chat. role=creator|moderator|editor|general|kicked", NULL, {}},
  {"chat_del_user", {{"chat", ca_chat}, {"user", ca_user}}, {}, do_chat_del_user, print_success_gw, "Deletes user from chat", NULL, {}},
  {"chat_info", {{"chat", ca_chat}}, {{"chat", ra_peer}}, do_chat_info, print_peer_info_gw, "Prints info about chat", NULL, {}},
  {"chat_join", {{"chat", ca_chat}}, {}, do_chat_join, print_success_gw, "Joins to chat", NULL, {}},
  {"chat_leave", {{"chat", ca_chat}}, {}, do_chat_leave, print_success_gw, "Leaves chat", NULL, {}},
  {"chat_check_invite_link", {{"link", ca_string}}, {{"info", ra_invite_link_info}}, do_chat_check_link, print_invite_link_gw, "Print info about chat by link", NULL, {}}, 
  {"chat_create_broadcast", {{"title", ca_string}, {"about", ca_string}}, {{"chat", ra_chat}}, do_channel_create, print_chat_gw, "Creates broadcast channel", NULL, {1, 0}},
  {"chat_create_group", {{"title", ca_string}, {"members", ca_user | ca_period}}, {{"chat", ra_chat}}, do_group_create, print_chat_gw, "Creates group chat", NULL, {}},
  {"chat_create_supergroup", {{"title", ca_string}, {"about", ca_string}}, {{"chat", ra_chat}}, do_channel_create, print_chat_gw, "Creates supergroup channel", NULL, {0, 1}},
  {"chat_export_invite_link", {{"chat", ca_chat}}, {{"link", ra_string}}, do_chat_export_link, print_string_gw, "Exports new invite link (and invalidates previous)", NULL, {}}, 
  {"chat_import_invite_link", {{"link", ca_string}}, {}, do_chat_import_link, print_success_gw, "Get chat by invite link and joins if possible", NULL, {}}, 
  
  {"chat_with_peer", {{"chat", ca_chat}}, {}, do_chat_with_peer, print_success_gw, "Interface option. All input will be treated as messages to this peer. Type /quit to end this mode", NULL, {}},
  
  {"compose", {{"chat", ca_chat}}, {{"message", ra_message}}, do_compose, print_msg_success_gw, "Sends text message to peer", NULL, {0}},
  {"compose_reply", {{"chat", ca_msg_id}}, {{"message", ra_message}}, do_compose, print_msg_success_gw, "Sends text message to peer", NULL, {1}},
  
  {"contact_list", {}, {{"users", ra_vector | ra_user}}, do_contact_list, print_user_list_gw, "Prints contact list", NULL, {}},
  {"contact_delete", {{"user", ca_user}}, {}, do_contact_delete, print_success_gw, "Deletes user from contact list", NULL, {}},

  {"delete_msg", {{"msg_id", ca_msg_id}}, {}, do_delete_msg, print_success_gw, "Deletes message", NULL, {}},

  {"dialog_list", {{"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"dialogs", ra_vector | ra_chat}}, do_dialog_list, print_dialog_list_gw, "List of last conversations", NULL, {}},
  
  {"fwd", {{"chat", ca_chat}, {"msg_id", ca_msg_id}}, {{"message", ra_message}}, do_fwd, print_msg_success_gw, "Forwards message to peer. Forward to secret chats is forbidden", NULL, {0, 0}},
  
  //{"get_terms_of_service", {ca_none}, do_get_terms_of_service, "get_terms_of_service\tPrints telegram's terms of service", NULL},
  
  {"get_message", {{"msg_id", ca_msg_id}}, {{"message", ra_message}}, do_get_message, print_msg_gw, "Get message by id", NULL, {}},
  //{"get_self", {ca_none}, do_get_self, "get_self \tGet our user info", NULL},
  {"group_upgrade", {{"group", ca_group}}, {{"chat", ra_chat}}, do_group_upgrade, print_chat_gw, "Upgrades group to supergroup", NULL, {}},
  
  {"help", {{"command_name", ca_command | ca_optional}}, {{"text", ra_string}}, do_help, print_string_gw, "Prints this help", NULL, {}},
  
  {"history", {{"chat", ca_chat}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"messages", ra_message | ra_vector}}, do_history, print_msg_list_gw, "Prints messages with this peer. Also marks messages as read", NULL, {}},
  
  {"load_file", {{"file_id", ca_number}}, {{"file", ra_string}}, do_load_file, print_filename_gw, "Downloads file to downloads dirs. Prints file name after download end", NULL, {0}},
  
  {"main_session", {}, {}, do_main_session, print_success_gw, "Sends updates to this connection (or terminal). Useful only with listening socket", NULL, {}},
  {"mark_read", {{"chat", ca_chat}}, {}, do_mark_read, print_success_gw, "Marks messages with peer as read", NULL, {}},
  {"msg", {{"chat", ca_chat}, {"text", ca_msg_string_end}}, {{"message", ra_message}}, do_msg, print_msg_success_gw, "Sends text message to peer", NULL, {0}},
  {"mute", {{"chat", ca_chat}, {"mute_for", ca_number}}, {}, do_mute, print_success_gw, "mutes chat for specified number of seconds (default 60)", NULL, {1}},
  

  {"resolve_username", {{"username", ca_string}}, {{"chat", ra_chat}}, do_resolve_username, print_chat_gw, "Find chat by username", NULL, {}},
  
  {"reply", {{"msg_id", ca_msg_id}, {"text", ca_msg_string_end}}, {{"message", ra_message}}, do_msg, print_msg_success_gw, "Sends text message to peer", NULL, {1}},  
  {"reply_file", {{"msg_id", ca_msg_id}, {"type", ca_media_type | ca_optional}, {"file", ca_file_name}, {"caption", ca_string_end | ca_optional}}, {{"message", ra_message}}, do_send_file, print_msg_success_gw, "Replies to peer with file", NULL, {1}},
  {"reply_fwd", {{"msg_id", ca_msg_id}, {"fwd_id", ca_msg_id}}, {{"message", ra_message}}, do_fwd, print_msg_success_gw, "Forwards message to peer. Forward to secret chats is forbidden", NULL, {1}},
  {"reply_location", {{"msg_id", ca_msg_id}, {"longitude", ca_double}, {"latitude", ca_double}}, {{"message", ra_message}}, do_send_location, print_msg_success_gw, "Sends geo location", NULL, {1}},  
  
  {"search", {{"chat", ca_chat | ca_optional}, {"limit", ca_number | ca_optional}, {"from", ca_number | ca_optional}, {"to", ca_number | ca_optional}, {"offset", ca_number | ca_optional}, {"query", ca_string_end}}, {{"messages", ra_message | ra_vector}}, do_search, print_msg_list_gw, "Search for pattern in messages from date from to date to (unixtime) in messages with peer (if peer not present, in all messages)", NULL, {}},
  
  {"send_file", {{"chat", ca_chat}, {"type", ca_media_type | ca_optional}, {"file", ca_file_name}, {"caption", ca_string_end | ca_optional}}, {{"message", ra_message}}, do_send_file, print_msg_success_gw, "Sends file to peer", NULL, {0}},
  {"send_location", {{"chat", ca_chat}, {"longitude", ca_double}, {"latitude", ca_double}}, {{"message", ra_message}}, do_send_location, print_msg_success_gw, "Sends geo location", NULL, {0}},
  {"send_typing", {{"chat", ca_chat}, {"action", ca_string | ca_optional}, {"progress", ca_number | ca_optional}}, {}, do_send_typing, print_success_gw, "Sends typing action action=[typing|cancel|record_video|upload_video|record_voice|upload_voice|upload_photo|upload_document|choose_location|choose_contact]", NULL, {}},
  
  {"show_license", {}, {{"text", ra_string}}, do_show_license, print_string_gw, "Prints contents of GPL license", NULL, {}},
  
  {"start_bot", {{"user", ca_user}, {"chat", ca_chat}, {"data", ca_string}}, {{"message", ra_message}}, do_start_bot, print_msg_success_gw, "Adds bot to chat", NULL, {}},

  { "timer", {{"timeout", ca_double}}, {}, do_timer, print_success_gw, "sets timer (in seconds)", NULL, {}},

  {"unblock_user", {{"user", ca_user}}, {}, do_unblock_user, print_success_gw, "Unblocks user", NULL, {}},
  {"unmute", {{"chat", ca_chat}}, {}, do_mute, print_success_gw, "unmutes chat", NULL, {0}},

  {"version", {}, {{"text", ra_string}}, do_version, print_string_gw, "Prints client and library version", NULL, {}},

  {"view_file", {{"file_id", ca_number}}, {{"file", ra_string}}, do_load_file, open_filename_gw, "Downloads file to downloads dir. Then tries to open it with system default action", NULL, {1}},
 



  //{"clear", {ca_none}, do_clear, "clear\tClears all data and exits. For debug.", NULL},
  
  //{"fwd_media", {ca_chat, ca_msg_id, ca_none}, do_fwd_media, "fwd_media <peer> <msg-id>\tForwards message media to peer. Forward to secret chats is forbidden. Result slightly differs from fwd", NULL},
  //{"import_card", {ca_string, ca_none}, do_import_card, "import_card <card>\tGets user by card and prints it name. You can then send messages to him as usual", NULL},
  //{"msg_kbd", {ca_chat, ca_string, ca_msg_string_end, ca_none}, do_msg_kbd, "msg <peer> <kbd> <text>\tSends text message to peer with custom kbd", NULL},
  {"quit", {}, {}, do_quit, print_success_gw, "Quits immediately", NULL, {}},
  //{"rename_contact", {ca_user, ca_string, ca_string, ca_none}, do_rename_contact, "rename_contact <user> <first name> <last name>\tRenames contact", NULL},
  {"safe_quit", {}, {}, do_safe_quit, print_success_gw, "Waits for all queries to end, then quits", NULL, {}},
  //{"secret_chat_rekey", { ca_secret_chat, ca_none}, do_secret_chat_rekey, "generate new key for active secret chat", NULL},
  {"set", {{"variable", ca_string}, {"value", ca_number}}, {}, do_set, print_success_gw, "Sets value of param. Currently available: log_level, debug_verbosity, alarm, msg_num", NULL, {}},
  //{"set_password", {ca_string | ca_optional, ca_none}, do_set_password, "set_password <hint>\tSets password", NULL},
  //{"set_ttl", {ca_secret_chat, ca_number,  ca_none}, do_set_ttl, "set_ttl <secret chat>\tSets secret chat ttl. Client itself ignores ttl", NULL},
  //{"set_phone_number", {ca_string, ca_none}, do_set_phone_number, "set_phone_number <phone>\tChanges the phone number of this account", NULL},
  //{"stats", {ca_none}, do_stats, "stats\tFor debug purpose", NULL},
  //{"status_online", {ca_none}, do_status_online, "status_online\tSets status as online", NULL},
  //{"status_offline", {ca_none}, do_status_offline, "status_offline\tSets status as offline", NULL},
  //{"view", {ca_msg_id, ca_none}, do_open_any, "view <msg-id>\tTries to view message contents", NULL},
  //{"visualize_key", {ca_secret_chat, ca_none}, do_visualize_key, "visualize_key <secret chat>\tPrints visualization of encryption key (first 16 bytes sha1 of it in fact)", NULL}
};

void register_new_command (struct command *cmd) {
  int i = 0;
  while (commands[i].name) {
    i ++;
  }
  assert (i < MAX_COMMANDS_SIZE - 1);
  commands[i] = *cmd;
}

//tgl_peer_t *autocomplete_peer;
//tdl_message_id_t autocomplete_id;

enum command_argument get_complete_mode (void);

int complete_string_list (char **list, int index, const char *text, ssize_t len, char **R) {
  index ++;
  while (list[index] && strncmp (list[index], text, len)) {
    index ++;
  }
  if (list[index]) {
    *R = strdup (list[index]);
    assert (*R);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}

int complete_command_list (int index, const char *text, ssize_t len, char **R) {
  index ++;
  while (commands[index].name && strncmp (commands[index].name, text, len)) {
    index ++;
  }
  if (commands[index].name) {
    *R = strdup (commands[index].name);
    assert (*R);
    return index;
  } else {
    *R = 0;
    return -1;
  }
}

char *media_type_list[] = {
  "animation",
  "audio",
  "document",
  "photo",
  "sticker",
  "video",
  "voice",
  NULL
};


int complete_spec_message_answer (struct tdl_message *M, int index, const char *text, int len, char **R) {
    *R = NULL;
    return -1;
  /*if (!M || !M->reply_markup || !M->reply_markup->rows) {
    *R = NULL;
    return -1;
  }
  index ++;

  int total = M->reply_markup->row_start[M->reply_markup->rows];
  while (index < total && strncmp (M->reply_markup->buttons[index], text, len)) {
    index ++;
  }
  
  if (index < total) {
    *R = strdup (M->reply_markup->buttons[index]);
    assert (*R);
    return index;
  } else {
    *R = NULL;
    return -1;
  }*/
}

int complete_message_answer (union tdl_chat *P, int index, const char *text, int len, char **R) {
    *R = NULL;
    return -1;
  /*
  struct tdl_message *M = P->last;
  while (M && (M->flags & TGLMF_OUT)) {
    M = M->next;
  }


  return complete_spec_message_answer (M, index, text, len, R);*/
}

int complete_user_command (union tdl_chat *P, int index, const char *text, int len, char **R) {
    *R = NULL;
    return -1;
  /*if (len <= 0 || *text != '/') {
    return complete_message_answer (P, index, text, len, R);
  }
  text ++;
  len --;
  struct tdl_user *U = (void *)P;
  if (!U->bot_info) {
    *R = NULL;
    return -1;
  }
  if (index >= U->bot_info->commands_num) {
    return U->bot_info->commands_num + complete_message_answer (P, index - U->bot_info->commands_num, text - 1, len + 1, R);
  }
  
  index ++;
  while (index < U->bot_info->commands_num && strncmp (U->bot_info->commands[index].command, text, len)) {
    index ++;
  }
  if (index < U->bot_info->commands_num) {
    *R = NULL;
    assert (asprintf (R, "/%s", U->bot_info->commands[index].command) >= 0);
    assert (*R);
    return index;
  } else {
    return U->bot_info->commands_num + complete_message_answer (P, index - U->bot_info->commands_num, text - 1, len + 1, R);
  }*/
}

int complete_chat_command (union tdl_chat *P, int index, const char *text, int len, char **R) {
    *R = NULL;
    return -1;
  /*if (len <= 0 || *text != '/') {
    return complete_message_answer (P, index, text, len, R);
  }
  text ++;
  len --;

  index ++;

  int tot = 0;
  int i;
  for (i = 0; i < P->chat.user_list_size; i++) { 
    struct tdl_user *U = (void *)tgl_peer_get (TLS, TGL_MK_USER (P->chat.user_list[i].user_id));
    if (!U) { continue; }
    if (!U->bot_info) { continue; }
    int p = len - 1;
    while (p >= 0 && text[p] != '@') { p --; }
    if (p < 0) { p = len; }
    while (index - tot < U->bot_info->commands_num && strncmp (U->bot_info->commands[index - tot].command, text, p)) {
      index ++;
    }
    if (index - tot < U->bot_info->commands_num) {
      *R = NULL;
      if (U->username) {
        assert (asprintf (R, "/%s@%s", U->bot_info->commands[index].command, U->username) >= 0);
      } else {
        assert (asprintf (R, "/%s", U->bot_info->commands[index].command) >= 0);
      }

      assert (*R);
      return index;
    }
    tot += U->bot_info->commands_num;
  }

  if (index == tot) {
    return tot + complete_message_answer (P, index - tot, text - 1, len + 1, R);
  } else {
    return tot + complete_message_answer (P, index - tot - 1, text - 1, len + 1, R);
  }*/
}

int complete_username (int mode, int index, const char *text, ssize_t len, char **R) {  
  *R = NULL;
  if (alias_queue.next == NULL) { return -1; }
  index ++;
  int p = 0;
  struct chat_alias *A = alias_queue.next;
  while (p < index) {
    assert (A != &alias_queue);
    A = A->next;
    p ++;
  }

  while (A != &alias_queue) {
    int type = -1;
    if (A->type == -1) {
      type = ((struct tdl_chat_info *)A->chat)->chat->type;
    } else {
      type = A->type;
    }
    if (mode == -1 || mode == type) {
      if (!memcmp (A->name, text, len)) {
        *R = strdup (A->name);
        return index;
      }
    }
    A = A->next;
    index ++;
  }

  *R = NULL;
  return -1;
}

char *command_generator (const char *text, int state) {  
  static int index;
  static enum command_argument mode;
  static char *command_pos;
  static ssize_t command_len;

  if (cur_chat_mode_chat) {
    char *R = 0;
    index = complete_string_list (in_chat_commands, index, text, rl_point, &R);
    return R;
  }
 
  char c = 0;
  c = rl_line_buffer[rl_point];
  rl_line_buffer[rl_point] = 0;
  if (!state) {
    index = -1;
    
    mode = get_complete_mode ();
    command_pos = cur_token;
    command_len = cur_token_len;
  } else {
    if (mode != ca_file_name && mode != ca_file_name_end && index == -1) { return 0; }
  }
  
  if (mode == ca_none || mode == ca_string || mode == ca_string_end || mode == ca_number || mode == ca_double || mode == ca_msg_id) {   
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0; 
  }
  assert (command_len >= 0);

  char *R = 0;
  switch (mode & 255) {
  case ca_command:
    index = complete_command_list (index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_media_type:
    index = complete_string_list (media_type_list, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_user:
    index = complete_username (tdl_chat_type_user, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_chat:
    index = complete_username (-1, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_file_name:
  case ca_file_name_end:
    if (c) { rl_line_buffer[rl_point] = c; }
    R = rl_filename_completion_function (command_pos, state);
    return R;
  case ca_group:
    index = complete_username (tdl_chat_type_group, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_secret_chat:
    index = complete_username (tdl_chat_type_secret_chat, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_channel:
    index = complete_username (tdl_chat_type_channel, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_modifier:
    index = complete_string_list (modifiers, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_msg_string_end:
    /*if (autocomplete_peer) {
      if (tgl_get_peer_type (autocomplete_peer->id) == TGL_PEER_USER) {
        index = complete_user_command (autocomplete_peer, index, command_pos, command_len, &R);
      }
      if (tgl_get_peer_type (autocomplete_peer->id) == TGL_PEER_CHAT) {
        index = complete_chat_command (autocomplete_peer, index, command_pos, command_len, &R);
      }
    }
    if (autocomplete_id.peer_type != (unsigned)NOT_FOUND) {
      struct tdl_message *M = tdl_message_get (TLS, &autocomplete_id);
      if (M) {
        if (command_len > 0 && *command_pos == '/') {
          tgl_peer_t *P = tgl_peer_get (TLS, M->from_user_id);
          if (P) {
            index = complete_user_command (autocomplete_peer, index, command_pos, command_len, &R);
          }
        } else {
          index = complete_spec_message_answer (M, index, command_pos, command_len, &R);
        }
      }
    }*/
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
#ifndef DISABLE_EXTF
  case ca_extf:
    //index = tglf_extf_autocomplete (TLS, text, len, index, &R, rl_line_buffer, rl_point);
    index = -1;
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
#endif
  default:
    if (c) { rl_line_buffer[rl_point] = c; }
    return 0;
  }
}

void print_fail (struct in_command *cmd) {
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "FAIL: %d: %s\n", TLS->error_code, TLS->error);
  mprint_end (cmd->ev);
}

void fail_interface (struct tdlib_state *TLS, struct in_command *cmd, int error_code, const char *format, ...) {
  static char error[1001];

  va_list ap;
  va_start (ap, format);
  int error_len = vsnprintf (error, 1000, format, ap);
  va_end (ap);
  if (error_len > 1000) { error_len = 1000; }
  error[error_len] = 0;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "FAIL: %d: %s\n", error_code, error);
  mprint_end (cmd->ev);
}

void print_success (struct in_command *cmd) {
  if (cmd->ev || cmd->query_id) {
    mprint_start (cmd->ev);
    if (cmd->query_id) {
      mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
    }
    mprintf (cmd->ev, "SUCCESS\n");
    mprint_end (cmd->ev);
  }
}

void print_success_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  } else { 
    print_success (cmd); 
    return; 
  }
}

void print_msg_success_gw (struct in_command *cmd, int success, struct res_arg *args) {
  print_success_gw (cmd, success, args);
}

void print_msg_list_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  int num = args[0].vec_len;
  int i;
  for (i = num - 1; i >= 0; i--) {    
    struct tdl_message *M = args[0].vec[i].message;
    print_message (cmd->ev, M);
  }
  mprint_end (cmd->ev);
}

void print_msg_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  print_message (cmd->ev, args[0].message);
  mprint_end (cmd->ev);
}

void print_invite_link_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
  
  struct tdl_chat_invite_link_info *info = args[0].invite_link_info;

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }

  if (info->is_group) {
    mprintf (cmd->ev, "Group ");
    mpush_color (cmd->ev, COLOR_MAGENTA);
  } else if (info->is_supergroup_channel) {
    mprintf (cmd->ev, "Supergroup ");
    mpush_color (cmd->ev, COLOR_MAGENTA);
  } else {
    mprintf (cmd->ev, "Channel ");
    mpush_color (cmd->ev, COLOR_CYAN);
  }

  mprintf (cmd->ev, "%s", info->title);

  mpop_color (cmd->ev);
  mpop_color (cmd->ev);
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_user_list_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
  
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }

  int num = args[0].vec_len;  
  int i;
  for (i = num - 1; i >= 0; i--) {
    struct tdl_user *U = args[0].vec[i].user;
    if (U->id != 0) {
      print_user_name (cmd->ev, U, U->id);
      mprintf (cmd->ev, "\n");
    }
  }
  mprint_end (cmd->ev);
}

void print_member (struct in_ev *ev, struct tdl_chat_member *U);

void print_chat_members_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
 
  int total = args[0].num;
  int num = args[1].vec_len;

  mprint_start (cmd->ev);
  int i;
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  mprintf (cmd->ev, "Total %d members\n", total);
  for (i = num - 1; i >= 0; i--) {
    struct tdl_chat_member *UL = args[1].vec[i].chat_member;
    print_member (cmd->ev, UL);
    mprintf (cmd->ev, "\n");
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_chat_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
  
  struct tdl_chat_info *C = args[0].chat;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  print_chat_name (cmd->ev, C, C->id);
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);

  in_command_decref (cmd);
}

void print_user_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
 
  struct tdl_user *U = args[0].user;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  print_user_name (cmd->ev, U, U->id);
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_channel_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
 
  struct tdl_channel *C = args[0].channel;
  
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  struct telegram_cli_chat_extra *e = C->extra;
  if (e && e->owner_type < 0) {
    struct tdl_chat_info *I = e->owner;
    print_chat_name (cmd->ev, I, I->id);
  }
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_group_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
 
  struct tdl_group *G = args[0].group;
  
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  struct telegram_cli_chat_extra *e = G->extra;
  if (e && e->owner_type < 0) {
    struct tdl_chat_info *I = e->owner;
    print_chat_name (cmd->ev, I, I->id);
  }
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_peer_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  union tdl_chat *C = args[0].peer;

  switch (C->type) {
  case tdl_chat_type_user:
    print_user_gw (cmd, success, args);
    break;
  case tdl_chat_type_group:
    print_group_gw (cmd, success, args);
    break;
  case tdl_chat_type_channel:
    print_channel_gw (cmd, success, args);
    break;
  case tdl_chat_type_secret_chat:
    break;
  }
}


void print_string_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "%s\n", args[0].str);
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_filename_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "Saved to %s\n", args[0].str);
  mprint_end (cmd->ev);
}

void open_filename_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
  
  static char buf[PATH_MAX];
  if (snprintf (buf, sizeof (buf), OPEN_BIN, args[0].str) >= (int) sizeof (buf)) {
    logprintf ("Open image command buffer overflow\n");
  } else {
    int pid = fork ();
    if (!pid) {
      execl("/bin/sh", "sh", "-c", buf, (char *) 0);
      exit (0);
    }
  }
}

void print_member (struct in_ev *ev, struct tdl_chat_member *U) {
  switch (U->role) {
    case tdl_chat_member_role_creator:
      mprintf (ev, "Creator   ");
      break;
    case tdl_chat_member_role_editor:
      mprintf (ev, "Editor    ");
      break;
    case tdl_chat_member_role_moderator:
      mprintf (ev, "Moderator ");
      break;
    case tdl_chat_member_role_general:
      mprintf (ev, "Member    ");
      break;
    case tdl_chat_member_role_left:
      mprintf (ev, "Left      ");
      break;
    case tdl_chat_member_role_kicked:
      mprintf (ev, "Kicked    ");
      break;
  }


  struct tdl_user *Us = tdlib_instant_get_user (TLS, U->user_id);
  print_user_name (ev, Us, U->user_id);
  mprintf (ev, " invited by ");
  struct tdl_user *I = tdlib_instant_get_user (TLS, U->inviter_user_id);
  print_user_name (ev, I, U->inviter_user_id);
  mprintf (ev, " ");
  print_date_full (ev, U->join_date);
}

void print_group_info_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
 
  struct tdl_group *G = args[0].group;
  mprint_start (cmd->ev);

  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  mprintf (cmd->ev, "Chat ");
  struct telegram_cli_chat_extra *e = G->extra;
  if (e && e->owner_type < 0) {
    struct tdl_chat_info *I = e->owner;
    print_chat_name (cmd->ev, I, I->id);

    if (I->photo) {
      if (I->photo->big || I->photo->small) {
        mprintf (cmd->ev, "\tphoto:");
        if (I->photo->big) {
          mprintf (cmd->ev, " big:[photo %d]", I->photo->big->id);
        }
        if (I->photo->small) {
          mprintf (cmd->ev, " small:[photo %d]", I->photo->small->id);
        }
        mprintf (cmd->ev, "\n");
      }
    }
  }

  mprintf (cmd->ev, " (id %d) members:\n", G->id);
  int i;  
  for (i = 0; i < G->full->members_cnt; i++) {
    mprintf (cmd->ev, "\t\t");
    print_member (cmd->ev, G->full->members[i]);
    mprintf (cmd->ev, "\n");
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_channel_info_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }
 
  mprint_start (cmd->ev);
  struct tdl_channel *C = args[0].channel;

  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  if (C->is_supergroup) {
    mprintf (cmd->ev, "Supergroup ");
  } else {
    mprintf (cmd->ev, "Channel ");
  }
  if (C->is_verified) {
    mprintf (cmd->ev, "[verified] ");
  }

  struct telegram_cli_chat_extra *e = C->extra;
  if (e && e->owner_type < 0) {
    struct tdl_chat_info *I = e->owner;
    print_chat_name (cmd->ev, I, I->id);
  }

  if (C->username) {
    mprintf (cmd->ev, " @%s", C->username);
  }
  mprintf (cmd->ev, " (#%d):\n", C->id);
  if (C->full->about) {
    mprintf (cmd->ev, "\tabout: %s\n", C->full->about);
  }

  if (e && e->owner_type < 0) {
    struct tdl_chat_info *I = e->owner;

    if (I->photo) {
      if (I->photo->big || I->photo->small) {
        mprintf (cmd->ev, "\tphoto:");
        if (I->photo->big) {
          mprintf (cmd->ev, " big:[photo %d]", I->photo->big->id);
        }
        if (I->photo->small) {
          mprintf (cmd->ev, " small:[photo %d]", I->photo->small->id);
        }
        mprintf (cmd->ev, "\n");
      }
    }
  }

  mprintf (cmd->ev, "\t%d members, %d admins, %d kicked\n", C->full->members_cnt, C->full->admins_cnt, C->full->kicked_cnt);
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_user_info_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  struct tdl_user *U = args[0].user;
  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  if (U->deleted) {
    mprintf (cmd->ev, "Deleted user ");
  } else {
    mprintf (cmd->ev, "User ");
  }
  if (U->is_verified) {
    mprintf (cmd->ev, "[verified] ");
  }
  print_user_name (cmd->ev, U, U->id);
  if (U->username) {
    mprintf (cmd->ev, " @%s", U->username);
  }
  mprintf (cmd->ev, " (#%d):\n", U->id);
  mprintf (cmd->ev, "\tphone: %s\n", U->phone_number);
  mprintf (cmd->ev, "\t");
  print_user_status (cmd->ev, U->status);
  mprintf (cmd->ev, "\n");

  if (U->photo) {
    if (U->photo->big || U->photo->small) {
      mprintf (cmd->ev, "\tphoto:");
      if (U->photo->big) {
        mprintf (cmd->ev, " big:[photo %d]", U->photo->big->id);
      }
      if (U->photo->small) {
        mprintf (cmd->ev, " small:[photo %d]", U->photo->small->id);
      }
      mprintf (cmd->ev, "\n");
    }
  }

  if (U->full->bot_info) {
    mprintf (cmd->ev, "\tdescription: %s\n", U->full->bot_info->description);
    mprintf (cmd->ev, "\tcommands:\n");

    int i;
    for (i = 0; i < U->full->bot_info->commands_cnt; i++) {
      mprintf (cmd->ev, "\t\t/%s: %s\n", U->full->bot_info->commands[i]->command, U->full->bot_info->commands[i]->description);
    }
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_peer_info_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  union tdl_chat *C = args[0].peer;

  switch (C->type) {
  case tdl_chat_type_user:
    print_user_info_gw (cmd, success, args);
    break;
  case tdl_chat_type_group:
    print_group_info_gw (cmd, success, args);
    break;
  case tdl_chat_type_channel:
    print_channel_info_gw (cmd, success, args);
    break;
  case tdl_chat_type_secret_chat:
    break;
  }
}

void print_dialog_list_gw (struct in_command *cmd, int success, struct res_arg *args) {
  if (!success) { 
    print_fail (cmd); 
    return; 
  }

  int num = args[0].vec_len;
  
  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  int i;
  for (i = num - 1; i >= 0; i--) {
    mprintf (cmd->ev, "Dialog ");
    struct tdl_chat_info *C = args[0].vec[i].chat;    
    print_chat_name (cmd->ev, C, C->id);
    mprintf (cmd->ev, " unread\n");
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void interpreter_chat_mode (struct in_command *cmd) {
  char *line = cmd->line;
  if (line == NULL || /* EOF received */
          !strncmp (line, "/exit", 5) || !strncmp (line, "/quit", 5)) {
    cur_chat_mode_chat = NULL;
    update_prompt ();
    return;
  }
  if (!strncmp (line, "/history", 8)) {
    cmd->refcnt ++;

    int limit = 40;
    sscanf (line, "/history %99d", &limit);
    if (limit < 0 || limit > 1000) { limit = 40; }
  
    struct chat_history_extra *e = malloc (sizeof (*e));
    e->cmd = cmd;
    e->current_pos = 0;
    e->current_size = 0;
    e->current_offset = 0;
    e->limit = limit;
    e->last_msg_id = 0;
    e->chat_id = cmd->chat_mode_chat->id;
    e->list = NULL;
  
    do_send_history_query (e);
    return;
  }
  if (strlen (line) > 0) {
    cmd->refcnt ++;

    tdlib_view_messages (TLS, tdcli_empty_cb, NULL, cmd->chat_mode_chat->id, 1, &cmd->chat_mode_chat->top_message->id);
    
    union tdl_input_message_content *content = tdlib_create_input_message_content_text (TLS, line, 0, disable_msg_preview, 1);
    tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, cmd->chat_mode_chat->id, 0, 0, 0, NULL, content);
  }
}

int eq_str (char *a, char *b) {
  return a ? b ? strcmp (a, b) : 0 : b ? 0 : 1;
}

int upd_str (char **a, char *b) {
  if (*a) {
    if (!b || strcmp (*a, b)) {
      free (*a);
      *a = b ? strdup (b) : NULL;
      return 1;
    } else {
      return 0;
    }
  } else {
    *a = b ? strdup (b) : NULL;
    return b ? 1 : 0;
  }
}

struct update_subscriber {
  void *extra;
  struct update_subscriber *next, *prev;
  void (*cb)(void *extra, struct update_description *D, struct res_arg args[]);
};

struct update_subscriber update_subscribers_queue;

struct update_subscriber *subscribe_updates (void *extra, void (*on_update)(void *, struct update_description *, struct res_arg [])) {
  struct update_subscriber *Q = &update_subscribers_queue;
  if (!Q->next) { 
    Q->next = Q;
    Q->prev = Q;
  }
  struct update_subscriber *a = malloc (sizeof (*a));
  a->extra = extra;
  a->cb = on_update;
  a->next = Q;
  a->prev = Q->prev;
  a->next->prev = a;
  a->prev->next = a;
  return a;
}

void do_update (struct update_description *D, struct res_arg args[]) {
  if (!enable_json) {
    if (D->default_cb) {
      D->default_cb (NULL, D, args);
    }
  } else {
    #ifdef USE_JSON
    json_update_cb (NULL, D, args);
    #endif
  }

  struct update_subscriber *a = update_subscribers_queue.next;
  while (a && a != &update_subscribers_queue) {
    a->cb (a->extra, D, args);
    a = a->next;
  }
}

void default_on_new_msg (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
  struct in_ev *ev = ev;

  mprint_start (NULL);
  print_message (NULL, args[0].message);
  mprint_end (NULL);
}

void on_new_msg (struct tdlib_state *TLSR, struct tdl_message *M, int disable_notifications) {
  assert (TLSR == TLS);
  if (alert_sound && !disable_notifications) {
    play_sound ();
  }
  
  struct tdl_chat_info *C = tdlib_instant_get_chat (TLS, M->chat_id);
  if (C) {
    struct telegram_cli_chat_extra *e = C->extra;
    total_unread += C->unread_count - e->unread_count;    
    e->unread_count = C->unread_count;
  }

  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].message = M;
  args[1].num = disable_notifications;

  static struct update_description D = {
    {{"message", ra_message}, {"disable_notifications", ra_int}},
    default_on_new_msg,
    "new_msg"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void default_on_edit_msg (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
}

void on_edit_msg (struct tdlib_state *TLSR, struct tdl_message *M) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].message = M;

  static struct update_description D = {
    {{"message", ra_message}},
    default_on_edit_msg,
    "edit_msg"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void add_alias_internal (struct telegram_cli_chat_extra *e, char *alias) {
  if (!alias) { return; }
  e->aliases_cnt ++;
  e->aliases = realloc (e->aliases, sizeof (void *) * (e->aliases_cnt));
  e->aliases[e->aliases_cnt - 1] = strdup (alias);

  add_alias (alias, e->owner_type, e->owner);
}

void del_alias_internal (struct telegram_cli_chat_extra *e, char *alias) {
  if (!alias) { return; }
  int i;
  for (i = 0; i < e->aliases_cnt; i++) {
    if (!strcmp (e->aliases[i], alias)) {
      del_alias (alias);
      e->aliases[i] = e->aliases[e->aliases_cnt - 1];
      free (e->aliases[i]);
      e->aliases = realloc (e->aliases, sizeof (void *) * (e->aliases_cnt - 1));
      e->aliases_cnt --;
      return;
    }
  }
}

void sub_alias_internal (struct telegram_cli_chat_extra *e, char *alias, char *new_alias) {
  if (!alias && !new_alias) { return; }
  if (!alias) { return add_alias_internal (e, new_alias); }
  if (!new_alias) { return del_alias_internal (e, alias); }
  if (!strcmp (alias, new_alias)) { return; }

  int i;
  for (i = 0; i < e->aliases_cnt; i++) {
    if (!strcmp (e->aliases[i], alias)) {
      del_alias (alias);
      free (e->aliases[i]);
      e->aliases[i] = strdup (new_alias);
      add_alias (new_alias, e->owner_type, e->owner);
      return;
    }
  }
}

void upd_aliases_internal (struct telegram_cli_chat_extra *e) {
  int i;
  for (i = 0; i < e->aliases_cnt; i++) {
    del_alias (e->aliases[i]);
    add_alias (e->aliases[i], e->owner_type, e->owner);
  }
}

int utf8_char_len (unsigned char c) {
  if ((c & 0x80) == 0) { return 1; }
  if ((c & 0xc0) == 0x80) { return 2; }
  if ((c & 0xe0) == 0xc0) { return 3; }
  if ((c & 0xf0) == 0xe0) { return 4; }
  if ((c & 0xf8) == 0xf0) { return 5; }
  if ((c & 0xfc) == 0xf8) { return 6; }
  if ((c & 0xfe) == 0xfc) { return 7; }
  return 8;
}

char *generate_alias_title (struct telegram_cli_chat_extra *e, const char *title) {
  if (!title) { return NULL; }
  static char s[256];
  ssize_t l = strlen (title);
  if (l >= 250) { l = 250; }
  memcpy (s, title, l);
  s[l] = 0;

  int p = 0;
  while (p < l) {
    if ((s[p] <= 32 && s[p] >= 0) || (p == 0 && s[p] == '@') || s[p] == '#') {
      s[p] = '_';
    }
    p ++;
  }

  p = 0;
  while (1) {
    struct chat_alias *A = get_by_alias (s);
    if (!A) { break; }
    if ((A->chat == e->owner) && A->type == e->owner_type) {
      break;
    }
    sprintf (s + l, "#%d", ++p);
  }
  return s;
}

char *generate_alias_name (struct telegram_cli_chat_extra *e, char *first_name, char *last_name) {
  if (!first_name && !last_name) { return NULL; }
  if (!first_name) { return generate_alias_title (e, last_name); }
  if (!last_name) { return generate_alias_title (e, first_name); }
  static char s[256];
  snprintf (s, 250, "%s %s", first_name, last_name);
  return generate_alias_title (e, s);
}

char *generate_alias_username (char *username) {
  if (!username) { return NULL; }
  static char s[256];
  s[0] = '@';
  strncpy (s + 1, username, 250);
  char *t = s + 1;
  while (*t) {
    if (*t >= 'A' && *t <= 'Z') {
      *t = (char)(*t + 'a' - 'A');
    }
    t ++;
  }
  return s;
}


void default_on_user_update (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
}

void on_user_update (struct tdlib_state *TLSR, struct tdl_user *U) {
  struct telegram_cli_chat_extra *e = U->extra;
  if (!U->extra) {
    e = calloc (sizeof (*e), 1);
    e->owner_type = tdl_chat_type_user;
    e->owner = U;
    U->extra = e;
  
    char s[20];
    sprintf (s, "user#id%d", U->id);
    e->main_alias = strdup (s);
    add_alias_internal (e, s);
  }

  char *u;
  
  u = generate_alias_username (U->username);
  sub_alias_internal (e, e->username_alias, u);
  upd_str (&e->username_alias, u);
  
  if (e->owner == U) {
    u = generate_alias_name (e, U->first_name, U->last_name);
    sub_alias_internal (e, e->name_alias, u);
    upd_str (&e->name_alias, u);
  }
  
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].user = U;

  static struct update_description D = {
    {{"user", ra_user}},
    default_on_user_update,
    "user_update"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void default_on_group_update (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
}

void on_group_update (struct tdlib_state *TLSR, struct tdl_group *G) {
  struct telegram_cli_chat_extra *e = G->extra;
  if (!G->extra) {
    e = calloc (sizeof (*e), 1);
    e->owner_type = tdl_chat_type_group;
    e->owner = G;
    G->extra = e;
  
    char s[20];
    sprintf (s, "group#id%d", G->id);
    e->main_alias = strdup (s);
    add_alias_internal (e, s);
  }
  
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].group = G;

  static struct update_description D = {
    {{"group", ra_group}},
    default_on_group_update,
    "group_update"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void default_on_channel_update (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
}

void on_channel_update (struct tdlib_state *TLSR, struct tdl_channel *Ch) {
  struct telegram_cli_chat_extra *e = Ch->extra;
  if (!Ch->extra) {
    e = calloc (sizeof (*e), 1);
    e->owner_type = tdl_chat_type_channel;
    e->owner = Ch;
    Ch->extra = e;
  
    char s[20];
    sprintf (s, "channel#id%d", Ch->id);
    e->main_alias = strdup (s);
    add_alias_internal (e, s);
  }

  char *u;
  
  u = generate_alias_username (Ch->username);
  sub_alias_internal (e, e->username_alias, u);
  upd_str (&e->username_alias, u);
  
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].channel = Ch;

  static struct update_description D = {
    {{"channel", ra_channel}},
    default_on_channel_update,
    "channel_update"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void on_secret_chat_update (struct tdlib_state *TLSR, struct tdl_secret_chat *U) {}

void default_on_chat_update (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
}

void on_chat_update (struct tdlib_state *TLSR, struct tdl_chat_info *C) {
  struct telegram_cli_chat_extra *e = C->extra;
  if (!e) {
    if (!C->chat->extra) {
      switch (C->chat->type) {
      case tdl_chat_type_user:
        on_user_update (TLS, &C->chat->user);
        break;
      case tdl_chat_type_group:
        on_group_update (TLS, &C->chat->group);
        break;
      case tdl_chat_type_channel:
        on_channel_update (TLS, &C->chat->channel);
        break;
      case tdl_chat_type_secret_chat:
        on_secret_chat_update (TLS, &C->chat->secret_chat);
        break;
      }
    }
    assert (C->chat->extra);
    C->extra = C->chat->extra;
    e = C->extra;
    e->owner = C;
    e->owner_type = -1;
    upd_aliases_internal (e);
  }

  assert (e);

  char *u;
  u = generate_alias_title (e, C->title);
  sub_alias_internal (e, e->name_alias, u);
  upd_str (&e->name_alias, u);

  total_unread += C->unread_count - e->unread_count;
  e->unread_count = C->unread_count;
 
  
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;

  static struct update_description D = {
    {{"chat", ra_chat}},
    default_on_chat_update,
    "chat_update"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void on_net_state_change (struct tdlib_state *TLS, enum tdl_connection_state new_state) {
  conn_state = new_state;
  update_prompt ();
}
  
void on_my_id (struct tdlib_state *TLS, int id) {
  assert (!TLS->my_id || TLS->my_id == id);
  TLS->my_id = id;
  
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].num = id;

  static struct update_description D = {
    {{"id", ra_int}},
    NULL,
    "self_id"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}
 
void default_on_type_notification (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
  if (log_level < 2) { return; }

  struct tdl_chat_info *C = args[0].chat;
  struct tdl_user *U = args[1].user;

  const char *type = args[2].str;
  int progress = args[3].num;
    
  struct in_ev *ev = NULL;
  mprint_start (ev);
  mpush_color (ev, COLOR_YELLOW);
  print_date (notify_ev, time (0));
  print_user_name (ev, U, U->id);
  if (C->chat != (union tdl_chat *)U) {
    mprintf (ev, " in ");
    print_chat_name (ev, C, C->id);
  }
  mprintf (ev, " ");

  if (!strcmp (type, "typing")) {
    mprintf (ev, "is typing");
  } else if (!strcmp (type, "cancel")) {
    mprintf (ev, "deleted typed message");
  } else if (!strcmp (type, "record_video")) {
    mprintf (ev, "is recording video");
  } else if (!strcmp (type, "upload_video")) {
    mprintf (ev, "is uploading video: progress %d%%", progress);
  } else if (!strcmp (type, "record_voice")) {
    mprintf (ev, "is recording voice message");
  } else if (!strcmp (type, "upload_voice")) {
    mprintf (ev, "is uploading voice message: progress %d%%", progress);
  } else if (!strcmp (type, "upload_photo")) {
    mprintf (ev, "is uploading photo: progress %d%%", progress);
  } else if (!strcmp (type, "upload_document")) {
    mprintf (ev, "is uploading document: progress %d%%", progress);
  } else if (!strcmp (type, "send_location")) {
    mprintf (ev, "is choosing geo location");
  } else if (!strcmp (type, "contact_contact")) {
    mprintf (ev, "is choosing contact");
  }
  mprintf (ev, "\n");
  mpop_color (ev);
  mprint_end (ev);
}

void on_type_notification (struct tdlib_state *TLS, struct tdl_chat_info *C, struct tdl_user *U, union tdl_user_action *action) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;
  args[1].user = U;
  args[3].num = 0;
  args[2].flags = 1;
  switch (action->type) {
    case tdl_message_typing_action_typing:
      args[2].str = strdup ("typing");
      break;
    case tdl_message_typing_action_cancel:
      args[2].str = strdup ("cancel");
      break;
    case tdl_message_typing_action_record_video:
      args[2].str = strdup ("record_video");
      break;
    case tdl_message_typing_action_upload_video:
      args[2].str = strdup ("upload_video");
      args[3].num = action->upload.progress;
      break;
    case tdl_message_typing_action_record_voice:
      args[2].str = strdup ("record_voice");
      break;
    case tdl_message_typing_action_upload_voice:
      args[2].str = strdup ("upload_voice");
      args[3].num = action->upload.progress;
      break;
    case tdl_message_typing_action_upload_photo:
      args[2].str = strdup ("upload_photo");
      args[3].num = action->upload.progress;
      break;
    case tdl_message_typing_action_upload_document:
      args[2].str = strdup ("upload_document");
      args[3].num = action->upload.progress;
      break;
    case tdl_message_typing_action_send_location:
      args[2].str = strdup ("send_location");
      break;
    case tdl_message_typing_action_choose_contact:
      args[2].str = strdup ("choose_contact");
      break;
  }


  static struct update_description D = {
    {{"chat", ra_chat}, {"user", ra_user}, {"type", ra_string}, {"progress", ra_int}},
    default_on_type_notification,
    "type_notification"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}
  
void default_on_new_authorization (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
  struct in_ev *ev = NULL;
  
  mprint_start (ev);
  mpush_color (ev, COLOR_REDB);
  print_date (ev, time (0));
  mprintf (ev, "New authorization: device '%s' location '%s'\n", args[0].str, args[1].str);
  mpop_color (ev);
  mprint_end (ev);
}

void on_new_authorization (struct tdlib_state *TLS, int date, const char *device, const char *location) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].str = strdup (device);
  args[0].flags = 1;
  args[1].str = strdup (location);
  args[1].flags = 1;
  args[2].num = date;

  static struct update_description D = {
    {{"device", ra_string}, {"location", ra_string}, {"date", ra_int}},
    default_on_new_authorization,
    "new_authorization"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void on_stickers_updated (struct tdlib_state *TLS) { return; }
void on_saved_animations_updated(struct tdlib_state *TLS) { return; }

void default_on_user_status_update (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
  if (log_level < 3) { return; }

  struct in_ev *ev = NULL;

  struct tdl_user *U = args[0].user;
  mprint_start (ev);
  mpush_color (ev, COLOR_YELLOW);

  print_date (ev, time (0));
  print_user_name (ev, U, U->id);
  mprintf (ev, " is now ");
  print_user_status (ev, U->status);
  mprintf (ev, "\n");
  mpop_color (ev);

  mprint_end (ev);
}

void on_user_status_update (struct tdlib_state *TLS, struct tdl_user *U) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].user = U;

  static struct update_description D = {
    {{"user", ra_user}},
    default_on_user_status_update,
    "user_status"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void default_on_messages_deleted (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }
  struct in_ev *ev = NULL;

  struct tdl_chat_info *C = args[0].chat;
  
  mprint_start (ev);
  mpush_color (ev, COLOR_YELLOW);
  print_date (ev, time (0));
  mprintf (ev, "Deleted %d messages from ", args[1].vec_len);
  print_chat_name (ev, C, C->id);
  mprintf (ev, "\n");
  mpop_color (ev);
  mprint_end (ev);
}

void on_messages_deleted (struct tdlib_state *TLS, struct tdl_chat_info *C, int cnt, int *ids) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;
  args[1].flags = 2;
  args[1].vec_len = cnt;
  args[1].vec = malloc (sizeof (struct res_arg) * cnt);
  int i;
  for (i = 0; i < cnt; i++) {
    args[1].vec[i].num = ids[i];
  }

  static struct update_description D = {
    {{"chat", ra_chat}, {"ids", ra_int | ra_vector}},
    default_on_messages_deleted,
    "delete_messages"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void on_reply_markup_updated (struct tdlib_state *TLS, struct tdl_chat_info *C) {}
 

void default_on_message_sent (void *extra, struct update_description *D, struct res_arg args[]) {
  if (disable_output) { return; }

  default_on_new_msg (extra, D, args);
}

void on_message_sent (struct tdlib_state *TLS, struct tdl_chat_info *C, struct tdl_message *M, int old_message_id) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].message = M;
  args[1].num = old_message_id;

  static struct update_description D = {
    {{"message", ra_message}, {"old_id", ra_int}},
    default_on_message_sent,
    "message_sent"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}
  
void on_message_failed (struct tdlib_state *TLS, struct tdl_chat_info *C, struct tdl_message *M, int error_code, const char *error) {}
  
void on_updated_message_content(struct tdlib_state *TLS, struct tdl_chat_info *C, int message_id, union tdl_message_content *content) {}
  
void on_updated_message_views(struct tdlib_state *TLS, struct tdl_chat_info *C, int message_id, int views) {}
  
void on_updated_chat_top_message(struct tdlib_state *TLS, struct tdl_chat_info *C) {}
void on_updated_chat_order(struct tdlib_state *TLS, struct tdl_chat_info *C) {}
  
void default_on_updated_chat_title (void *extra, struct update_description *D, struct res_arg args[]) {
}

void on_updated_chat_title (struct tdlib_state *TLS, struct tdl_chat_info *C) {
  if (!C->extra) { return; }
  
  struct telegram_cli_chat_extra *e = C->extra;
  assert (e);

  char *u;
  u = generate_alias_title (e, C->title);
  sub_alias_internal (e, e->name_alias, u);
  upd_str (&e->name_alias, u);

  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;

  static struct update_description D = {
    {{"chat", ra_chat}},
    default_on_updated_chat_title,
    "chat_update_title"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}
  
void default_on_updated_chat_photo (void *extra, struct update_description *D, struct res_arg args[]) {
}
  
void on_updated_chat_photo (struct tdlib_state *TLS, struct tdl_chat_info *C) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;

  static struct update_description D = {
    {{"chat", ra_chat}},
    default_on_updated_chat_photo,
    "chat_update_photo"
  };

  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void default_on_read_inbox (void *extra, struct update_description *D, struct res_arg args[]) {
}
  
void on_marked_read_inbox (struct tdlib_state *TLS, struct tdl_chat_info *C) {
  struct telegram_cli_chat_extra *e = C->extra;
  total_unread += C->unread_count - e->unread_count;
  e->unread_count = C->unread_count;

  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;

  static struct update_description D = {
    {{"chat", ra_chat}},
    default_on_read_inbox,
    "chat_read_inbox"
  };
  
  do_update (&D, args);

  free_res_arg_list (args, 10);
 
  update_prompt ();
}

void default_on_read_outbox (void *extra, struct update_description *D, struct res_arg args[]) {
}
  
void on_marked_read_outbox (struct tdlib_state *TLS, struct tdl_chat_info *C) {
  struct res_arg args[10];
  memset (&args, 0, sizeof (args));
  args[0].chat = C;

  static struct update_description D = {
    {{"chat", ra_chat}},
    default_on_read_outbox,
    "chat_read_outbox"
  };
  
  do_update (&D, args);

  free_res_arg_list (args, 10);
}

void on_update_file_progress (struct tdlib_state *TLS, long long file_id, int size, int ready) {
}
  
void on_update_file (struct tdlib_state *TLS, struct tdl_file *F) {
  if (F->path) {
    struct file_wait *W = tree_lookup_file_wait (file_wait_tree, (void *)&F->id);
    if (W) {
      struct file_wait_cb *cb = W->first_cb;
      while (cb) {
        cb->callback (TLS, cb->callback_extra, 1, F->path);
        struct file_wait_cb *n = cb->next;
        free (cb);
        cb = n;
      }
      file_wait_tree = tree_delete_file_wait (file_wait_tree, W);
      free (W);
    }
  }
}

void wakeup (struct tdlib_state *TLS);

struct tgl_update_callback upd_cb = {
  .wakeup = wakeup,
  .user_update = on_user_update,
  .group_update = on_group_update,
  .channel_update = on_channel_update,
  .secret_chat_update = on_secret_chat_update,
  .chat_update = on_chat_update,
  .new_msg = on_new_msg,
  .edit_msg = on_edit_msg,
  .change_net_state = on_net_state_change,
  .my_id = on_my_id,
  .type_notification = on_type_notification,
  .new_authorization = on_new_authorization,
  .stickers_updated = on_stickers_updated,
  .saved_animations_updated = on_saved_animations_updated,
  .user_status_update = on_user_status_update,
  .messages_deleted = on_messages_deleted,
  .reply_markup_updated = on_reply_markup_updated,
  .message_sent = on_message_sent,
  .message_failed = on_message_failed,
  .updated_message_content = on_updated_message_content,
  .updated_message_views = on_updated_message_views,
  .updated_chat_top_message = on_updated_chat_top_message,
  .updated_chat_order = on_updated_chat_order,
  .updated_chat_title = on_updated_chat_title,
  .updated_chat_photo = on_updated_chat_photo,
  .marked_read_inbox = on_marked_read_inbox,
  .marked_read_outbox = on_marked_read_outbox,
  .update_file_progress = on_update_file_progress,
  .update_file = on_update_file

};

int parse_argument_modifier (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int opt = (D->type & ca_optional) | (D->type & ca_period);

  char *save = line_ptr;
  next_token ();
  
  if (cur_token_len < 0 || cur_token_quoted) {
    A->str = NULL;
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else if (cur_token_end_str) {
    if (cur_token_len >= 1 && cur_token[0] == '[') {
      return -3;
    } else {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    }
  } else {
    if (cur_token_len >= 2 && cur_token[0] == '[' && cur_token[cur_token_len - 1] == ']') {
      A->flags = 1;
      A->str = strndup (cur_token, cur_token_len);
      return 0;
    } else {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    }
  }
}

int parse_argument_string (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int opt = (D->type & ca_optional) | (D->type & ca_period);

  char *save = line_ptr;
  next_token ();

  if (cur_token_end_str || cur_token_len < 0) {
    A->str = NULL;
    if (cur_token_end_str) { return -3; }
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else {
    A->flags = 1;
    A->str = strndup (cur_token, cur_token_len);
    return 0;
  }
}

int parse_argument_string_end (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  next_token_end ();
      
  if (cur_token_len < 0) { 
    return -1;
  } else {
    A->flags = 1;
    A->str = strndup (cur_token, cur_token_len);
    return 0;
  }
}

int parse_argument_number (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int opt = (D->type & ca_optional) | (D->type & ca_period);

  char *save = line_ptr;
  next_token ();

  if (cur_token_quoted || cur_token_len < 0) {
    A->num = NOT_FOUND;
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else if (cur_token_end_str) {
    int i;
    for (i = 0; i < cur_token_len; i++) {
      if (cur_token[i] < '0' && cur_token[i] >= '9') {
        if (i != 0 || cur_token[i] != '-') {
          if (opt) {
            line_ptr = save;
            return -5;
          }
          return -1;
        }
      }
    }
    A->num = NOT_FOUND;
    return -3;
  } else {
    char *token = strndup (cur_token, cur_token_len);
    A->num = cur_token_int (token);
    free (token);

    if (A->num == NOT_FOUND) {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    } else { 
      return 0;
    }
  }
}

int parse_argument_double (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int opt = (D->type & ca_optional) | (D->type & ca_period);

  char *save = line_ptr;
  next_token ();

  if (cur_token_quoted || cur_token_len < 0) {
    A->dval = NOT_FOUND;
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else if (cur_token_end_str) {
    int i;
    for (i = 0; i < cur_token_len; i++) {
      if (cur_token[i] < '0' && cur_token[i] >= '9') {
        if (cur_token[i] != '-' && cur_token[i] != 'e' && cur_token[i] != '.' && cur_token[i] != 'E') {
          if (opt) {
            line_ptr = save;
            return -5;
          }
          return -1;
        }
      }
    }
    A->dval = NOT_FOUND;
    return -3;
  } else {
    char *token = strndup (cur_token, cur_token_len);
    A->dval = cur_token_double (token);
    free (token);

    if (A->dval == NOT_FOUND) {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    } else { 
      return 0;
    }
  }
}

int parse_argument_msg_id (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int opt = (D->type & ca_optional) | (D->type & ca_period);

  char *save = line_ptr;
  next_token ();

  if (cur_token_quoted || cur_token_len < 0) {
    A->msg_id.message_id = -1;
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else if (cur_token_end_str) {
    A->msg_id.message_id = -1;
    return -3;
  } else {
    char *token = strndup (cur_token, cur_token_len);
    tdl_message_id_t id = cur_token_msg_id (token, cmd);
    free (token);

    if (id.message_id == -2) {            
      return -2;
    }
    A->msg_id = id;

    if (A->msg_id.message_id == -1) {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    } else { 
      return 0;
    }
  }
}

int parse_argument_chat (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int opt = (D->type & ca_optional) | (D->type & ca_period);
  int op = D->type & 255;

  char *save = line_ptr;
  next_token ();

  if (cur_token_quoted || cur_token_len < 0) {
    A->chat = NULL;
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else if (cur_token_end_str) {
    A->chat = NULL;
    return -3;
  } else {
    int m = -1;
    if (op == ca_user) { m = tdl_chat_type_user; }
    if (op == ca_group) { m = tdl_chat_type_group; }
    if (op == ca_channel) { m = tdl_chat_type_channel; }
    if (op == ca_secret_chat) { m = tdl_chat_type_secret_chat; }            

    char *token = strndup (cur_token, cur_token_len);
    struct tdl_chat_info *C = cur_token_peer (token, m, cmd);
    free (token);
            
    if (C == (void *)-1l) {
      return -2;
    }

    if (!C) {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    } else { 
      A->chat = C;
      return 0;
    }
  }
}

int parse_argument_any (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int op = D->type & 255;

  switch (op) {
  case ca_user:
  case ca_group:
  case ca_secret_chat:
  case ca_channel:
  case ca_chat:
    return parse_argument_chat (cmd, A, D);
  case ca_file_name:
  case ca_string:
  case ca_media_type:
  case ca_command:
    return parse_argument_string (cmd, A, D);
  case ca_modifier:
    return parse_argument_modifier (cmd, A, D);
  case ca_file_name_end:
  case ca_string_end:
  case ca_msg_string_end:
    return parse_argument_string_end (cmd, A, D);
  case ca_number:
    return parse_argument_number (cmd, A, D);
  case ca_double:
    return parse_argument_double (cmd, A, D);
  case ca_msg_id:
    return parse_argument_msg_id (cmd, A, D);
  case ca_none:
  default:
    logprintf ("type=%d\n", op);
    assert (0);
    return -1;
  }
}

int parse_argument_period (struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  A->flags = 2;

  A->vec_len = 0;
  A->vec = malloc (0);

  int opt = D->type & ca_optional;

  struct arg T;
  while (1) {
    memset (&T, 0, sizeof (T));
    int r = parse_argument_any (cmd, &T, D);
    if (r == -2) { return r; }
    if (r == -1 || r == -5) {
      return A->vec_len ? 0 : (opt ? -5 : -1);
    }
    if (r == -3) {
      return r;
    }
    A->vec = realloc (A->vec, sizeof (struct arg) * (A->vec_len + 1));
    A->vec[A->vec_len ++] = T;
  }
}

void free_argument (struct arg *A) {
  if (!A->flags) { return; }
  if (A->flags == 1) {
    free (A->str);
    return;
  }
  assert (A->flags == 2);
  int i;
  for (i = 0; i < A->vec_len; i++) {
    free_argument (&A->vec[i]);
  }
  free (A->vec);
}

void free_args_list (struct arg args[], int cnt) {
  int i;
  for (i = 0; i < cnt; i++) {
    free_argument (&args[i]);
  }
}

struct command_argument_desc carg_0 = { "modifiers", ca_modifier | ca_period | ca_optional };
struct command_argument_desc carg_1 = { "command", ca_command };

int parse_command_line (struct arg args[], struct in_command *cmd, int complete) {
  //struct command_argument_desc *D = command->args;
  //void (*fun)(struct command *, int, struct arg[], struct in_command *) = command->fun;
  struct command *command = NULL;

  int p = 0;  
  int ok = 0;

  int complete_mode = ca_none;

  while (1) {
    struct command_argument_desc *D;
    if (p == 0) {
      D = &carg_0;
    } else if (p == 1) {
      D = &carg_1;
    } else {
      D = &command->args[p - 2];
    }
    if (!D->type) {
      break;
    }
    
    int r;
    if (D->type & ca_period) {
      r = parse_argument_period (cmd, &args[p], D);
    } else {
      r = parse_argument_any (cmd, &args[p], D);
    }

    if (r == -5) {
      assert (D->type & ca_optional);
      r = 0;
    }
   
    if (r == -3) {
      if (!complete) {
        if (D->type & ca_period) {
          if (args[p].vec_len > 0 || (D->type & ca_optional)) {
            r = 0;
          }
        } else if (D->type & ca_optional) {
          r = 0;
        }
      } else {
        complete_mode = D->type;
      }
    }

    if (!r && p == 1) {
      command = commands;      
      while (command->name) {
        if (!strcmp (command->name, args[p].str)) {
          break;
        }
        command ++;
      }
      if (!command->name) {
        r = -4;
        if (!complete) {
          fail_interface (TLS, cmd, ENOSYS, "unknown command %s", args[p].str);
        }
      }
    }

    p ++;

    if (r == -1 && !complete) {
      fail_interface (TLS, cmd, ENOSYS, "can not parse arg '%s'", D->name);
    }

    if (r == -3 && !complete) {
      fail_interface (TLS, cmd, ENOSYS, "can not parse arg '%s': EOF", D->name);
    }

    if (r < 0) {
      ok = r;
      break;
    }

  }

  if (!ok && !complete) {
    next_token ();
    if (cur_token_len != 0) {
      fail_interface (TLS, cmd, ENOSYS, "too many args");
      ok = -1;
    }
  }
  return (complete) ? (ok == -3 ? complete_mode : ca_none) : ok;
}

enum command_argument get_complete_mode (void) {
  line_ptr = rl_line_buffer;
  force_end_mode = 0;

  struct arg args[12];
  memset (&args, 0, sizeof (args));

  int res = parse_command_line (args, NULL, 1);
  
  free_args_list (args, 12);

  return res;
}

void interpreter_ex (struct in_command *cmd) {  
  #ifdef USE_JSON
  if (enable_json) {
    json_interpreter_ex (cmd);
    return;
  }
  #endif
  char *line = cmd->line;
  force_end_mode = 1;
  if (cmd->chat_mode_chat) {
    interpreter_chat_mode (cmd);
    return;
  }

  line_ptr = line;
  
  if (!line) { 
    do_safe_quit (NULL, 0, NULL, NULL);
    return; 
  }
  if (!*line) {
    return;
  }

  if (line && *line) {
    add_history (line);
  }

  struct arg args[12];
  memset (&args, 0, sizeof (args));


  int res = parse_command_line (args, cmd, 0);

  if (!res) {
    struct command *command = commands;    

    while (command->name) {
      if (!strcmp (command->name, args[1].str)) {
        break;
      }
      command ++;
    }

    cmd->query_id = (int)find_modifier (args[0].vec_len, args[0].vec, "id", 2);
    int count = (int)find_modifier (args[0].vec_len, args[0].vec, "x", 1);
    if (!count) { count = 1; }
    cmd->cmd = command;

    #ifdef USE_JSON
    cmd->cb = enable_json ? json_universal_cb : command->default_cb;    
    #else
    cmd->cb = command->default_cb;    
    #endif
    command->fun (command, 12, args, cmd);
  }
  
  free_args_list (args, 12);

  update_prompt ();
}

void interpreter (char *line) {
  if (!line) {
    do_safe_quit (NULL, 0, NULL, NULL);
  }
  struct in_command *cmd = malloc (sizeof (*cmd));
  memset (cmd, 0, sizeof (struct in_command));
  cmd->ev = NULL;
  cmd->line = strdup (line);
  cmd->chat_mode_chat = cur_chat_mode_chat;
  cmd->refcnt = 1;
  in_readline = 1;
  interpreter_ex (cmd);
  in_readline = 0;
  in_command_decref (cmd);
}

int readline_active;
/*void rprintf (const char *format, ...) {
  mprint_start (ev);
  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  print_end();
}*/

int saved_point;
char *saved_line;
static int prompt_was;


void deactivate_readline (void) {
  if (read_one_string) {
    printf ("\033[2K\r");
    fflush (stdout);
  } else {
    saved_point = rl_point;
    saved_line = malloc (rl_end + 1);
    assert (saved_line);
    saved_line[rl_end] = 0;
    memcpy (saved_line, rl_line_buffer, rl_end);

    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
  }
}


extern char *one_string_prompt;
extern int one_string_flags;
extern int one_string_len;
extern char one_string[];

void reactivate_readline (void) {
  if (read_one_string) {
    printf ("%s ", one_string_prompt);
    if (!(one_string_flags & 1)) {
      printf ("%.*s", one_string_len, one_string);
    }
    fflush (stdout);
  } else {
    set_prompt (get_default_prompt ());
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    free (saved_line);
  }
}

void print_start (void) {
  if (in_readline) { return; }
  if (readline_disabled) { return; }
  assert (!prompt_was);
  if (readline_active) {
    deactivate_readline ();
  }
  prompt_was = 1;
}

void print_end (void) {
  if (in_readline) { return; }
  if (readline_disabled) { 
    fflush (stdout);
    return; 
  }
  assert (prompt_was);
  if (readline_active) {
    reactivate_readline ();
  }
  prompt_was = 0;
}

/*void hexdump (int *in_ptr, int *in_end) {
  mprint_start (ev);
  int *ptr = in_ptr;
  while (ptr < in_end) { mprintf (ev, " %08x", *(ptr ++)); }
  mprintf (ev, "\n");
  mprint_end (ev); 
}*/

void logprintf (const char *format, ...) {
  int x = 0;
  if (!prompt_was) {
    x = 1;
    print_start ();
  }
  if (!disable_colors) {
    printf (COLOR_GREY);
  }
  printf (" *** ");


  double T = (double)time (0);
  printf ("%.6lf ", T);

  va_list ap;
  va_start (ap, format);
  vfprintf (stdout, format, ap);
  va_end (ap);
  if (!disable_colors) {
    printf (COLOR_NORMAL);
  }
  if (x) {
    print_end ();
  }
}

int color_stack_pos;
const char *color_stack[10];

void push_color (const char *color) {
  if (disable_colors) { return; }
  assert (color_stack_pos < 10);
  color_stack[color_stack_pos ++] = color;
  printf ("%s", color);
}

void pop_color (void) {
  if (disable_colors) { return; }
  assert (color_stack_pos > 0);
  color_stack_pos --;
  if (color_stack_pos >= 1) {
    printf ("%s", color_stack[color_stack_pos - 1]);
  } else {
    printf ("%s", COLOR_NORMAL);
  }
}

//int unknown_user_list_pos;
//int unknown_user_list[1000];

/*void print_peer_permanent_name (struct in_ev *ev, tgl_peer_id_t id) {
  mprintf (ev, "%s", print_permanent_peer_id (id));
}*/

/*void print_user_name (struct in_ev *ev, union tdl_chat *U) {
  assert (U->type == tdl_chat_type_user);
  mpush_color (ev, COLOR_RED);
  if (!U) {
    mprintf (ev, "user#id%d", U->id);
  } else {
    if (U->id == TLS->my_id || U->user.my_link == tdl_user_link_state_contact) {
      mpush_color (ev, COLOR_REDB);
    }
    if (U->user.deleted) {
      mprintf (ev, "deleted user#id%d", U->id);
    } else if (use_ids) {
      mprintf (ev, "user#id%d", U->id);
    } else if (!U->user.first_name || !strlen (U->user.first_name)) {
      mprintf (ev, "%s", U->user.last_name);
    } else if (!U->user.last_name || !strlen (U->user.last_name)) {
      mprintf (ev, "%s", U->user.first_name);
    } else {
      mprintf (ev, "%s %s", U->user.first_name, U->user.last_name); 
    }
    if (U->id == TLS->my_id || U->user.my_link == tdl_user_link_state_contact) {
      mpop_color (ev);
    }
  }
  mpop_color (ev);
}
*/

//void print_chat_name (struct in_ev *ev, tgl_peer_id_t id, union tdl_chat *C) {
//  return;
  /*assert (tgl_get_peer_type (id) == TGL_PEER_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (permanent_peer_id_mode) {
    print_peer_permanent_name (ev, id);
    mpop_color (ev);
    return;
  }
  if (!C || use_ids) {
    mprintf (ev, "chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->chat.title);
  }
  mpop_color (ev);*/
//}

void print_encr_chat_name (struct in_ev *ev, tgl_peer_id_t id, union tdl_chat *C) {
  return;
  /*assert (tgl_get_peer_type (id) == TGL_PEER_ENCR_CHAT);
  mpush_color (ev, COLOR_MAGENTA);
  if (permanent_peer_id_mode) {
    print_peer_permanent_name (ev, id);
    mpop_color (ev);
    return;
  }
  if (!C || use_ids) {
    mprintf (ev, "encr_chat#%d", tgl_get_peer_id (id));
  } else {
    mprintf (ev, "%s", C->print_name);
  }
  mpop_color (ev);*/
}

void print_peer_name  (struct in_ev *ev, tgl_peer_id_t id, union tdl_chat *C) {
  return;
  /*
  switch (tgl_get_peer_type (id)) {
  case TGL_PEER_USER:
    print_user_name (ev, id, C);
    return;
  case TGL_PEER_CHAT:
    print_chat_name (ev, id, C);
    return;
  case TGL_PEER_CHANNEL:
    print_channel_name (ev, id, C);
    return;
  case TGL_PEER_ENCR_CHAT:
    print_encr_chat_name (ev, id, C);
    return;
  default:
    assert (0);
  }*/
}

static char *monthes[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
void print_date (struct in_ev *ev, long t) {
  struct tm *tm = localtime ((void *)&t);
  if (time (0) - t < 12 * 60 * 60) {
    mprintf (ev, "[%02d:%02d] ", tm->tm_hour, tm->tm_min);
  } else if (time (0) - t < 24 * 60 * 60 * 180) {
    mprintf (ev, "[%02d %s]", tm->tm_mday, monthes[tm->tm_mon]);
  } else {
    mprintf (ev, "[%02d %s %d]", tm->tm_mday, monthes[tm->tm_mon], tm->tm_year + 1900);
  }
}

void print_date_full (struct in_ev *ev, long t) {
  struct tm *tm = localtime ((void *)&t);
  mprintf (ev, "[%04d/%02d/%02d %02d:%02d:%02d]", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void print_msg_id (struct in_ev *ev, tdl_message_id_t msg_id, struct tdl_message *M) {
  return;
  /*
  if (msg_num_mode) {
    if (!permanent_msg_id_mode) {
      if (M) {
        mprintf (ev, "%d", M->temp_id);
      } else {
        mprintf (ev, "???");
      }
    } else {
      mprintf (ev, "%s", print_permanent_msg_id (msg_id));
    }
  }*/
}

void print_service_message (struct in_ev *ev, struct tdl_message *M) {
  return;
  /*
  assert (M);
  //print_start ();
  mpush_color (ev, COLOR_GREY);

  if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHANNEL) {
    mpush_color (ev, COLOR_CYAN);
  } else {
    mpush_color (ev, COLOR_MAGENTA);
  }
  print_msg_id (ev, M->permanent_id, M);
  mprintf (ev, " ");
  print_date (ev, M->date);
  mpop_color (ev);
  mprintf (ev, " ");
  if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHAT) {
    print_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  } else if (tgl_get_peer_type (M->to_id) == TGL_PEER_CHANNEL) {
    print_channel_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  } else {
    assert (tgl_get_peer_type (M->to_id) == TGL_PEER_ENCR_CHAT);
    print_encr_chat_name (ev, M->to_id, tgl_peer_get (TLS, M->to_id));
  }

  if (tgl_get_peer_type (M->from_user_id) == TGL_PEER_USER) {
    mprintf (ev, " ");
    print_user_name (ev, M->from_user_id, tgl_peer_get (TLS, M->from_user_id));
  }
 
  switch (M->action.type) {
  case tdl_message_action_none:
    mprintf (ev, "\n");
    break;
  case tdl_message_action_geo_chat_create:
    mprintf (ev, "Created geo chat\n");
    break;
  case tdl_message_action_geo_chat_checkin:
    mprintf (ev, "Checkin in geochat\n");
    break;
  case tdl_message_action_chat_create:
    mprintf (ev, " created chat %s. %d users\n", M->action.title, M->action.user_num);
    break;
  case tdl_message_action_chat_edit_title:
    mprintf (ev, " changed title to %s\n", 
      M->action.new_title);
    break;
  case tdl_message_action_chat_edit_photo:
    mprintf (ev, " changed photo\n");
    break;
  case tdl_message_action_chat_delete_photo:
    mprintf (ev, " deleted photo\n");
    break;
  case tdl_message_action_chat_add_users:
    mprintf (ev, " added users:");
    {
      int i;
      for (i = 0; i < M->action.user_num; i++) {
        print_user_name (ev, tgl_set_peer_id (TGL_PEER_USER, M->action.users[i]), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.users[i])));
      }
    }
    mprintf (ev, "\n");
    break;
  case tdl_message_action_chat_add_user_by_link:
    mprintf (ev, " added by link from ");
    print_user_name (ev, tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    mprintf (ev, "\n");
    break;
  case tdl_message_action_chat_delete_user:
    mprintf (ev, " deleted user ");
    print_user_name (ev, tgl_set_peer_id (TGL_PEER_USER, M->action.user), tgl_peer_get (TLS, tgl_set_peer_id (TGL_PEER_USER, M->action.user)));
    mprintf (ev, "\n");
    break;
  case tdl_message_action_set_message_ttl:
    mprintf (ev, " set ttl to %d seconds. Unsupported yet\n", M->action.ttl);
    break;
  case tdl_message_action_read_messages:
    mprintf (ev, " %d messages marked read\n", M->action.read_cnt);
    break;
  case tdl_message_action_delete_messages:
    mprintf (ev, " %d messages deleted\n", M->action.delete_cnt);
    break;
  case tdl_message_action_screenshot_messages:
    mprintf (ev, " %d messages screenshoted\n", M->action.screenshot_cnt);
    break;
  case tdl_message_action_flush_history:
    mprintf (ev, " cleared history\n");
    break;
  case tdl_message_action_resend:
    mprintf (ev, " resend query\n");
    break;
  case tdl_message_action_notify_layer:
    mprintf (ev, " updated layer to %d\n", M->action.layer);
    break;
  case tdl_message_action_typing:
    mprintf (ev, " is ");
    print_typing (ev, M->action.typing);
    break;
  case tdl_message_action_noop:
    mprintf (ev, " noop\n");
    break;
  case tdl_message_action_request_key:
    mprintf (ev, " request rekey #%016llx\n", M->action.exchange_id);
    break;
  case tdl_message_action_accept_key:
    mprintf (ev, " accept rekey #%016llx\n", M->action.exchange_id);
    break;
  case tdl_message_action_commit_key:
    mprintf (ev, " commit rekey #%016llx\n", M->action.exchange_id);
    break;
  case tdl_message_action_abort_key:
    mprintf (ev, " abort rekey #%016llx\n", M->action.exchange_id);
    break;
  case tdl_message_action_channel_create:
    mprintf (ev, " created channel %s\n", M->action.title);
    break;
  case tdl_message_action_migrated_to:
    mprintf (ev, " migrated to channel\n");
    break;
  case tdl_message_action_migrated_from:
    mprintf (ev, " migrated from group '%s'\n", M->action.title);
    break;
  }
  mpop_color (ev);
  //print_end ();*/
}

tgl_peer_id_t last_from_user_id;
tgl_peer_id_t last_to_id;

void print_user_status (struct in_ev *ev, struct tdl_user_status *S) {
  switch (S->type) {
  case tdl_user_status_empty:
    mprintf (ev, "offline");
    break;
  case tdl_user_status_online:
    mprintf (ev, "online");
    break;
  case tdl_user_status_offline:
    mprintf (ev, "offline (was online at ");
    print_date (ev, S->when);
    mprintf (ev, ")");
    break;
  case tdl_user_status_recently:
    mprintf (ev, "recently");
    break;
  case tdl_user_status_last_week:
    mprintf (ev, "last week");
    break;
  case tdl_user_status_last_month:
    mprintf (ev, "last month");
    break;
  }
}

void print_user_name (struct in_ev *ev, struct tdl_user *U, int id) { 
  if (U && U->my_link == tdl_user_link_state_contact) {
    mpush_color (ev, COLOR_REDB);
  } else {
    mpush_color (ev, COLOR_RED);
  }

  if (U && (U->first_name || U->last_name)) {
    if (U->first_name) {
      if (U->last_name) {
        mprintf (ev, "%s %s", U->first_name, U->last_name);
      } else {
        mprintf (ev, "%s", U->first_name);
      }
    } else {
      mprintf (ev, "%s", U->last_name);
    }
  } else {
    mprintf (ev, "user#%d", id);
  }
  
  mpop_color (ev);
}

void print_chat_name (struct in_ev *ev, struct tdl_chat_info *C, long long id) {
  if (!C) {
    mpush_color (ev, COLOR_RED);
    mprintf (ev, "unknown#%lld", id);
    mpop_color (ev);
  } else {   
    switch (C->chat->type) {
      case tdl_chat_type_user:
        print_user_name (ev, &C->chat->user, C->chat->user.id);
        return;
      case tdl_chat_type_group:
        mpush_color (ev, COLOR_MAGENTA);
        mprintf (ev, "%s", C->title);
        mpop_color (ev);
        break;
      case tdl_chat_type_channel:
        if (C->chat->channel.is_supergroup) {
          mpush_color (ev, COLOR_MAGENTA);
        } else {
          mpush_color (ev, COLOR_CYAN);
        }
        mprintf (ev, "%s", C->title);
        mpop_color (ev);
        break;
      case tdl_chat_type_secret_chat:
        mpush_color (ev, COLOR_LCYAN);
        mprintf (ev, "%s", C->title);
        mpop_color (ev);
        break;
    }
  }
}

void print_animation (struct in_ev *ev, struct tdl_animation *animation) {
  mprintf (ev, "[animation %d", animation->file->id);
  if (animation->file_name) {
    mprintf (ev, " name=%s", animation->file_name);
  }
  if (animation->mime_type) {
    mprintf (ev, " type=%s", animation->mime_type);
  }
  if (animation->width && animation->height) {
    mprintf (ev, " size=%dx%d", animation->width, animation->height);
  }
  int size = animation->file->size;

  mprintf (ev, " size=");
  if (size < (1 << 10)) {
    mprintf (ev, "%dB", size);
  } else if (size < (1 << 20)) {
    mprintf (ev, "%dKiB", size >> 10);
  } else if (size < (1 << 30)) {
    mprintf (ev, "%dMiB", size >> 20);
  } else {
    mprintf (ev, "%dGiB", size >> 30);
  }

  mprintf (ev, "]");
}

void print_audio (struct in_ev *ev, struct tdl_audio *audio) {
  mprintf (ev, "[audio %d", audio->file->id);
  if (audio->file_name) {
    mprintf (ev, " name=%s", audio->file_name);
  }
  if (audio->mime_type) {
    mprintf (ev, " type=%s", audio->mime_type);
  }
  if (audio->title) {
    mprintf (ev, " title=%s", audio->title);
  }
  if (audio->performer) {
    mprintf (ev, " artist=%s", audio->performer);
  }
  int size = audio->file->size;

  mprintf (ev, " size=");
  if (size < (1 << 10)) {
    mprintf (ev, "%dB", size);
  } else if (size < (1 << 20)) {
    mprintf (ev, "%dKiB", size >> 10);
  } else if (size < (1 << 30)) {
    mprintf (ev, "%dMiB", size >> 20);
  } else {
    mprintf (ev, "%dGiB", size >> 30);
  }

  mprintf (ev, "]");
}

void print_document (struct in_ev *ev, struct tdl_document *document) {
  mprintf (ev, "[document %d", document->file->id);
  if (document->file_name) {     
    mprintf (ev, " name=%s", document->file_name);
  }
  if (document->mime_type) {
    mprintf (ev, " type=%s", document->mime_type);
  }
  int size = document->file->size;

  mprintf (ev, " size=");
  if (size < (1 << 10)) {
    mprintf (ev, "%dB", size);
  } else if (size < (1 << 20)) {
    mprintf (ev, "%dKiB", size >> 10);
  } else if (size < (1 << 30)) {
    mprintf (ev, "%dMiB", size >> 20);
  } else {
    mprintf (ev, "%dGiB", size >> 30);
  }

  mprintf (ev, "]");
}

void print_photo (struct in_ev *ev, struct tdl_photo *photo) {
  mprintf (ev, "[photo");
  if (photo->sizes_cnt > 0) {
    struct tdl_photo_size *s = photo->sizes[photo->sizes_cnt - 1];

    int j;
    for (j = 0; j < photo->sizes_cnt; j++) {
      struct tdl_photo_size *t = photo->sizes[j];
      if (t->width != 0 || t->height != 0) {
        mprintf (ev, " [photo_size %d size=%dx%d]", t->file->id, t->width, t->height);
      }
    }

    int size = s->file->size;

    mprintf (ev, " size=");
    if (size < (1 << 10)) {
      mprintf (ev, "%dB", size);
    } else if (size < (1 << 20)) {
      mprintf (ev, "%dKiB", size >> 10);
    } else if (size < (1 << 30)) {
      mprintf (ev, "%dMiB", size >> 20);
    } else {
      mprintf (ev, "%dGiB", size >> 30);
    }
  }

  mprintf (ev, "]");
}

void print_sticker (struct in_ev *ev, struct tdl_sticker *sticker) {
  mprintf (ev, "[sticker %d", sticker->file->id);
  if (sticker->emoji) {
    mprintf (ev, " emoji=%s", sticker->emoji);
  }
  mprintf (ev, " set_id=%lld", sticker->set_id);
  mprintf (ev, " rating=%.3lf", sticker->rating);
  if (sticker->width && sticker->height) {
    mprintf (ev, " size=%dx%d", sticker->width, sticker->height);
  }

  int size = sticker->file->size;

  mprintf (ev, " size=");
  if (size < (1 << 10)) {
    mprintf (ev, "%dB", size);
  } else if (size < (1 << 20)) {
    mprintf (ev, "%dKiB", size >> 10);
  } else if (size < (1 << 30)) {
    mprintf (ev, "%dMiB", size >> 20);
  } else {
    mprintf (ev, "%dGiB", size >> 30);
  }

  mprintf (ev, "]");
}

void print_video (struct in_ev *ev, struct tdl_video *video) {
  mprintf (ev, "[video %d", video->file->id);
  if (video->file_name) {     
    mprintf (ev, " name=%s", video->file_name);
  }
  if (video->mime_type) {
    mprintf (ev, " type=%s", video->mime_type);
  }
  if (video->height && video->width) {
    mprintf (ev, " size=%dx%d", video->width, video->height);
  }
  if (video->duration) {
    mprintf (ev, " duration=%d", video->duration);
  }
  int size = video->file->size;

  mprintf (ev, " size=");
  if (size < (1 << 10)) {
    mprintf (ev, "%dB", size);
  } else if (size < (1 << 20)) {
    mprintf (ev, "%dKiB", size >> 10);
  } else if (size < (1 << 30)) {
    mprintf (ev, "%dMiB", size >> 20);
  } else {
    mprintf (ev, "%dGiB", size >> 30);
  }

  mprintf (ev, "]");
}

void print_voice (struct in_ev *ev, struct tdl_voice *voice) {
  mprintf (ev, "[voice %d", voice->file->id);
  if (voice->file_name) {     
    mprintf (ev, " name=%s", voice->file_name);
  }
  if (voice->mime_type) {
    mprintf (ev, " type=%s", voice->mime_type);
  }
  if (voice->duration) {
    mprintf (ev, " duration=%d", voice->duration);
  }
  int size = voice->file->size;

  mprintf (ev, " size=");
  if (size < (1 << 10)) {
    mprintf (ev, "%dB", size);
  } else if (size < (1 << 20)) {
    mprintf (ev, "%dKiB", size >> 10);
  } else if (size < (1 << 30)) {
    mprintf (ev, "%dMiB", size >> 20);
  } else {
    mprintf (ev, "%dGiB", size >> 30);
  }

  mprintf (ev, "]");
}

void print_venue (struct in_ev *ev, struct tdl_message_content_venue *C) {
  mprintf (ev, "[venue https://maps.google.com/?q=%.6lf,%.6lf", C->latitude, C->longitude);
  if (C->title) {
    mprintf (ev, "title=%s", C->title);
  }
  if (C->address) {
    mprintf (ev, "address=%s", C->address);
  }
  mprintf (ev, "]");
}

void print_contact (struct in_ev *ev, struct tdl_message_content_contact *C) {
  mprintf (ev, "[contact %s", C->phone);
  if (C->first_name) {
    mprintf (ev, " %s", C->first_name);
  }
  if (C->last_name) {
    mprintf (ev, " %s", C->last_name);
  }
  if (C->user_id) {
    struct tdl_user *U = tdlib_instant_get_user (TLS, C->user_id);
    if (U) {
      mprintf (ev, " ");
      print_user_name (ev, U, U->id);
    }
  }
  mprintf (ev, "]");
}

void print_media (struct in_ev *ev, struct tdl_message_content_media *C) {
  union tdl_message_media *M = C->media;
  assert (M);
  if (C->caption) {
    mprintf (ev, "%s ", C->caption);
  }
  switch (M->type) {
  case tdl_media_animation:
    print_animation (ev, &M->animation);
    break;
  case tdl_media_audio:
    print_audio (ev, &M->audio);
    break;
  case tdl_media_document:
    print_document (ev, &M->document);
    break;
  case tdl_media_sticker:
    print_sticker (ev, &M->sticker);
    break;
  case tdl_media_photo:
    print_photo (ev, &M->photo);
    break;
  case tdl_media_video:
    print_video (ev, &M->video);
    break;
  case tdl_media_voice:
    print_voice (ev, &M->voice);
    break;
  }
}

void print_members (struct in_ev *ev, int members_count, struct tdl_user **members) {
  mprintf (ev, "%d users:", members_count);
  int i;
  for (i = 0; i < members_count; i++) {
    mprintf (ev, " ");
    print_user_name (ev, members[i], members[i]->id);
  }
}

void print_message_action (struct in_ev *ev, union tdl_message_action *action) {
  mpush_color (ev, COLOR_YELLOW);
  switch (action->action) {
  case tdl_message_action_type_group_create:
    mprintf (ev, "Created group '%s' ", action->group_create.title);
    print_members (ev, action->group_create.members_cnt, 
      action->group_create.members);
    break;
  case tdl_message_action_type_channel_create:
    mprintf (ev, "Created channel %s", action->channel_create.title);
    break;
  case tdl_message_action_type_chat_change_title:
    mprintf (ev, "renamed to %s", action->change_title.title);
    break;
  case tdl_message_action_type_chat_change_photo:
    mprintf (ev, "changed photo");
    break;
  case tdl_message_action_type_chat_delete_photo:
    mprintf (ev, "deleted photo");
    break;
  case tdl_message_action_type_chat_add_members:
    mprintf (ev, "added ");
    print_members (ev, action->add_members.members_cnt, 
      action->add_members.members);
    break;
  case tdl_message_action_type_chat_join_by_link:
    {
      mprintf (ev, "joined by link");
      struct tdl_user *U = tdlib_instant_get_user (TLS, action->join_by_link.inviter_user_id);
      if (U) {
        mprintf (ev, " by ");
        print_user_name (ev, U, U->id);
      }
    }
    break;
  case tdl_message_action_type_chat_delete_member:
    {
      print_user_name (ev, action->delete_member.user, action->delete_member.user->id);
      mprintf (ev, " deleted");
    }
    break;
  case tdl_message_action_type_chat_migrate_to:
    {
      mprintf (ev, " migrated to channel");
    }
    break;
  case tdl_message_action_type_chat_migrate_from:
    {
      mprintf (ev, " migrated from group");
    }
    break;
  case tdl_message_action_type_pin_message:
    {
      mprintf (ev, " pinned message");
    }
    break;
  }
  mpop_color (ev);
}

void print_message_id (struct in_ev *ev, struct tdl_chat_info *C, int id) {
  if (permanent_msg_id_mode) {
    /*int s[3];
    s[0] = C->chat->type;
    s[1] = C->chat->id;
    s[2] = id;*/
    switch (C->chat->type) {
    case tdl_chat_type_user:
      mprintf (ev, "user#id%d@%d ", C->chat->user.id, id);
      break;
    case tdl_chat_type_group:
      mprintf (ev, "group#id%d@%d ", C->chat->group.id, id);
      break;
    case tdl_chat_type_channel:
      mprintf (ev, "channel#id%d@%d ", C->chat->channel.id, id);
      break;
    case tdl_chat_type_secret_chat:
      mprintf (ev, "secret_chat#id%d@%d ", C->chat->secret_chat.id, id);
      break;
    }
  } else {
    mprintf (ev, "%d", convert_global_to_local (C->id, id)->local_id);
  }
}

void print_message (struct in_ev *ev, struct tdl_message *M) {
  assert (M);
  if (M->content->type == tdl_message_content_type_deleted) {
    return;
  }    

  struct tdl_chat_info *C = tdlib_instant_get_chat (TLS, M->chat_id);
  struct tdl_user *U = tdlib_instant_get_user (TLS, M->sender_user_id);

  if (M->sender_user_id == TLS->my_id) {
    mpush_color (ev, COLOR_GREEN);
  } else {
    mpush_color (ev, COLOR_BLUE);
  }
  print_date (ev, M->date);
  mprintf (ev, " ");
  print_message_id (ev, C, M->id);
  mprintf (ev, " ");

  print_chat_name (ev, C, M->chat_id);

  if (M->sender_user_id > 0 && (!C || C->chat->type != tdl_chat_type_user)) {
    mprintf (ev, " ");
    print_user_name (ev, U, M->sender_user_id);
  }

  if (M->sender_user_id == TLS->my_id) {
    if (C && M->id <= C->last_read_outbox_message_id) {
      mprintf (ev, " ««« "); 
    } else {
      mprintf (ev, " <<< "); 
    }
  } else {
    if (C && M->id <= C->last_read_inbox_message_id) {
      mprintf (ev, " »»» ");
    } else {
      mprintf (ev, " >>> ");
    }
  }

  if (M->forward_info) {
    mprintf (ev, "[fwd ");  
    if (M->forward_info->chat_id) {
      struct tdl_chat_info *C = tdlib_instant_get_chat (TLS, M->forward_info->chat_id);
      print_chat_name (ev, C, M->forward_info->chat_id);
      mprintf (ev, " ");
    }
    if (M->forward_info->user_id) {
      struct tdl_user *U = tdlib_instant_get_user (TLS, M->forward_info->user_id);
      if (!U) {
        mprintf (ev, "user#id%d", M->forward_info->user_id);
      } else {
        print_user_name (ev, U, U->id);
      }
      mprintf (ev, " ");
    }

    print_date (ev, M->forward_info->date);
    mprintf (ev, "] ");
  }
  
  if (M->reply_to_message_id) {
    mprintf (ev, "[reply ");
    print_message_id (ev, C, M->reply_to_message_id);    
    mprintf (ev, "] ");    
  }

  if (M->via_bot_user_id) {
    struct tdl_user *U = tdlib_instant_get_user (TLS, M->via_bot_user_id);
    if (U && U->username) {
      mprintf (ev, "[via @%s] ", U->username);
    }
  }

  switch (M->content->type) {
  case tdl_message_content_type_text:
    mprintf (ev, "%s", M->content->text.text);
    if (M->content->text.web_page) {
      mprintf (ev, " [webpage %s: %s]", 
        M->content->text.web_page->title,
        M->content->text.web_page->description
      );
    }  
    break;
  case tdl_message_content_type_media:
    print_media (ev, &M->content->media);
    break;
  case tdl_message_content_type_venue:
    print_venue (ev, &M->content->venue);
    break;
  case tdl_message_content_type_contact:
    print_contact (ev, &M->content->contact);
    break;
  case tdl_message_content_type_deleted:
    mprintf (ev, "[deleted]");
    break;
  case tdl_message_content_type_unsupported:
    mprintf (ev, "[unsupported]");
    break;
  case tdl_message_content_type_action:
    print_message_action (ev, &M->content->action);
    break;
  }
  
  mpop_color (ev);
  assert (!color_stack_pos);
  mprintf (ev, "\n");
}

void play_sound (void) {
  printf ("\a");
}

void set_interface_callbacks (void) {
  if (readline_disabled) { return; }
  readline_active = 1;
  rl_filename_quote_characters = strdup (" ");
  rl_basic_word_break_characters = strdup (" ");
  
  
  rl_callback_handler_install (get_default_prompt (), interpreter);
  rl_completion_entry_function = command_generator;
}
