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
#include <termios.h>

#ifdef READLINE_GNU
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <editline/readline.h>
#endif

#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <time.h>

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
#  include <lua.h>
#  include "lua-tg.h"
#endif

#ifdef HAVE_LIBCONFIG
#include <libconfig.h>
#endif


//#include "mtproto-common.h"

#include "loop.h"

//#include "td/libtd/src/main/jni/td/telegram/td_c_client.h"
#include "telegram-layout.h"


#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __APPLE__
#define OPEN_BIN "open '%s'"
#else
#define OPEN_BIN "xdg-open '%s'"
#endif

#include <errno.h>

#ifdef USE_JSON
#include "json-tg.h"
#endif

#include "auto/git_info.h"

#include "tree.h"

struct pending_message {
  long long chat_id;
  long long id;
  struct in_command *cmd;
};

static inline int pending_message_cmp (struct pending_message *a, struct pending_message *b) {
  if (a->id < b->id) { return -1; }
  if (a->id > b->id) { return 1; }
  if (a->chat_id < b->chat_id) { return -1; }
  if (a->chat_id > b->chat_id) { return 1; }
  return 0;
}

DEFINE_TREE (pending_message, struct pending_message *, pending_message_cmp, NULL);
struct tree_pending_message *pending_messages;

int allocated_commands;

extern struct event_base *ev_base;

int total_unread;
int disable_msg_preview;
int my_id;
int need_prompt_update;
int safe_quit;

char *conn_state;

struct delayed_query {
  struct in_command *cmd;
  //char *line;
  //struct in_ev *ev;

  char *token;
  int action;
  int mode;
};

struct tdcli_peer {
  long long chat_id;
  int peer_type;
  int peer_id;
  struct TdChat *chat;
  union {
    struct TdUser *user;
    struct TdGroup *group;
    struct TdChannel *channel;
    struct TdSecretChat *secret_chat;
  };
  union {
    struct TdUserFull *user_full;
    struct TdGroupFull *group_full;
    struct TdChannelFull *channel_full;
    struct TdSecretChatFull *secret_chat_full;
  };
  
  char *main_alias;
  char *name_alias;
  char *username_alias;

  int aliases_cnt;
  char **aliases;

  int unread_count;
};

static int tdcli_peer_cmp_chat_id (struct tdcli_peer *a, struct tdcli_peer *b) {
  if (a->chat_id < b->chat_id) { return -1; }
  if (a->chat_id > b->chat_id) { return 1; }
  return 0;
}

static int tdcli_peer_cmp_peer_id (struct tdcli_peer *a, struct tdcli_peer *b) {
  if (a->peer_id < b->peer_id) { return -1; }
  if (a->peer_id > b->peer_id) { return 1; }
  if (a->peer_type < b->peer_type) { return -1; }
  if (a->peer_type > b->peer_type) { return 1; }
  return 0;
}
DEFINE_TREE (chat_peer, struct tdcli_peer *, tdcli_peer_cmp_chat_id, NULL);
DEFINE_TREE (peer_chat, struct tdcli_peer *, tdcli_peer_cmp_peer_id, NULL);

struct tree_chat_peer *tdcli_chats;
struct tree_peer_chat *tdcli_peers;

struct TdChat *get_chat (long long chat_id) { 
  struct tdcli_peer PB;
  PB.chat_id = chat_id;
  struct tdcli_peer *P = tree_lookup_chat_peer (tdcli_chats, &PB);
  return P ? P->chat : NULL;
}

struct TdChat *get_peer_chat (int peer_type, int peer_id) { 
  struct tdcli_peer PB;
  PB.peer_type = peer_type;
  PB.peer_id = peer_id;
  struct tdcli_peer *P = tree_lookup_peer_chat (tdcli_peers, &PB);
  return P ? P->chat : NULL;
}

struct tdcli_peer *get_peer (long long chat_id) {
  struct tdcli_peer PB;
  PB.chat_id = chat_id;
  return tree_lookup_chat_peer (tdcli_chats, &PB);
}

struct TdUser *get_user (int user_id) { 
  struct tdcli_peer PB;
  PB.peer_type = CODE_User;
  PB.peer_id = user_id;
  struct tdcli_peer *P = tree_lookup_peer_chat (tdcli_peers, &PB);
  return P ? P->user : NULL;
}

struct TdUserFull *get_user_full (int user_id) { 
  struct tdcli_peer PB;
  PB.peer_type = CODE_User;
  PB.peer_id = user_id;
  struct tdcli_peer *P = tree_lookup_peer_chat (tdcli_peers, &PB);
  return P ? P->user_full : NULL;
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

struct tdcli_peer *peer_update_chat (struct TdChat *C) {
  struct tdcli_peer PB;
  PB.chat_id = C->id_;
  struct tdcli_peer *P = tree_lookup_chat_peer (tdcli_chats, &PB);
  if (!P) {
    switch ((enum List_ChatInfo)C->type_->ID) {    
    case CODE_PrivateChatInfo:
      {
        struct TdPrivateChatInfo *I = (void *)C->type_;
        PB.peer_type = CODE_User;
        PB.peer_id = I->user_->id_;
      }
      break;
    case CODE_GroupChatInfo:
      {
        struct TdGroupChatInfo *I = (void *)C->type_;
        PB.peer_type = CODE_Group;
        PB.peer_id = I->group_->id_;
      }
      break;
    case CODE_ChannelChatInfo:
      {
        struct TdChannelChatInfo *I = (void *)C->type_;
        PB.peer_type = CODE_Channel;
        PB.peer_id = I->channel_->id_;
      }
      break;
    case CODE_SecretChatInfo:
      {
        struct TdSecretChatInfo *I = (void *)C->type_;
        PB.peer_type = CODE_SecretChat;
        PB.peer_id = I->secret_chat_->id_;
      }
      break;
    //default:
    //  assert (0);
    }

    P = tree_lookup_peer_chat (tdcli_peers, &PB);
    if (P) {
      assert (!P->chat_id);
      P->chat_id = C->id_;      
      tdcli_chats = tree_insert_chat_peer (tdcli_chats, P, rand ());
    } else {
      P = calloc (sizeof (*P), 1);
      P->chat_id = PB.chat_id;
      P->peer_type = PB.peer_type;
      P->peer_id = PB.peer_id;
      tdcli_chats = tree_insert_chat_peer (tdcli_chats, P, rand ());
      tdcli_peers = tree_insert_peer_chat (tdcli_peers, P, rand ());
    }
  }

  __sync_fetch_and_add (&C->refcnt, 1);
  if (P->chat) {
    TdDestroyObjectChat (P->chat);
  }
  P->chat = C;

  switch ((enum List_ChatInfo)C->type_->ID) {
    case CODE_PrivateChatInfo:
      {        
        struct TdPrivateChatInfo *I = (void *)C->type_;
        __sync_fetch_and_add (&P->user->refcnt, 1);
        if (P->user) {
          TdDestroyObjectUser (P->user);
        }
        P->user = I->user_;
      }
      break;
    case CODE_GroupChatInfo:
      {
        struct TdGroupChatInfo *I = (void *)C->type_;
        __sync_fetch_and_add (&P->group->refcnt, 1);
        if (P->group) {
          TdDestroyObjectGroup (P->group);
        }
        P->group = I->group_;
      }
      break;
    case CODE_ChannelChatInfo:
      {
        struct TdChannelChatInfo *I = (void *)C->type_;
        __sync_fetch_and_add (&P->channel->refcnt, 1);
        if (P->channel) {
          TdDestroyObjectChannel (P->channel);
        }
        P->channel = I->channel_;
      }
      break;
    case CODE_SecretChatInfo:
      {
        struct TdSecretChatInfo *I = (void *)C->type_;
        __sync_fetch_and_add (&P->secret_chat->refcnt, 1);
        if (P->secret_chat) {
          TdDestroyObjectSecretChat (P->secret_chat);
        }
        P->secret_chat = I->secret_chat_;
      }
      break;
    //default:
    //  assert (0);
  }

  return P;
}

struct tdcli_peer *peer_update_peer (struct TdNullaryObject *U, int id) {
  struct tdcli_peer PB;
  PB.peer_type = U->ID;
  PB.peer_id = id;
  assert (U->refcnt > 0);

  struct tdcli_peer *P = tree_lookup_peer_chat (tdcli_peers, &PB);
  if (!P) {
    P = calloc (sizeof (*P), 1);
    P->peer_type = PB.peer_type;
    P->peer_id = PB.peer_id;
    tdcli_peers = tree_insert_peer_chat (tdcli_peers, P, rand ());
  }

  __sync_fetch_and_add (&U->refcnt, 1);
  if (P->user) {
    TdDestroyObjectNullaryObject ((void *)P->user);
  }
  P->user = (void *)U;
  return P;
}

#define chat_alias_cmp(a,b) strcmp (a->name, b->name)
DEFINE_TREE (chat_alias, struct chat_alias *, chat_alias_cmp, NULL)

struct tree_chat_alias *alias_tree;
struct chat_alias alias_queue;

void add_alias (const char *name, struct tdcli_peer *peer) {
  if (alias_queue.next == NULL) {
    alias_queue.next = alias_queue.prev = &alias_queue;
  }
  struct chat_alias *alias = malloc (sizeof (*alias));
  alias->name = strdup (name);
  alias->peer = peer;
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

void add_alias_internal (struct tdcli_peer *e, char *alias) {
  if (!alias) { return; }
  e->aliases_cnt ++;
  e->aliases = realloc (e->aliases, sizeof (void *) * (e->aliases_cnt));
  e->aliases[e->aliases_cnt - 1] = strdup (alias);

  add_alias (alias, e);
}

void del_alias_internal (struct tdcli_peer *e, char *alias) {
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

void sub_alias_internal (struct tdcli_peer *e, char *alias, char *new_alias) {
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
      add_alias (new_alias, e);
      return;
    }
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

char *generate_alias_title (struct tdcli_peer *e, const char *title) {
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
    if (A->peer == e) {
      break;
    }
    sprintf (s + l, "#%d", ++p);
  }
  return s;
}

char *generate_alias_name (struct tdcli_peer *e, char *first_name, char *last_name) {
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

void on_user_update (struct TdUser *U) {
  assert (U->ID == CODE_User);
  struct tdcli_peer *P = peer_update_peer ((void *)U, U->id_);
    
  if (!P->main_alias) {
    char s[20];
    sprintf (s, "user#id%d", U->id_);
    P->main_alias = strdup (s);
    add_alias_internal (P, s);
  }
  
  char *u;
  
  u = generate_alias_username (U->username_);
  sub_alias_internal (P, P->username_alias, u);
  upd_str (&P->username_alias, u);
    
  if (!P->chat) {
    u = generate_alias_name (P, U->first_name_, U->last_name_);
    sub_alias_internal (P, P->name_alias, u);
    upd_str (&P->name_alias, u);
  }
}

void on_group_update (struct TdGroup *G) {
  assert (G->ID == CODE_Group);
  struct tdcli_peer *P = peer_update_peer ((void *)G, G->id_);
    
  if (!P->main_alias) {
    char s[20];
    sprintf (s, "group#id%d", G->id_);
    P->main_alias = strdup (s);
    add_alias_internal (P, s);
  }
}

void on_channel_update (struct TdChannel *Ch) {
  assert (Ch->ID == CODE_Channel);
  struct tdcli_peer *P = peer_update_peer ((void *)Ch, Ch->id_);
    
  if (!P->main_alias) {
    char s[20];
    sprintf (s, "channel#id%d", Ch->id_);
    P->main_alias = strdup (s);
    add_alias_internal (P, s);
  }
  
  char *u;  
  u = generate_alias_username (Ch->username_);
  sub_alias_internal (P, P->username_alias, u);
  upd_str (&P->username_alias, u);
}

void on_secret_chat_update (struct TdSecretChat *S) {
  struct tdcli_peer *P = peer_update_peer ((void *)S, S->id_);
    
  if (!P->main_alias) {
    char s[20];
    sprintf (s, "secret_chat#id%d", S->id_);
    P->main_alias = strdup (s);
    add_alias_internal (P, s);
  }
}

void on_chat_update (struct TdChat *C) {
  assert (C->ID == CODE_Chat);
  switch ((enum List_ChatInfo)C->type_->ID) {
  case CODE_PrivateChatInfo:
    {
      struct TdPrivateChatInfo *I = (void *)C->type_;
      on_user_update (I->user_);
    }
    break;
  case CODE_GroupChatInfo:
    {
      struct TdGroupChatInfo *I = (void *)C->type_;
      on_group_update (I->group_);
    }
    break;
  case CODE_ChannelChatInfo:
    {
      struct TdChannelChatInfo *I = (void *)C->type_;
      on_channel_update (I->channel_);
    }
    break;
  case CODE_SecretChatInfo:
    {
      struct TdSecretChatInfo *I = (void *)C->type_;
      on_secret_chat_update (I->secret_chat_);
    }
    break;
    
  }

  struct tdcli_peer *P = peer_update_chat (C);
  assert (P);
  
  char *u;
  u = generate_alias_title (P, C->title_);
  sub_alias_internal (P, P->name_alias, u);
  upd_str (&P->name_alias, u);

  total_unread += C->unread_count_ - P->unread_count;
  P->unread_count = C->unread_count_;
}


/* {{{ MESSAGE ALIASES */
struct message_alias {
  int local_id;
  long long message_id;
  long long chat_id;

  struct TdMessage *message;
};
#define message_alias_cmp_local(a,b) (a->local_id - b->local_id)
#define message_alias_cmp_global(a,b) memcmp(&a->message_id, &b->message_id, 16)
DEFINE_TREE (message_alias_local, struct message_alias *, message_alias_cmp_local, NULL)
DEFINE_TREE (message_alias_global, struct message_alias *, message_alias_cmp_global, NULL)

struct tree_message_alias_local *message_local_tree;
struct tree_message_alias_global *message_global_tree;
int message_local_last_id;

struct message_alias *convert_local_to_global (int local_id) {
  struct message_alias M;
  M.local_id = local_id;
  return tree_lookup_message_alias_local (message_local_tree, &M);
}

struct message_alias *convert_global_to_local (long long chat_id, long long message_id) {
  struct message_alias M;
  M.chat_id = chat_id;
  M.message_id = message_id;

  struct message_alias *A = tree_lookup_message_alias_global (message_global_tree, &M);
  if (A) { return A; }
  A = malloc (sizeof (*A));
  A->local_id = ++ message_local_last_id;
  A->chat_id = chat_id;
  A->message_id = message_id;
  A->message = NULL;
  return A;
}

struct TdMessage *get_message (long long chat_id, long long message_id) {
  struct message_alias *A = convert_global_to_local (chat_id, message_id);
  return A->message;
}

void on_message_update (struct TdMessage *MD) {
  struct message_alias M;
  M.chat_id = MD->chat_id_;
  M.message_id = MD->id_;

  struct message_alias *A;
  A = tree_lookup_message_alias_global (message_global_tree, &M);
  if (A) {
    if (!A->message) {
      A->message = MD;
      __sync_fetch_and_add (&MD->refcnt, 1);
    }
    return;
  }
  A = malloc (sizeof (*A));
  A->local_id = ++ message_local_last_id;
  A->chat_id = M.chat_id;
  A->message_id = M.message_id;
  A->message = MD;
  __sync_fetch_and_add (&MD->refcnt, 1);
  message_global_tree = tree_insert_message_alias_global (message_global_tree, A, rand ());
  message_local_tree = tree_insert_message_alias_local (message_local_tree, A, rand ());
}
/* }}} */ 

struct file_wait_cb {
  struct file_wait_cb *next;
  void (*callback)(void *, void *, struct TdNullaryObject *);
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

void in_command_decref (struct in_command *cmd) {
  if (!--cmd->refcnt) {
    free (cmd->line);
  
    if (cmd->ev && !--cmd->ev->refcnt) {
      free (cmd->ev);
    }
    
    free (cmd);

    allocated_commands --;

    if (!allocated_commands && safe_quit) {
      do_halt (0);
    }
  } else {
    assert (cmd->refcnt > 0);
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


int in_readline;
int readline_active;

int log_level;

char *line_ptr;

long long cur_chat_mode_chat_id;
extern int readline_disabled;

extern int disable_output;

struct in_ev *notify_ev;

extern int usfd;
extern int sfd;
extern int use_ids;

extern int daemonize;

extern void *TLS;
int readline_deactivated;

void update_chat_aliases (struct TdChat *chat) {
  if (!chat) { return; }
}

void fail_interface (void *TLS, struct in_command *cmd, int error_code, const char *format, ...) __attribute__ (( format (printf, 4, 5)));
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
#define WAIT_AIO (int)0x80000001
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
  static char buf[32];
  sprintf (buf, "%lld", id.message_id);
  /* 
  unsigned char *s = (void *)&id;
  int i;
  for (i = 0; i < (int)sizeof (tdl_message_id_t); i++) {
    sprintf (buf + 2 * i, "%02x", (unsigned)s[i]);
  }
  */
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

long long cur_token_peer (char *s, enum tdcli_chat_type mode, struct in_command *cmd);

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

  long long chat_id = cur_token_peer (s, tdcli_any, cmd);
  *t = c;

  if (chat_id == WAIT_AIO) {
    res.message_id = -2;
    return res;
  }
  if (chat_id == NOT_FOUND) {
    return res;
  }
  res.chat_id = chat_id;
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

void stop_query (struct delayed_query *q, int error_code, char *error) {
  fail_interface (TLS, q->cmd, error_code, "Fail to resolve chat: %s", error);
  
  if (q->token) {
    free (q->token);
  }
  free (q);
}

void process_with_query_resolve_chat (void *TLS, void *extra, struct TdNullaryObject *R) {
  if (R->ID == CODE_Error) {
    struct TdError *E = (void *)R;
    stop_query (extra, E->code_, E->message_);
    return;
  }
  update_chat_aliases ((struct TdChat *)R);
  proceed_with_query (extra);
}

long long cur_token_peer (char *s, enum tdcli_chat_type mode, struct in_command *cmd) {
  if (!s) {
    return NOT_FOUND;
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
    if (A->peer->chat) {
      if (mode == tdcli_any || mode == A->peer->peer_type) {
        if (mode == tdcli_any) {
          return A->peer->chat_id;
        } else {
          return A->peer->peer_id;
        }
      } else {
        return NOT_FOUND;
      }
    } else if (!cmd) {
      return NOT_FOUND;
    } else {
      if (mode != tdcli_any && A->peer->peer_type != mode) {
        return NOT_FOUND;       
      }
      struct delayed_query *q = calloc (sizeof (*q), 1);
      q->cmd = cmd;
      q->action = 1;
      q->mode = mode;

      switch (A->peer->peer_type) {
      case CODE_User:
        TdCClientSendCommand (TLS, (void *)TdCreateObjectCreatePrivateChat (A->peer->peer_id), process_with_query_resolve_chat, q);
        break;
      case CODE_Group:
        TdCClientSendCommand (TLS, (void *)TdCreateObjectCreateGroupChat (A->peer->peer_id), process_with_query_resolve_chat, q);
        break;
      case CODE_Channel:
        TdCClientSendCommand (TLS, (void *)TdCreateObjectCreateChannelChat (A->peer->peer_id), process_with_query_resolve_chat, q);
        break;
      /*case CODE_secret_chat:
        tdlib_get_secret_chat (TLS, process_with_query_resolve_chat, q, ((struct tdl_secret_chat *)q->chat)->id;
        break;*/
      default:
        assert (0);
        return NOT_FOUND;
      }
      return WAIT_AIO;
    }
  }

  if (!cmd) {
    return NOT_FOUND;
  }
  
  if (*s == '@' && cur_token_len >= 2) {
    struct delayed_query *q = calloc (sizeof (*q), 1);
    q->cmd = cmd;
    q->action = 2;
    q->mode = mode;

    TdCClientSendCommand (TLS, (void *)TdCreateObjectSearchPublicChat (s + 1), process_with_query_resolve_chat, q);

    return WAIT_AIO;
  }

  char *f[3] = { "user#id", "group#id", "channel#id" };
  int ff[3] = {CODE_User, CODE_Group, CODE_Channel};

  int i;
  for (i = 0; i < 3; i++) {
    if (mode >= 0 && mode != ff[i]) { continue; }
    if (!memcmp (s, f[i], strlen (f[i]))) {
      int id = atoi (s + strlen (f[i]));
      if (id != 0) {
        struct delayed_query *q = calloc (sizeof (*q), 1);
        q->action = 4;
        q->cmd = cmd;

        switch (ff[i]) {
          case CODE_User:
            TdCClientSendCommand (TLS, (void *)TdCreateObjectCreatePrivateChat (id), process_with_query_resolve_chat, q);
            break;
          case CODE_Group:
            TdCClientSendCommand (TLS, (void *)TdCreateObjectCreateGroupChat (id), process_with_query_resolve_chat, q);
            break;
          case CODE_Channel:
            TdCClientSendCommand (TLS, (void *)TdCreateObjectCreateChannelChat (id), process_with_query_resolve_chat, q);
            break;
            /*case CODE_secret_chat:
              tdlib_get_secret_chat (TLS, process_with_query_resolve_chat, q, ((struct tdl_secret_chat *)q->chat)->id;
              break;*/
          default:
            assert (0);
            return NOT_FOUND;
        }
        
        return WAIT_AIO;
      }
    }
  }
  
  return NOT_FOUND;
}

char *get_default_prompt (void) {
  static char buf[1000];
  int l = 0;
  l += snprintf (buf + l, 999 - l, "[%s] ", conn_state);
  l += snprintf (buf + l, 999 - l, "%d ", total_unread);
  /*if (TLS->cur_uploading_bytes || TLS->cur_downloading_bytes) {
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
  }*/
  //if (cur_chat_mode_chat) {
  //  l += snprintf (buf + l, 999 - l, "%.*s ", 100, cur_chat_mode_chat->title);
  //}
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

  #ifdef READLINE_GNU
  print_start ();
  #endif
  set_prompt (get_default_prompt ());
  if (readline_active) {
    #ifdef READLINE_GNU
    rl_forced_update_display ();
    #endif
  }
  #ifdef READLINE_GNU
  print_end ();
  #endif
}

char *modifiers[] = {
  "[offline]",
  "[enable_preview]",
  "[disable_preview]",
  "[html]",
  "[markdown]",
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

void print_string_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_chat_link_gw (struct in_command *cmd, struct TdNullaryObject *res);
void open_filename_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_filename_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_user_list_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_chat_members_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_msg_list_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_dialog_list_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_group_info_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_channel_info_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_user_info_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_peer_info_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_callback_answer_gw (struct in_command *cmd, struct TdNullaryObject *res);

void print_channel_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_user_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_chat_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_peer_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_secret_chat_gw (struct in_command *cmd, struct TdNullaryObject *res);

void print_msg_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_msg_success_gw (struct in_command *cmd, struct TdNullaryObject *res);
void print_success_gw (struct in_command *cmd, struct TdNullaryObject *result);

void print_invite_link_gw (struct in_command *cmd, struct TdNullaryObject *res);

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

void tdcli_cb (void *instance, void *extra, struct TdNullaryObject *result) {
  if (!extra) { return; }

  if (verbosity >= 2) {
    char *s = TdSerializeNullaryObject (result);
    logprintf ("%s\n", s);
    free (s);
  }

  struct in_command *cmd = extra;
  assert (cmd->cb);
  cmd->cb (cmd, result);
  
  in_command_decref (cmd);
}

/* {{{ client methods */

void on_timer_alarm (evutil_socket_t fd, short what, void *arg) {
  struct TdOk *result = TdCreateObjectOk ();  
  tdcli_cb (TLS, arg, (struct TdNullaryObject *)result);
  TdDestroyObjectOk (result);
}

void do_timer (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
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
  int total = 0;
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
  
  struct TdTestString *result = TdCreateObjectTestString (s);
  assert (result);
  assert (result->refcnt == 1);
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);
  TdDestroyObjectTestString (result);
  #undef BL
}

void do_show_license (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  static char *s = 
#include "LICENSE.h"
  ;
  struct TdTestString *result = TdCreateObjectTestString (s);
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);
  TdDestroyObjectTestString (result);
}

void do_quit (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (daemonize) {
    event_incoming (cmd->ev->bev, BEV_EVENT_EOF, cmd->ev);
  }
  do_halt (0);
  
  struct TdOk *result = TdCreateObjectOk ();  
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);
  TdDestroyObjectOk (result);
}

void do_safe_quit (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (daemonize) {
    event_incoming (cmd->ev->bev, BEV_EVENT_EOF, cmd->ev);
  }
  safe_quit = 1;
  
  struct TdOk *result = TdCreateObjectOk ();  
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);
  TdDestroyObjectOk (result);
}

void do_set (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  const char *var = args[2].str;
  int value = (int)args[2].num;

  if (!strcmp (var, "debug_verbosity")) {  
    verbosity = value;
    TdCClientSetVerbosity (verbosity);
  } else if (!strcmp (var, "log_level")) {
    log_level = value;
  } else if (!strcmp (var, "msg_num")) {
    msg_num_mode = value;
  } else if (!strcmp (var, "alert")) {
    alert_sound = value;
  }
  
  struct TdOk *result = TdCreateObjectOk ();  
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);  
  TdDestroyObjectOk (result);
}

void do_chat_with_peer (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (!cmd->ev) {
    cur_chat_mode_chat_id = args[2].chat_id;
  }
  
  struct TdOk *result = TdCreateObjectOk ();  
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);  
  TdDestroyObjectOk (result);
}

void do_main_session (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  if (notify_ev && !--notify_ev->refcnt) {
    free (notify_ev);
  }
  notify_ev = cmd->ev;
  if (cmd->ev) { cmd->ev->refcnt ++; }
  
  struct TdOk *result = TdCreateObjectOk ();  
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);  
  TdDestroyObjectOk (result);
}

void do_version (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  #define STR2(X) #X
  #define STR(X) STR2(X)
  //char *s = TELEGRAM_CLI_VERSION_STR;
  char *s = "Telegram-cli " TELEGRAM_CLI_VERSION "\n"
            "Uses tdlib version " GIT_COMMIT "\n"
            #ifdef READLINE_GNU
            "Uses libreadline " STR(RL_VERSION_MAJOR) "." STR(RL_VERSION_MINOR) "\n"
            #else
            "Uses libedit ????\n"
            #endif
            #ifdef USE_LUA
            "Uses " LUA_VERSION "\n"
            #endif
            #ifdef USE_JSON
            "Uses libjansson " JANSSON_VERSION "\n"
            #endif
            "Uses libevent " LIBEVENT_VERSION "\n"
            #ifdef HAVE_LIBCONFIG
            "Uses libconfig " STR(LIBCONFIG_VER_MAJOR) "." 
                              STR(LIBCONFIG_VER_MINOR) "."
                              STR(LIBCONFIG_VER_REVISION) "\n"
            #endif
            ;
  #undef STR
  #undef STR2
  
  struct TdTestString *result = TdCreateObjectTestString (s);
  tdcli_cb (TLS, cmd, (struct TdNullaryObject *)result);
  TdDestroyObjectTestString (result);
}
/* }}} */

/* {{{ WORK WITH ACCOUNT */

void do_set_password (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  //assert (arg_num == 1);
  //tgl_do_set_password (TLS, ARG2STR_DEF(0, "empty"), print_success_gw, ev);
}
/* }}} */

void do_push_button (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  struct TdMessage *M = get_message (args[2].msg_id.chat_id, args[2].msg_id.message_id);
  if (!M) {
    fail_interface (TLS, cmd, EINVAL, "Can not find message");
    return;
  }
  if (!M->reply_markup_ || M->reply_markup_->ID != CODE_ReplyMarkupInlineKeyboard) {
    fail_interface (TLS, cmd, EINVAL, "Message does not contain inline buttons");
    return;
  }
  struct TdReplyMarkupInlineKeyboard *R = (void *)M->reply_markup_;
  int p = 0;
  while (p <= args[3].num) {
    int i, j;
    for (i = 0; i < R->rows_->len; i++) {
      for (j = 0; j < R->rows_->data[i]->len; j++) {
        struct TdInlineKeyboardButton *B = R->rows_->data[i]->data[j];        

        if (p == args[3].num) {
          if (B->type_->ID != CODE_InlineKeyboardButtonTypeCallback) {
            fail_interface (TLS, cmd, EINVAL, "Bad button type");
            return;
          } else {
            struct TdInlineKeyboardButtonTypeCallback *C = (void *)B->type_;
            TdCClientSendCommand(TLS, (void *)TdCreateObjectGetCallbackQueryAnswer (M->chat_id_, M->id_, (void *)TdCreateObjectCallbackQueryData (TdCreateObjectBytes (C->data_.data, C->data_.len))), tdcli_cb, cmd);
            return;
          }
        }
        p ++;
      }
    }
  }
  
  fail_interface (TLS, cmd, EINVAL, "Bad button id");
  //assert (arg_num == 1);
  //tgl_do_set_password (TLS, ARG2STR_DEF(0, "empty"), print_success_gw, ev);
}

void try_download_cb (void *instance, void *extra, struct TdNullaryObject *result) {
  if (result->ID == CODE_Error) {
    struct file_wait *F = extra;
    struct file_wait_cb *cb = F->first_cb;
    while (cb) {
      cb->callback (TLS, cb->callback_extra, result);
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
  cb->callback = tdcli_cb;  
  cb->callback_extra = cmd;
  if (F->first_cb) {
    F->last_cb->next = cb;
    F->last_cb = cb;
  } else {
    F->last_cb = F->first_cb = cb;

    TdCClientSendCommand(TLS, (void *)TdCreateObjectDownloadFile (id, 1), try_download_cb, F);
  }
}

/* {{{ SENDING MESSAGES */

void do_msg (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id;
  long long reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat_id;
    reply_id = find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  char *text = args[3].str;
  int do_html = (int)find_modifier (args[0].vec_len, args[0].vec, "html", 0);
  int do_markdown = (int)find_modifier (args[0].vec_len, args[0].vec, "markdown", 0);

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

  assert (do_html == 0 || do_html == 1);

  struct TdInputMessageContent *content = (void *)TdCreateObjectInputMessageText (text, disable_preview, 1, (void *)TdCreateObjectVectorNullaryObject (0, NULL), do_html ? (void *)TdCreateObjectTextParseModeHTML () : do_markdown ? (void *)TdCreateObjectTextParseModeMarkdown () : NULL);
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendMessage (chat_id, reply_id, 0, 0, NULL, content), tdcli_cb, cmd);
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
  long long chat_id;
  long long reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;    
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat_id;
    reply_id = (long long)find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  char *media_type = args[3].str;
  char *file_name = args[4].str;
  char *caption = args[5].str;

  
  struct TdInputMessageContent *content = NULL;
  if (media_type && strlen (media_type)) {
    if (!strcmp (media_type, "animation")) {
      content = (void *)TdCreateObjectInputMessageAnimation ((void *)TdCreateObjectInputFileLocal (file_name), NULL, 0, 0, 0, caption);
    } else if (!strcmp (media_type, "audio")) {
      content = (void *)TdCreateObjectInputMessageAudio ((void *)TdCreateObjectInputFileLocal (file_name), NULL, 0, NULL, NULL, caption);
    } else if (!strcmp (media_type, "document")) {
      content = (void *)TdCreateObjectInputMessageDocument ((void *)TdCreateObjectInputFileLocal (file_name), NULL, caption);
    } else if (!strcmp (media_type, "photo")) {
      content = (void *)TdCreateObjectInputMessagePhoto ((void *)TdCreateObjectInputFileLocal (file_name), NULL, (void *)TdCreateObjectVectorInt (0, NULL), 0, 0, caption);
    } else if (!strcmp (media_type, "sticker")) {
      content = (void *)TdCreateObjectInputMessageSticker ((void *)TdCreateObjectInputFileLocal (file_name), NULL, 0, 0);
    } else if (!strcmp (media_type, "video")) {
      content = (void *)TdCreateObjectInputMessageVideo ((void *)TdCreateObjectInputFileLocal (file_name), NULL, (void *)TdCreateObjectVectorInt (0, NULL), 0, 0, 0, caption);
    } else if (!strcmp (media_type, "voice")) {
      content = (void *)TdCreateObjectInputMessageVoice ((void *)TdCreateObjectInputFileLocal (file_name), 0, TdCreateObjectBytes (NULL, 0), caption);
    } else {
      fail_interface (TLS, cmd, EINVAL, "Unknown media type");
      return;
    }
  } else {
    content = (void *)TdCreateObjectInputMessageDocument ((void *)TdCreateObjectInputFileLocal (file_name), NULL, caption);
  }

  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendMessage (chat_id, reply_id, 0, 0, NULL, content), tdcli_cb, cmd);
}

void do_send_location (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id;
  long long reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat_id;
    reply_id = find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  double latitude = args[3].dval;
  double longitude = args[4].dval;

  struct TdInputMessageContent *content = (void *)TdCreateObjectInputMessageLocation (TdCreateObjectLocation (latitude, longitude));
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendMessage (chat_id, reply_id, 0, 0, NULL, content), tdcli_cb, cmd);
}

void do_send_contact (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id;
  long long reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;    
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat_id;
    reply_id = find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  char *phone = args[3].str;
  char *first_name = args[4].str;
  char *last_name = args[5].str;

  struct TdInputMessageContent *content = (void *)TdCreateObjectInputMessageContact (TdCreateObjectContact (phone, first_name, last_name, 0));
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendMessage (chat_id, reply_id, 0, 0, NULL, content), tdcli_cb, cmd);
}

void do_fwd (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id;
  long long reply_id;
  if (command->params[0]) {
    chat_id = args[2].msg_id.chat_id;
    reply_id = args[2].msg_id.message_id;
  } else {
    chat_id = args[2].chat_id;
    reply_id = find_modifier (args[0].vec_len, args[0].vec, "reply_id", 2);
  }

  long long from_chat_id = args[3].msg_id.chat_id;
  long long msg_id = args[3].msg_id.message_id;
  
  struct TdInputMessageContent *content = (void *)TdCreateObjectInputMessageForwarded (from_chat_id, msg_id, 0);
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendMessage (chat_id, reply_id, 0, 0, NULL, content), tdcli_cb, cmd);
}

/* }}} */

/* {{{ EDITING SELF PROFILE */

void do_change_profile_photo (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *file_name = args[2].str;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectSetProfilePhoto (file_name), tdcli_cb, cmd);
}

void do_change_profile_name (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *first_name = args[2].str;
  char *last_name = args[3].str;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeName (first_name, last_name), tdcli_cb, cmd);
}

void do_change_username (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *username = args[2].str;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeUsername (username), tdcli_cb, cmd);
}

/* }}} */

/* {{{ WORKING WITH GROUP CHATS */

void do_mute (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  int mute_for = (command->params[0]) ? (int)(args[3].num == NOT_FOUND ? 3600 : args[3].num) : 0;

  struct TdNotificationSettingsScope *scope = (void *)TdCreateObjectNotificationSettingsForChat (chat_id);
  struct TdNotificationSettings *settings = TdCreateObjectNotificationSettings (mute_for, "default", 0); 
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSetNotificationSettings (scope, settings), tdcli_cb, cmd);
}

void do_chat_change_photo (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  char *file_name = args[3].str;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChatPhoto (chat_id, (void *)TdCreateObjectInputFileLocal (file_name)), tdcli_cb, cmd);
}

void do_chat_change_title (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  char *title = args[3].str;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChatTitle (chat_id, title), tdcli_cb, cmd);
}

void do_chat_info (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  struct TdChat *C = get_chat (args[2].chat_id);

  if (C) {    
    switch ((enum List_ChatInfo)C->type_->ID) {
      case CODE_PrivateChatInfo:
        {
          struct TdPrivateChatInfo *I = (void *)C->type_;
          TdCClientSendCommand(TLS, (void *)TdCreateObjectGetUserFull (I->user_->id_), tdcli_cb, cmd);
        }
        break;
      case CODE_GroupChatInfo:
        {
          struct TdGroupChatInfo *I = (void *)C->type_;
          TdCClientSendCommand(TLS, (void *)TdCreateObjectGetGroupFull (I->group_->id_), tdcli_cb, cmd);
        }
        break;
      case CODE_ChannelChatInfo:
        {
          struct TdChannelChatInfo *I = (void *)C->type_;
          TdCClientSendCommand(TLS, (void *)TdCreateObjectGetChannelFull (I->channel_->id_), tdcli_cb, cmd);
        }
        break;
      case CODE_SecretChatInfo:
      default:
        assert (0);
    }
  } else {
    fail_interface (TLS, cmd, EINVAL, "Unknown chat");
  }
}

void do_chat_change_role (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  struct TdChatMemberStatus *status = NULL;

  char *role_str = args[4].str;
  if (!strcmp (role_str, "creator")) {
    status = (void *)TdCreateObjectChatMemberStatusCreator ();
  } else if (!strcmp (role_str, "editor")) {
    status = (void *)TdCreateObjectChatMemberStatusEditor ();
  } else if (!strcmp (role_str, "moderator")) {
    status = (void *)TdCreateObjectChatMemberStatusModerator ();
  } else if (!strcmp (role_str, "general")) {
    status = (void *)TdCreateObjectChatMemberStatusMember ();
  } else if (!strcmp (role_str, "kicked")) {
    status = (void *)TdCreateObjectChatMemberStatusKicked ();
  } else {
    fail_interface (TLS, cmd, EINVAL, "Unknown member role");
    return;
  }
  
  long long chat_id = args[2].chat_id;
  int user_id = args[3].user_id;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChatMemberStatus (chat_id, user_id, status), tdcli_cb, cmd);
}

void do_chat_add_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  int user_id = args[3].user_id;
  int fwd_msg_num = (args[4].num == NOT_FOUND) ? 0 : (int)args[4].num;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectAddChatMember (chat_id, user_id, fwd_msg_num), tdcli_cb, cmd);
}

void do_chat_del_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  int user_id = args[3].user_id;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChatMemberStatus (chat_id, user_id, 
   (void *)TdCreateObjectChatMemberStatusKicked ()), tdcli_cb, cmd);
}

void do_chat_join (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  TdCClientSendCommand(TLS, (void *)TdCreateObjectAddChatMember (chat_id, my_id, 0), tdcli_cb, cmd);
}

void do_chat_leave (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChatMemberStatus (chat_id, my_id, 
   (void *)TdCreateObjectChatMemberStatusLeft ()), tdcli_cb, cmd);
}
    
void do_group_create (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *title = args[2].str;


  assert (args[3].vec_len <= 1000);
  
  int ids[1000];

  int i;
  for (i = 0; i < args[3].vec_len; i++) {
    ids[i] = args[3].vec[i].user_id;
  }
  
  struct TdVectorInt *av = TdCreateObjectVectorInt (args[3].vec_len, ids);

  TdCClientSendCommand(TLS, (void *)TdCreateObjectCreateNewGroupChat (av, title), tdcli_cb, cmd);  
}
    
void do_channel_create (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *title = args[2].str;
  char *about = args[3].str;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectCreateNewChannelChat (title, (int)command->params[0], about), tdcli_cb, cmd);  
}

void do_chat_export_link (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectExportChatInviteLink (chat_id), tdcli_cb, cmd);  
}

void do_chat_import_link (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *link = args[2].str;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectImportChatInviteLink (link), tdcli_cb, cmd);  
}

void do_chat_check_link (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *link = args[2].str;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectCheckChatInviteLink (link), tdcli_cb, cmd);  
}

void do_channel_get_members (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int channel_id = args[2].channel_id;
  int limit = (args[3].num == NOT_FOUND) ? 0 : (int)args[3].num;
  int offset = (args[4].num == NOT_FOUND) ? 0 : (int)args[4].num;

  struct TdChannelMembersFilter *filter;
  switch ((int)command->params[0]) {
  case 0:
    filter = (void *)TdCreateObjectChannelMembersRecent ();
    break;
  case 1:
    filter = (void *)TdCreateObjectChannelMembersAdministrators ();
    break;
  case 2:
    filter = (void *)TdCreateObjectChannelMembersKicked ();
    break;
  case 3:
    filter = (void *)TdCreateObjectChannelMembersBots ();
    break;
  default:
    assert (0);
    filter = NULL;
  }
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectGetChannelMembers (channel_id, filter, offset, limit), tdcli_cb, cmd);  
}

void do_group_upgrade (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectMigrateGroupChatToChannelChat (chat_id), tdcli_cb, cmd);  
}


/* }}} */

/* {{{ WORKING WITH USERS */

void do_add_contact (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *phone = args[2].str;
  char *first_name = args[3].str;
  char *last_name = args[4].str;

  struct TdContact *contact = TdCreateObjectContact (phone, first_name, last_name, 0);
  struct TdVectorContact *contacts = (void *)TdCreateObjectVectorNullaryObject (1, (struct TdNullaryObject **)&contact);
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectImportContacts (contacts), tdcli_cb, cmd);  
}


void do_block_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int user_id = args[2].user_id;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectBlockUser (user_id), tdcli_cb, cmd);  
}

void do_unblock_user (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int user_id = args[2].user_id;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectUnblockUser (user_id), tdcli_cb, cmd);  
}
/* }}} */

/* WORKING WITH CHANNELS {{{ */

void do_channel_change_about (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int channel_id = args[2].channel_id;
  char *about = args[3].str;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChannelAbout (channel_id, about), tdcli_cb, cmd);  
}

void do_channel_change_username (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int channel_id = args[2].channel_id;
  char *username = args[3].str;
  if (*username == '@') { username ++; }

  TdCClientSendCommand(TLS, (void *)TdCreateObjectChangeChannelUsername (channel_id, username), tdcli_cb, cmd);  
}

void do_channel_edit (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *enabled = args[4].str;
  int mode;
  if (!strcmp (enabled, "yes") || !strcmp (enabled, "enabled")) {
    mode = 1;
  } else if (!strcmp (enabled, "no") || !strcmp (enabled, "disabled")) {
    mode = 0;
  } else {
    fail_interface (TLS, cmd, EINVAL, "yes/no expected as third argument");
    return;
  }
  
  int channel_id = args[2].channel_id;
  char *scope = args[3].str;

  if (!strcmp (scope, "invites")) {
    TdCClientSendCommand(TLS, (void *)TdCreateObjectToggleChannelInvites (channel_id, mode), tdcli_cb, cmd);  
  } else if (!strcmp (args[1].str, "sign")) {
    TdCClientSendCommand(TLS, (void *)TdCreateObjectToggleChannelSignMessages (channel_id, mode), tdcli_cb, cmd);  
  } else {
    fail_interface (TLS, cmd, EINVAL, "invites/sign expected as second argument");
    return;
  }
}

void do_pin_message (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].msg_id.chat_id;
  long long msg_id = args[2].msg_id.message_id;
 
  struct tdcli_peer *P = get_peer (chat_id);

  if (!P || P->peer_type != CODE_Channel) {
    fail_interface (TLS, cmd, EINVAL, "message must be in supergroup");
    return;
  }

  int channel_id = P->peer_id;
    
  TdCClientSendCommand(TLS, (void *)TdCreateObjectPinChannelMessage (channel_id, msg_id, 1), tdcli_cb, cmd);  
}

/* }}} */

/* {{{ WORKING WITH DIALOG LIST */

void do_dialog_list (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int limit = (args[2].num == NOT_FOUND) ? 10 : (int)args[2].num;
  int offset = (args[3].num == NOT_FOUND) ? 0 : (int)args[3].num;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectGetChats ((1ull << 63) - 1 - offset, 0, limit), tdcli_cb, cmd);  
}

void do_resolve_username (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  char *u = args[2].str;
  if (*u == '@') { u ++; }
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSearchPublicChat (u), tdcli_cb, cmd);  
}

void do_contact_list (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int limit = (args[2].num == NOT_FOUND) ? 10 : (int)args[2].num;
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSearchContacts (NULL, limit), tdcli_cb, cmd);  
}

void do_contact_delete (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int user_id = args[2].user_id;

  struct TdVectorInt *av = TdCreateObjectVectorInt (1, &user_id);
  TdCClientSendCommand(TLS, (void *)TdCreateObjectDeleteContacts (av), tdcli_cb, cmd);  
}

/* }}} */

/* {{{ WORKING WITH ONE DIALOG */

void do_mark_read (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].chat_id;
  long long id = 0;
  struct TdVectorLong *av = TdCreateObjectVectorLong (1, &id);
  TdCClientSendCommand(TLS, (void *)TdCreateObjectViewMessages (chat_id, av), tdcli_cb, cmd);  
}

struct chat_history_extra {
  struct in_command *cmd;
  int current_size;
  int current_pos;
  int current_offset;
  int limit;
  long long last_msg_id;
  long long chat_id;
  struct TdMessage **list;
};

void do_send_history_query (struct chat_history_extra *e);

void free_chat_history_extra (struct chat_history_extra *e) {
  int i;
  for (i = 0; i < e->current_pos; i++) {
    TdDestroyObjectMessage (e->list[i]);
  }
  free (e->list);
  free (e);
}

void received_chat_history_slice (void *TLS, void *extra, struct TdNullaryObject *res) {
  struct chat_history_extra *e = extra;
  if (res->ID == CODE_Error) {
    tdcli_cb (TLS, e->cmd, res);
    free_chat_history_extra (e);
    return;
  }
  assert (res->ID == CODE_Messages);
  struct TdMessages *msgs = (void *)res;
  int cnt = msgs->messages_->len;
  assert (cnt + e->current_pos <= e->limit);  
  
  if (cnt + e->current_pos > e->current_size) {
    e->list = realloc (e->list, sizeof (void *) * (cnt + e->current_pos));
    e->current_size = cnt + e->current_pos;
  }
  
  int i;
  for (i = 0; i < cnt; i++) {
    __sync_fetch_and_add (&msgs->messages_->data[i]->refcnt, 1);
    e->list[e->current_pos ++] = msgs->messages_->data[i];
  }

  if (cnt == 0 || e->current_pos >= e->limit) {
    for (i = 0; i < e->current_pos; i++) {
      __sync_fetch_and_add (&e->list[i]->refcnt, 1);
    }
    struct TdVectorMessage *vec = (void *)TdCreateObjectVectorNullaryObject (e->current_pos, (void *)e->list);
    struct TdMessages *msgs_vec = TdCreateObjectMessages (msgs->total_count_, vec);
    tdcli_cb (TLS, e->cmd, (struct TdNullaryObject *)msgs_vec);
    
    if (e->current_pos > 0) {
      long long id = e->list[0]->id_;
      struct TdVectorLong *av = TdCreateObjectVectorLong (1, &id);
      TdCClientSendCommand(TLS, (void *)TdCreateObjectViewMessages (e->chat_id, av), tdcli_cb, NULL);  
    }

    TdDestroyObjectMessages (msgs_vec);
    free_chat_history_extra (e);
    return;
  }
  
  if (cnt > 0) {
    e->last_msg_id = e->list[e->current_pos - 1]->id_;
    e->current_offset = 0;
  }

  do_send_history_query (e);
}

void do_send_history_query (struct chat_history_extra *e) {
  int p = e->limit - e->current_pos;
  if (p > 100) { p = 100; }
  assert (p >= 0);

  TdCClientSendCommand(TLS, (void *)TdCreateObjectGetChatHistory (e->chat_id, e->last_msg_id, e->current_offset, p), received_chat_history_slice, e);  
}

void do_history (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  struct chat_history_extra *e = malloc (sizeof (*e));
  e->cmd = cmd;
  e->current_pos = 0;
  e->current_size = 0;
  e->current_offset = args[4].num != NOT_FOUND ? (int)args[4].num : 0;
  e->limit = args[3].num != NOT_FOUND ? (int)args[3].num : 40;
  e->last_msg_id = 0;
  e->chat_id = args[2].chat_id;
  e->list = NULL;
  
  do_send_history_query (e);
}

void do_send_typing (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  struct TdSendMessageAction *action;

  long long chat_id = args[2].chat_id;
  char *typing = args[3].str;
  int progress = args[4].num == NOT_FOUND ? 0 : (int)args[4].num;

  if (!typing || !strcmp (typing, "typing")) {
    action = (void *)TdCreateObjectSendMessageTypingAction ();
  } else if (!strcmp (typing, "cancel")) {
    action = (void *)TdCreateObjectSendMessageCancelAction ();
  } else if (!strcmp (typing, "record_video")) {
    action = (void *)TdCreateObjectSendMessageRecordVideoAction ();
  } else if (!strcmp (typing, "upload_video")) {
    action = (void *)TdCreateObjectSendMessageUploadVideoAction (progress);
  } else if (!strcmp (typing, "record_voice")) {
    action = (void *)TdCreateObjectSendMessageRecordVoiceAction ();
  } else if (!strcmp (typing, "upload_voice")) {
    action = (void *)TdCreateObjectSendMessageUploadVoiceAction (progress);
  } else if (!strcmp (typing, "upload_photo")) {
    action = (void *)TdCreateObjectSendMessageUploadPhotoAction (progress);
  } else if (!strcmp (typing, "upload_document")) {
    action = (void *)TdCreateObjectSendMessageUploadDocumentAction (progress);
  } else if (!strcmp (typing, "choose_location")) {
    action = (void *)TdCreateObjectSendMessageGeoLocationAction ();
  } else if (!strcmp (typing, "choose_contact")) {
    action = (void *)TdCreateObjectSendMessageChooseContactAction ();
  } else {
    fail_interface (TLS, cmd, ENOSYS, "illegal typing status");
    return;
  }
  
  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendChatAction (chat_id, action), tdcli_cb, cmd);  
}

/* }}} */


/* {{{ ANOTHER MESSAGES FUNCTIONS */

void do_search (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int limit = (args[3].num == NOT_FOUND) ? 40 : (int)args[3].num;
  int from = (args[4].num == NOT_FOUND) ? 0 : (int)args[4].num;
 
  long long chat_id = args[2].chat_id;
  char *query = args[7].str;

  if (chat_id != NOT_FOUND) {
    TdCClientSendCommand(TLS, (void *)TdCreateObjectSearchChatMessages (chat_id, query, from, limit, (void *)TdCreateObjectSearchMessagesFilterEmpty ()), tdcli_cb, cmd);  
  } else {
    TdCClientSendCommand(TLS, (void *)TdCreateObjectSearchMessages (query, 0, 0, 0, limit), tdcli_cb, cmd);  
  }
}

void do_delete_msg (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].msg_id.chat_id;
  long long msg_id = args[2].msg_id.message_id;

  struct TdVectorLong *vec = TdCreateObjectVectorLong (1, &msg_id);
  TdCClientSendCommand(TLS, (void *)TdCreateObjectDeleteMessages (chat_id, vec, 0), tdcli_cb, cmd);  
}

void do_get_message (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  long long chat_id = args[2].msg_id.chat_id;
  long long msg_id = args[2].msg_id.message_id;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectGetMessage (chat_id, msg_id), tdcli_cb, cmd);  
}

/* }}} */

/* {{{ BOT */

void do_start_bot (struct command *command, int arg_num, struct arg args[], struct in_command *cmd) {
  int user_id = args[2].user_id;
  long long chat_id = args[3].chat_id;
  char *token = args[4].str;

  TdCClientSendCommand(TLS, (void *)TdCreateObjectSendBotStartMessage (user_id, chat_id, token), tdcli_cb, cmd);  
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
  
  {"channel_get_admins", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel admins", NULL, {1}},
  {"channel_get_bots", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel bots", NULL, {3}},
  {"channel_get_kicked", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel kicked members", NULL, {2}},
  {"channel_get_members", {{"channel", ca_channel}, {"limit", ca_number | ca_optional}, {"offset", ca_number | ca_optional}}, {{"total", ra_int}, {"members", ra_vector | ra_chat_member}}, do_channel_get_members, print_chat_members_gw, "Gets channel recent members", NULL, {0}},
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
  {"chat_create_broadcast", {{"title", ca_string}, {"about", ca_string}}, {{"chat", ra_chat}}, do_channel_create, print_chat_gw, "Creates broadcast channel", NULL, {0}},
  {"chat_create_group", {{"title", ca_string}, {"members", ca_user | ca_period}}, {{"chat", ra_chat}}, do_group_create, print_chat_gw, "Creates group chat", NULL, {}},
  {"chat_create_supergroup", {{"title", ca_string}, {"about", ca_string}}, {{"chat", ra_chat}}, do_channel_create, print_chat_gw, "Creates supergroup channel", NULL, {1}},
  {"chat_export_invite_link", {{"chat", ca_chat}}, {{"link", ra_string}}, do_chat_export_link, print_chat_link_gw, "Exports new invite link (and invalidates previous)", NULL, {}}, 
  {"chat_import_invite_link", {{"link", ca_string}}, {}, do_chat_import_link, print_success_gw, "Get chat by invite link and joins if possible", NULL, {}}, 
  
  {"chat_with_peer", {{"chat", ca_chat}}, {}, do_chat_with_peer, print_success_gw, "Interface option. All input will be treated as messages to this peer. Type /quit to end this mode", NULL, {}},
  
  {"compose", {{"chat", ca_chat}}, {{"message", ra_message}}, do_compose, print_msg_success_gw, "Sends text message to peer", NULL, {0}},
  {"compose_reply", {{"chat", ca_msg_id}}, {{"message", ra_message}}, do_compose, print_msg_success_gw, "Sends text message to peer", NULL, {1}},
  
  {"contact_list", {{"limit", ca_number | ca_optional}}, {{"users", ra_vector | ra_user}}, do_contact_list, print_user_list_gw, "Prints contact list", NULL, {}},
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

  {"pin_message", {{"message", ca_msg_id}}, {}, do_pin_message, print_success_gw, "Tries to push inline button", NULL, {}},
  {"push_button", {{"message", ca_msg_id}, {"button_id", ca_number}}, {}, do_push_button, print_callback_answer_gw, "Tries to push inline button", NULL, {}},
  

  {"resolve_username", {{"username", ca_string}}, {{"chat", ra_chat}}, do_resolve_username, print_chat_gw, "Find chat by username", NULL, {}},
  
  {"reply", {{"msg_id", ca_msg_id}, {"text", ca_msg_string_end}}, {{"message", ra_message}}, do_msg, print_msg_success_gw, "Sends text message to peer", NULL, {1}},  
  {"reply_file", {{"msg_id", ca_msg_id}, {"type", ca_media_type | ca_optional}, {"file", ca_file_name}, {"caption", ca_string_end | ca_optional}}, {{"message", ra_message}}, do_send_file, print_msg_success_gw, "Replies to peer with file. type=[animation|audio|document|photo|sticker|video|voice]", NULL, {1}},
  {"reply_fwd", {{"msg_id", ca_msg_id}, {"fwd_id", ca_msg_id}}, {{"message", ra_message}}, do_fwd, print_msg_success_gw, "Forwards message to peer. Forward to secret chats is forbidden", NULL, {1}},
  {"reply_location", {{"msg_id", ca_msg_id}, {"longitude", ca_double}, {"latitude", ca_double}}, {{"message", ra_message}}, do_send_location, print_msg_success_gw, "Sends geo location", NULL, {1}},  
  
  {"search", {{"chat", ca_chat | ca_optional}, {"limit", ca_number | ca_optional}, {"from", ca_number | ca_optional}, {"to", ca_number | ca_optional}, {"offset", ca_number | ca_optional}, {"query", ca_string_end}}, {{"messages", ra_message | ra_vector}}, do_search, print_msg_list_gw, "Search for pattern in messages from date from to date to (unixtime) in messages with peer (if peer not present, in all messages)", NULL, {}},
  
  {"send_file", {{"chat", ca_chat}, {"type", ca_media_type | ca_optional}, {"file", ca_file_name}, {"caption", ca_string_end | ca_optional}}, {{"message", ra_message}}, do_send_file, print_msg_success_gw, "Sends file to peer. type=[animation|audio|document|photo|sticker|video|voice]", NULL, {0}},
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

int complete_username (enum tdcli_chat_type mode, int index, const char *text, ssize_t len, char **R) {  
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
    if (mode == tdcli_any || mode == A->peer->peer_type) {
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

  if (cur_chat_mode_chat_id) {
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
    index = complete_username (tdcli_user, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_chat:
    index = complete_username (tdcli_any, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_file_name:
  case ca_file_name_end:
    if (c) { rl_line_buffer[rl_point] = c; }
    R = rl_filename_completion_function (command_pos, state);
    return R;
  case ca_group:
    index = complete_username (tdcli_group, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_secret_chat:
    index = complete_username (tdcli_secret_chat, index, command_pos, command_len, &R);
    if (c) { rl_line_buffer[rl_point] = c; }
    return R;
  case ca_channel:
    index = complete_username (tdcli_channel, index, command_pos, command_len, &R);
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

void print_fail (struct in_command *cmd, struct TdError *error) {
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "FAIL: %d: %s\n", error->code_, error->message_);
  mprint_end (cmd->ev);
}

void fail_interface (void *TLS, struct in_command *cmd, int error_code, const char *format, ...) {
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

  in_command_decref (cmd);
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

void print_success_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) { 
    print_fail (cmd, (struct TdError *)res); 
    return; 
  } else { 
    print_success (cmd); 
    return; 
  }
}

void print_msg_success_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_success_gw (cmd, res);
  } else {
    assert (res->ID == CODE_Message);
    struct TdMessage *M = (void *)res;
    assert (M->send_state_->ID == CODE_MessageIsBeingSent);
    
    struct pending_message Q;
    Q.chat_id = M->chat_id_;
    Q.id = M->id_;
    assert (!tree_lookup_pending_message (pending_messages, &Q));

    struct pending_message *P = malloc (sizeof (*P));
    P->chat_id = M->chat_id_;
    P->id = M->id_;
    P->cmd = cmd;
    if (cmd) {
      cmd->refcnt ++;
    }

    pending_messages = tree_insert_pending_message (pending_messages, P, rand ());
  }
}

void print_msg_list_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }

  assert (res->ID == CODE_Messages);
  struct TdMessages *msgs = (void *)res;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  int num = msgs->messages_->len;
  int i;
  for (i = num - 1; i >= 0; i--) {    
    struct TdMessage *M = msgs->messages_->data[i];
    print_message (cmd->ev, M);
  }
  mprint_end (cmd->ev);
}

void print_msg_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }

  assert (res->ID == CODE_Message);

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  print_message (cmd->ev, (struct TdMessage *)res);
  mprint_end (cmd->ev);
}

void print_callback_answer_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
  
  assert (res->ID == CODE_CallbackQueryAnswer);

  struct TdCallbackQueryAnswer *C = (void *)res;

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  mprintf (cmd->ev, "Callback query answer");
  if (C->text_) {
    mprintf (cmd->ev, " %s", C->text_);
  }
  if (C->url_) {
    mprintf (cmd->ev, " <%s>", C->url_);
  }
  mprintf (cmd->ev, "\n");
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);

}

void print_invite_link_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_ChatInviteLinkInfo);
  struct TdChatInviteLinkInfo *info = (void *)res;

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }

  if (info->is_group_) {
    mprintf (cmd->ev, "Group ");
    mpush_color (cmd->ev, COLOR_MAGENTA);
  } else if (info->is_supergroup_channel_) {
    mprintf (cmd->ev, "Supergroup ");
    mpush_color (cmd->ev, COLOR_MAGENTA);
  } else {
    mprintf (cmd->ev, "Channel ");
    mpush_color (cmd->ev, COLOR_CYAN);
  }

  mprintf (cmd->ev, "%s", info->title_);

  mpop_color (cmd->ev);
  mpop_color (cmd->ev);
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_user_list_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_Users);
  struct TdUsers *users = (void *)res;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }

  int num = users->users_->len;  
  int i;
  for (i = num - 1; i >= 0; i--) {
    struct TdUser *U = users->users_->data[i];
    if (U->id_ != 0) {
      print_user_name (cmd->ev, U, U->id_);
      mprintf (cmd->ev, "\n");
    }
  }
  mprint_end (cmd->ev);
}

void print_member (struct in_ev *ev, struct TdChatMember *U);

void print_chat_members_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_ChatMembers);
  struct TdChatMembers *M = (void *)res;

  int total = M->total_count_;
  int num = M->members_->len;

  mprint_start (cmd->ev);
  int i;
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  mprintf (cmd->ev, "Total %d members\n", total);
  for (i = num - 1; i >= 0; i--) {
    struct TdChatMember *CM = M->members_->data[i];
    print_member (cmd->ev, CM);
    mprintf (cmd->ev, "\n");
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_chat_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_Chat);
  struct TdChat *C = (void *)res;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  print_chat_name (cmd->ev, C, C->id_);
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_user_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_User);
  struct TdUser *U = (void *)res;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  print_user_name (cmd->ev, U, U->id_);
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_channel_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_Channel);
  struct TdChannel *Ch = (void *)res;
  
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }

  struct TdChat *C = get_peer_chat (CODE_Channel, Ch->id_);

  if (C) {
    print_chat_name (cmd->ev, C, C->id_);
  } else {
    mprintf (cmd->ev, "channel#id%d", Ch->id_);
  }
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

void print_group_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_Group);
  struct TdGroup *G = (void *)res;
  
  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  struct TdChat *C = get_peer_chat (CODE_Group, G->id_);

  if (C) {
    print_chat_name (cmd->ev, C, C->id_);
  } else {
    mprintf (cmd->ev, "group#id%d", G->id_);
  }
  mprintf (cmd->ev, "\n");
  mprint_end (cmd->ev);
}

/*void print_peer_gw (struct in_command *cmd, int success, struct res_arg *args) {
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
}*/


void print_string_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }

  assert (res->ID == CODE_TestString);
  struct TdTestString *S = (void *)res;

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "%s\n", S->value_);
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_chat_link_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
 
  assert (res->ID == CODE_ChatInviteLink);
  struct TdChatInviteLink *L = (void *)res;

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "%s\n", L->invite_link_);
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_filename_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
  assert (res->ID == CODE_UpdateFile);
  struct TdUpdateFile *U = (void *)res;

  mprint_start (cmd->ev);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld] ", cmd->query_id);
  }
  mprintf (cmd->ev, "Saved to %s\n", U->file_->path_);
  mprint_end (cmd->ev);
}

void open_filename_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
  assert (res->ID == CODE_UpdateFile);
  struct TdUpdateFile *U = (void *)res;
  
  static char buf[PATH_MAX];
  if (snprintf (buf, sizeof (buf), OPEN_BIN, U->file_->path_) >= (int) sizeof (buf)) {
    logprintf ("Open image command buffer overflow\n");
  } else {
    int pid = fork ();
    if (!pid) {
      execl("/bin/sh", "sh", "-c", buf, (char *) 0);
      exit (0);
    }
  }
}

void print_member (struct in_ev *ev, struct TdChatMember *U) {
  switch ((enum List_ChatMemberStatus)U->status_->ID) {
    case CODE_ChatMemberStatusCreator:
      mprintf (ev, "Creator   ");
      break;
    case CODE_ChatMemberStatusEditor:
      mprintf (ev, "Editor    ");
      break;
    case CODE_ChatMemberStatusModerator:
      mprintf (ev, "Moderator ");
      break;
    case CODE_ChatMemberStatusMember:
      mprintf (ev, "Member    ");
      break;
    case CODE_ChatMemberStatusLeft:
      mprintf (ev, "Left      ");
      break;
    case CODE_ChatMemberStatusKicked:
      mprintf (ev, "Kicked    ");
      break;
  }


  print_user_name (ev, NULL, U->user_id_);
  mprintf (ev, " invited by ");
  print_user_name (ev, NULL, U->inviter_user_id_);
  mprintf (ev, " ");
  print_date_full (ev, U->join_date_);
}

void print_group_info_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
  assert (res->ID == CODE_GroupFull);
 
  struct TdGroupFull *F = (void *)res;
  mprint_start (cmd->ev);
  on_group_update (F->group_);

  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  struct TdGroup *G = F->group_;
  struct TdChat *C = get_peer_chat (CODE_Group, G->id_);

  mprintf (cmd->ev, "Chat ");
  if (C) {
    print_chat_name (cmd->ev, C, C->id_);
  }

  mprintf (cmd->ev, " (id %d) members:\n", G->id_);

  if (C && C->photo_ && C->photo_->big_) {
    mprintf (cmd->ev, "\tphoto:[photo %d]\n", C->photo_->big_->id_);
  }
  int i;  
  for (i = 0; i < F->members_->len; i++) {
    struct TdChatMember *M = F->members_->data[i];
    mprintf (cmd->ev, "\t\t");
    print_member (cmd->ev, M);
    mprintf (cmd->ev, "\n");
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_channel_info_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
  assert (res->ID == CODE_ChannelFull);
 
  struct TdChannelFull *F = (void *)res;
  mprint_start (cmd->ev);
  on_channel_update (F->channel_);

  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }

  struct TdChannel *Ch = F->channel_;
  struct TdChat *C = get_peer_chat (CODE_Channel, Ch->id_);


  if (Ch->is_supergroup_) {
    mprintf (cmd->ev, "Supergroup ");
  } else {
    mprintf (cmd->ev, "Channel ");
  }
  if (Ch->is_verified_) {
    mprintf (cmd->ev, "[verified] ");
  }

  if (C) {
    print_chat_name (cmd->ev, C, C->id_);
  }

  if (Ch->username_) {
    mprintf (cmd->ev, " @%s", Ch->username_);
  }
  mprintf (cmd->ev, " (#%d):\n", Ch->id_);
  if (F->about_) {
    mprintf (cmd->ev, "\tabout: %s\n", F->about_);
  }

  if (C->photo_ && C->photo_->big_) {
    mprintf (cmd->ev, "\tphoto:[photo %d]\n", C->photo_->big_->id_);
  }

  mprintf (cmd->ev, "\t%d members, %d admins, %d kicked\n", F->member_count_, F->administrator_count_, F->kicked_count_);
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_user_info_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }
  assert (res->ID == CODE_UserFull);
  struct TdUserFull *U = (void *)res;

  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  mprintf (cmd->ev, "User ");
  /*if (U->user_->deleted_) {
    mprintf (cmd->ev, "Deleted user ");
  } else {
  }*/
  if (U->user_->is_verified_) {
    mprintf (cmd->ev, "[verified] ");
  }
  print_user_name (cmd->ev, U->user_, U->user_->id_);
  if (U->user_->username_) {
    mprintf (cmd->ev, " @%s", U->user_->username_);
  }
  mprintf (cmd->ev, " (#%d):\n", U->user_->id_);
  mprintf (cmd->ev, "\tphone: %s\n", U->user_->phone_number_);
  mprintf (cmd->ev, "\t");
  print_user_status (cmd->ev, U->user_->status_);
  mprintf (cmd->ev, "\n");

  if (U->user_->profile_photo_) {
    if (U->user_->profile_photo_) {
      mprintf (cmd->ev, "\tphoto: [photo %d]\n", U->user_->profile_photo_->big_->id_);
    }
  }

  if (U->bot_info_) {
    mprintf (cmd->ev, "\tdescription: %s\n", U->bot_info_->description_);
    mprintf (cmd->ev, "\tcommands:\n");

    int i;
    for (i = 0; i < U->bot_info_->commands_->len; i++) {
      struct TdBotCommand *C = U->bot_info_->commands_->data[i];
      mprintf (cmd->ev, "\t\t/%s: %s\n", C->command_, C->description_);
    }
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void print_peer_info_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }

  switch (res->ID) {
  case CODE_UserFull:
    return print_user_info_gw (cmd, res);
  case CODE_ChannelFull:
    return print_channel_info_gw (cmd, res);
  case CODE_GroupFull:
    return print_group_info_gw (cmd, res);
  default:
    assert (0);
  }
}

void print_dialog_list_gw (struct in_command *cmd, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    print_fail (cmd, (struct TdError *)res);
    return;
  }

  assert (res->ID == CODE_Chats);
  struct TdChats *chats = (void *)res;

  int num = chats->chats_->len;
  
  mprint_start (cmd->ev);
  mpush_color (cmd->ev, COLOR_YELLOW);
  if (cmd->query_id) {
    mprintf (cmd->ev, "[id=%lld]\n", cmd->query_id);
  }
  int i;
  for (i = num - 1; i >= 0; i--) {
    mprintf (cmd->ev, "Dialog ");
    struct TdChat *C = chats->chats_->data[i];
    print_chat_name (cmd->ev, C, C->id_);
    mprintf (cmd->ev, "\n");
  }
  mpop_color (cmd->ev);
  mprint_end (cmd->ev);
}

void interpreter_chat_mode (struct in_command *cmd) {
  char *line = cmd->line;
  if (line == NULL || /* EOF received */
          !strncmp (line, "/exit", 5) || !strncmp (line, "/quit", 5)) {
    cur_chat_mode_chat_id = 0;
    update_prompt ();
    return;
  }
  if (!strncmp (line, "/history", 8)) {
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
    e->chat_id = cmd->chat_mode_chat_id;
    e->list = NULL;
  
    do_send_history_query (e);
    return;
  }
  if (strlen (line) > 0) {
    cmd->refcnt ++;

    //tdlib_view_messages (TLS, tdcli_empty_cb, NULL, cmd->chat_mode_chat->id, 1, &cmd->chat_mode_chat->top_message->id);
    
    //union tdl_input_message_content *content = tdlib_create_input_message_content_text (TLS, line, 0, disable_msg_preview, 1);
    //tdlib_send_message (TLS, (void *)tdcli_ptr_cb, cmd, cmd->chat_mode_chat->id, 0, 0, 0, NULL, content);
  }
}

struct update_subscriber {
  void *extra;
  struct update_subscriber *next, *prev;
  void (*cb)(void *extra, struct TdUpdate *U);
};

struct update_subscriber update_subscribers_queue;

struct update_subscriber *subscribe_updates (void *extra, void (*on_update)(void *, struct TdUpdate *)) {
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

void default_update_handler (void *arg, struct TdUpdate *Upd) {
  struct in_ev *ev = arg;
  switch ((enum List_Update)Upd->ID) {
  case CODE_UpdateNewMessage:
    {
      struct TdUpdateNewMessage *U = (void *)Upd;
      mprint_start (ev);
      print_message (ev, U->message_);
      mprint_end (ev);
      if (alert_sound && !U->disable_notification_) {
        play_sound ();
      }
    }
    break;
  case CODE_UpdateMessageSendSucceeded:
    {
      struct TdUpdateMessageSendSucceeded *U = (void *)Upd;
      mprint_start (ev);
      print_message (ev, U->message_);
      mprint_end (ev);

      struct pending_message Q;
      Q.chat_id = U->message_->chat_id_;
      Q.id = U->old_message_id_; 
      struct pending_message *P = tree_lookup_pending_message (pending_messages, &Q);
      
      if (P) {
        print_success (P->cmd);
        in_command_decref (P->cmd);
        pending_messages = tree_delete_pending_message (pending_messages, P);
        free (P);
      }
    }
    break;
  case CODE_UpdateMessageSendFailed:
    {
      struct TdUpdateMessageSendFailed *U = (void *)Upd;
      struct pending_message Q;
      Q.chat_id = U->message_->chat_id_;
      Q.id = U->old_message_id_; 
      struct pending_message *P = tree_lookup_pending_message (pending_messages, &Q);

      if (P) {
        struct TdError *E = TdCreateObjectError (U->error_code_, U->error_message_);
        print_fail (P->cmd, E);
        TdDestroyObjectError (E);
        in_command_decref (P->cmd);
        pending_messages = tree_delete_pending_message (pending_messages, P);
        free (P);
      }
    }
    break;
  case CODE_UpdateMessageContent:
    break;
  case CODE_UpdateMessageEdited:
    break;
  case CODE_UpdateMessageViews:
    break;
  case CODE_UpdateChat:
    break;
  case CODE_UpdateChatTopMessage:
    break;
  case CODE_UpdateChatOrder:
    break;
  case CODE_UpdateChatTitle:
    break;
  case CODE_UpdateChatPhoto:
    break;
  case CODE_UpdateChatReadInbox:
    break;
  case CODE_UpdateChatReadOutbox:
    break;
  case CODE_UpdateChatReplyMarkup:
    break;
  case CODE_UpdateChatDraftMessage:
    break;
  case CODE_UpdateNotificationSettings:
    break;
  case CODE_UpdateDeleteMessages:
    break;
  case CODE_UpdateUserAction:
    {
      struct TdUpdateUserAction *U = (void *)Upd;
      if ((!disable_output || arg) && log_level >= 2) {       
        mprint_start (ev);
        mpush_color (ev, COLOR_YELLOW);

        print_date (ev, time (0));
        mprintf (ev, "User ");
        print_user_name (ev, NULL, U->user_id_);
        mprintf (ev, " is now ");
        print_send_message_action (ev, U->action_);

        if (U->chat_id_) {
          struct TdChat *C = get_chat (U->chat_id_);
          if (!C || C->type_->ID != CODE_PrivateChatInfo) {
            mprintf (ev, " in chat ");
            print_chat_name (ev, C, U->chat_id_);
          }
        }
        mprintf (ev, "\n");
        mpop_color (ev);

        mprint_end (ev);
      }
    }
    break;
  case CODE_UpdateUserStatus:
    {
      struct TdUpdateUserStatus *U = (void *)Upd;

      if ((!disable_output || arg) && log_level >= 3) {       
        mprint_start (ev);
        mpush_color (ev, COLOR_YELLOW);

        print_date (ev, time (0));
        print_user_name (ev, NULL, U->user_id_);
        mprintf (ev, " is now ");
        print_user_status (ev, U->status_);
        mprintf (ev, "\n");
        mpop_color (ev);

        mprint_end (ev);
      }
    }
    break;
  case CODE_UpdateUser:
    break;
  case CODE_UpdateGroup:
    break;
  case CODE_UpdateChannel:
    break;
  case CODE_UpdateSecretChat:
    break;
  case CODE_UpdateChannelFull:
    break;
  case CODE_UpdateUserBlocked:
    break;
  /*case CODE_UpdateNewAuthorization:
    if ((!disable_output || arg) && log_level >= 0) {       
      struct TdUpdateNewAuthorization *U = (void *)Upd;
      mprint_start (ev);
      mpush_color (ev, COLOR_YELLOW);

      print_date (ev, time (0));
      mprintf (ev, "New authorization: ");
      print_date (ev, U->date_);
      mprintf (ev, " device=%s location=%s\n", U->device_, U->location_);
      mpop_color (ev);

      mprint_end (ev);
    }
    break;*/
  case CODE_UpdateFileProgress:
    break;
  case CODE_UpdateFile:
    break;
  case CODE_UpdateOption:
    break;
  case CODE_UpdateSavedAnimations:
    break;
  case CODE_UpdateNewInlineQuery:
    break;
  case CODE_UpdateNewChosenInlineResult:
    break;
  case CODE_UpdateNewCallbackQuery:
    break;
  case CODE_UpdateNewInlineCallbackQuery:
    break;
  case CODE_UpdateServiceNotification:
    {
      struct TdUpdateServiceNotification *U = (void *)Upd;
      mprint_start (ev);
      mpush_color (ev, COLOR_REDB);
      mprintf (ev, "Service notification: type=%s ", U->type_);
      print_message_content (ev, NULL, U->content_);
      mprintf (ev, "\n");
      mpop_color (ev);
      mprint_end (ev);
    }
    break;
  case CODE_UpdatePrivacy:
    break;
  case CODE_UpdateFileGenerationStart:
    break;
  case CODE_UpdateFileGenerationProgress:
    break;
  case CODE_UpdateFileGenerationFinish:
    break;
  case CODE_UpdateRecentStickers:
    break;
  case CODE_UpdateTrendingStickerSets:
    break;
  case CODE_UpdateStickerSets:
    break;
  case CODE_UpdateAuthState:
    break;
  case CODE_UpdateMessageSendAcknowledged:
    break;
  case CODE_UpdateChatIsPinned:
    break;
  case CODE_UpdateOpenMessageContent:
    break;
  case CODE_UpdateNewCustomQuery:
    break;
  case CODE_UpdateNewCustomEvent:
    break;
  }
}

void do_update (struct TdUpdate *U) {
  if (!enable_json) {
    default_update_handler (NULL, U);
  } else {
    #ifdef USE_JSON
    json_update_cb (NULL, U);
    #endif
  }

  struct update_subscriber *a = update_subscribers_queue.next;
  while (a && a != &update_subscribers_queue) {
    a->cb (a->extra, U);
    a = a->next;
  }
}

void updates_handler (void *TLS, void *arg, struct TdUpdate *Upd) {
  switch ((enum List_Update)Upd->ID) {
  case CODE_UpdateNewMessage:
    {
      struct TdUpdateNewMessage *U = (void *)Upd;
      on_message_update (U->message_);
    }
    break;
  case CODE_UpdateMessageSendSucceeded:
    {
      struct TdUpdateMessageSendSucceeded *U = (void *)Upd;
      on_message_update (U->message_);
    }
    break;
  case CODE_UpdateMessageSendFailed:
    break;
  case CODE_UpdateMessageContent:
    {
      struct TdUpdateMessageContent *U = (void *)Upd;
      struct TdMessage *M = get_message (U->chat_id_, U->message_id_);
      if (M) {
        TdDestroyObjectMessageContent (M->content_);
        M->content_ = U->new_content_;
        if (M->content_) {
          __sync_fetch_and_add (&M->content_->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateMessageEdited:
    {
      struct TdUpdateMessageEdited *U = (void *)Upd;
      struct TdMessage *M = get_message (U->chat_id_, U->message_id_);
      if (M) {
        M->edit_date_ = U->edit_date_;
        TdDestroyObjectReplyMarkup (M->reply_markup_);
        M->reply_markup_ = U->reply_markup_;
        if (M->reply_markup_) {
          __sync_fetch_and_add (&M->reply_markup_->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateMessageViews:
    {
      struct TdUpdateMessageViews *U = (void *)Upd;
      struct TdMessage *M = get_message (U->chat_id_, U->message_id_);
      if (M) {
        M->views_ = U->views_;
      }
    }
    break;
  case CODE_UpdateChat:
    {
      struct TdUpdateChat *U = (void *)Upd;
      on_chat_update (U->chat_);
    }
    break;
  case CODE_UpdateChatTopMessage:
    {
      struct TdUpdateChatTopMessage *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        if (C->top_message_) {
          TdDestroyObjectMessage (C->top_message_);          
        }
        C->top_message_ = U->top_message_;
        if (U->top_message_) {
          __sync_fetch_and_add (&U->top_message_->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateChatOrder:
    {
      struct TdUpdateChatOrder *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        C->order_ = U->order_;
      }
    }
    break;
  case CODE_UpdateChatTitle:
    {
      struct TdUpdateChatTitle *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        if (C->title_) {
          free (C->title_);
        }
        C->title_ = U->title_ ? strdup (U->title_) : NULL;
        on_chat_update (C);
      }
    }
    break;
  case CODE_UpdateChatPhoto:
    {
      struct TdUpdateChatPhoto *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        if (C->photo_) {
          TdDestroyObjectChatPhoto (C->photo_);
        }
        C->photo_ = U->photo_;
        if (C->photo_) {
          __sync_fetch_and_add (&C->photo_->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateChatReadInbox:
    {
      struct TdUpdateChatReadInbox *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        C->unread_count_ = U->unread_count_;
        C->last_read_inbox_message_id_ = U->last_read_inbox_message_id_;
      }
    }
    break;
  case CODE_UpdateChatReadOutbox:
    {
      struct TdUpdateChatReadOutbox *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        C->last_read_outbox_message_id_ = U->last_read_outbox_message_id_;
      }
    }
    break;
  case CODE_UpdateChatReplyMarkup:
    {
      struct TdUpdateChatReplyMarkup *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        C->reply_markup_message_id_ = U->reply_markup_message_id_;
      }
    }
    break;
  case CODE_UpdateChatDraftMessage:
    {
      struct TdUpdateChatDraftMessage *U = (void *)Upd;
      struct TdChat *C = get_chat (U->chat_id_);

      if (C) {
        if (C->draft_message_) {
          TdDestroyObjectDraftMessage (C->draft_message_);
        }
        C->draft_message_ = U->draft_message_;
        if (C->draft_message_) {
          __sync_fetch_and_add (&C->draft_message_->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateNotificationSettings:
    break;
  case CODE_UpdateDeleteMessages:
    break;
  case CODE_UpdateUserAction:
    break;
  case CODE_UpdateUserStatus:
    {
      struct TdUpdateUserStatus *U = (void *)Upd;
      struct TdUser *C = get_user (U->user_id_);
      if (C) {
        if (C->status_) {
          TdDestroyObjectUserStatus (C->status_);
        }
        C->status_ = U->status_;
        if (C->status_) {
          __sync_fetch_and_add (&C->status_->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateUser:
    {
      struct TdUpdateUser *U = (void *)Upd;
      on_user_update (U->user_);
    }
    break;
  case CODE_UpdateGroup:
    {
      struct TdUpdateGroup *U = (void *)Upd;
      on_group_update (U->group_);
    }
    break;
  case CODE_UpdateChannel:
    {
      struct TdUpdateChannel *U = (void *)Upd;
      on_channel_update (U->channel_);
    }
    break;
  case CODE_UpdateSecretChat:
    {
      struct TdUpdateSecretChat *U = (void *)Upd;
      on_secret_chat_update (U->secret_chat_);
    }
    break;
  case CODE_UpdateChannelFull:
    {
      struct TdUpdateChannelFull *U = (void *)Upd;
      on_channel_update (U->channel_full_->channel_);
      struct tdcli_peer PB;
      PB.peer_type = CODE_Channel;
      PB.peer_id = U->channel_full_->channel_->id_;
      struct tdcli_peer *P = tree_lookup_peer_chat (tdcli_peers, &PB);

      if (P) {
        if (P->channel_full) {
          TdDestroyObjectChannelFull (P->channel_full);
        }
        P->channel_full = U->channel_full_;
        if (P->channel_full) {
          __sync_fetch_and_add (&P->channel_full->refcnt, 1);
        }
      }
    }
    break;
  case CODE_UpdateUserBlocked:
    {
      struct TdUpdateUserBlocked *U = (void *)Upd;
      struct TdUserFull *C = get_user_full (U->user_id_);
      if (C) {
        C->is_blocked_ = U->is_blocked_;
      }
    }
    break;
  //case CODE_UpdateNewAuthorization:
  //  break;
  case CODE_UpdateFileProgress:
    break;
  case CODE_UpdateFile:
    {
      struct TdUpdateFile *U = (void *)Upd;
      if (U->file_->path_) {
        struct file_wait *W = tree_lookup_file_wait (file_wait_tree, (void *)&U->file_->id_);
        if (W) {
          struct file_wait_cb *cb = W->first_cb;
          while (cb) {
            cb->callback (TLS, cb->callback_extra, (void *)U);
            struct file_wait_cb *n = cb->next;
            free (cb);
            cb = n;
          }
          file_wait_tree = tree_delete_file_wait (file_wait_tree, W);
          free (W);
        }
      }
    }
    break;
  case CODE_UpdateOption:
    {
      struct TdUpdateOption *U = (void *)Upd;
      if (!strcmp (U->name_, "my_id")) {
        assert (U->value_->ID == CODE_OptionInteger);
        struct TdOptionInteger *O = (void *)U->value_;
        my_id = O->value_;
      } else if (!strcmp (U->name_, "connection_state")) {
        assert (U->value_->ID == CODE_OptionString);
        struct TdOptionString *O = (void *)U->value_;
        if (conn_state) {
          free (conn_state);
        }
        conn_state = strdup (O->value_);
        update_prompt ();
      }
    }
    break;
  case CODE_UpdateSavedAnimations:
    break;
  case CODE_UpdateNewInlineQuery:
    break;
  case CODE_UpdateNewChosenInlineResult:
    break;
  case CODE_UpdateNewCallbackQuery:
    break;
  case CODE_UpdateNewInlineCallbackQuery:
    break;
  case CODE_UpdateServiceNotification:
    break;
  case CODE_UpdatePrivacy:
    break;
  case CODE_UpdateFileGenerationStart:
    break;
  case CODE_UpdateFileGenerationProgress:
    break;
  case CODE_UpdateFileGenerationFinish:
    break;
  case CODE_UpdateRecentStickers:
    break;
  case CODE_UpdateTrendingStickerSets:
    break;
  case CODE_UpdateStickerSets:
    break;
  case CODE_UpdateAuthState:
    break;
  case CODE_UpdateMessageSendAcknowledged:
    break;
  case CODE_UpdateChatIsPinned:
    break;
  case CODE_UpdateOpenMessageContent:
    break;
  case CODE_UpdateNewCustomQuery:
    break;
  case CODE_UpdateNewCustomEvent:
    break;
  /*default:
    {
      mprint_start (NULL);
      char *s = TdSerializeUpdate ((struct TdUpdate *)Upd);
      mprintf (NULL, "Unhandled update:\n%s\n", s);
      free (s);
      mprint_end (NULL);
    }
    break;*/
  }
  do_update (Upd);
}

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
    A->chat_id = NOT_FOUND;
    if (opt) {
      line_ptr = save;
      return -5;
    }
    return -1;
  } else if (cur_token_end_str) {
    A->chat_id = NOT_FOUND;
    return -3;
  } else {
    int m = tdcli_any;
    if (op == ca_user) { m = tdcli_user; }
    if (op == ca_group) { m = tdcli_group; }
    if (op == ca_channel) { m = tdcli_channel; }
    //if (op == ca_secret_chat) { m = tdlcli_secret_chat; }            

    char *token = strndup (cur_token, cur_token_len);
    long long chat_id = cur_token_peer (token, m, cmd);
    free (token);
            
    if (chat_id == WAIT_AIO) {
      return -2;
    }

    if (chat_id == NOT_FOUND) {
      if (opt) {
        line_ptr = save;
        return -5;
      } else {
        return -1;
      }
    } else { 
      A->chat_id = chat_id;
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
  if (cmd->chat_mode_chat_id) {
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


  cmd->refcnt ++;
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

    if (!cmd->cb) {
      #ifdef USE_JSON
      cmd->cb = enable_json ? json_universal_cb : command->default_cb;    
      #else
      cmd->cb = command->default_cb;    
      #endif
    }
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
  cmd->chat_mode_chat_id = cur_chat_mode_chat_id;
  cmd->refcnt = 1;
  allocated_commands ++;
  in_readline = 1;
  interpreter_ex (cmd);
  in_readline = 0;
  in_command_decref (cmd);

  need_prompt_update = 1;
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
  
    #ifdef READLINE_GNU
    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
    #else
    printf ("\033[2K\r");
    set_prompt ("");
    //rl_line_buffer[0] = 0;
    //rl_point = 0;
    //rl_redisplay();
    #endif
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
    fflush (stdout);
    set_prompt (get_default_prompt ());
    #ifdef READLINE_GNU
    rl_replace_line(saved_line, 0);
    rl_point = saved_point;
    rl_redisplay();
    #else
    //rl_line_buffer = strdup (saved_line);
    //rl_point = saved_point;
    rl_forced_update_display();
    #endif
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
  need_prompt_update = 1;
}

/*void hexdump (int *in_ptr, int *in_end) {
  mprint_start (ev);
  int *ptr = in_ptr;
  while (ptr < in_end) { mprintf (ev, " %08x", *(ptr ++)); }
  mprintf (ev, "\n");
  mprint_end (ev); 
}*/

char log_buf[1 << 20];

void logprintf (const char *format, ...) {
  va_list ap;
  va_start (ap, format);
  vsnprintf (log_buf, (1 << 20) - 1, format, ap);
  va_end (ap);

  double T = (double)time (0);
  //mprint_start (notify_ev);
  fprintf (stderr, " *** %.6lf %s\n", T, log_buf);
  //mprint_end (notify_ev);
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

void print_send_message_action (struct in_ev *ev, struct TdSendMessageAction *action) {
  switch ((enum List_SendMessageAction)action->ID) {
  case CODE_SendMessageTypingAction:
    mprintf (ev, "is typing");
    break;
  case CODE_SendMessageCancelAction:
    mprintf (ev, "deleted typed message");
    break;
  case CODE_SendMessageRecordVideoAction:
    mprintf (ev, "is recording video");
    break;
  case CODE_SendMessageUploadVideoAction:
    mprintf (ev, "is uploading video: progress %d%%", ((struct TdSendMessageUploadVideoAction *)action)->progress_);
    break;
  case CODE_SendMessageRecordVoiceAction:
    mprintf (ev, "is recording voice message");
    break;
  case CODE_SendMessageUploadVoiceAction:
    mprintf (ev, "is uploading voice message: progress %d%%", ((struct TdSendMessageUploadVoiceAction *)action)->progress_);
    break;
  case CODE_SendMessageUploadPhotoAction:
    mprintf (ev, "is uploading photo: progress %d%%", ((struct TdSendMessageUploadPhotoAction *)action)->progress_);
    break;
  case CODE_SendMessageUploadDocumentAction:
    mprintf (ev, "is uploading document: progress %d%%", ((struct TdSendMessageUploadDocumentAction *)action)->progress_);
    break;
  case CODE_SendMessageGeoLocationAction:
    mprintf (ev, "is choosing geo location");
    break;
  case CODE_SendMessageChooseContactAction:
    mprintf (ev, "is choosing contact");
    break;
  case CODE_SendMessageStartPlayGameAction:
    mprintf (ev, "started to play game");
    break;
  }
}

void print_user_status (struct in_ev *ev, struct TdUserStatus *status) {
  switch ((enum List_UserStatus)status->ID) {
  case CODE_UserStatusEmpty:
    mprintf (ev, "offline");
    break;
  case CODE_UserStatusOnline:
    mprintf (ev, "online");
    break;
  case CODE_UserStatusOffline:
    mprintf (ev, "offline (was online at ");
    print_date (ev, ((struct TdUserStatusOffline *)status)->was_online_);
    mprintf (ev, ")");
    break;
  case CODE_UserStatusRecently:
    mprintf (ev, "recently");
    break;
  case CODE_UserStatusLastWeek:
    mprintf (ev, "last week");
    break;
  case CODE_UserStatusLastMonth:
    mprintf (ev, "last month");
    break;
  }
}

void print_user_name (struct in_ev *ev, struct TdUser *U, int id) { 
  if (!U) {
    U = get_user (id);        
  }
  if (!U) {
    struct TdNullaryObject *R = TdCClientSendCommandSync (TLS, (void *)TdCreateObjectGetUser (id));

    if (R) {
      if (R->ID == CODE_User) {
        on_user_update ((void *)R);
        U = get_user (id);        
        assert (U == (void *)R);
        assert (U && U->ID == CODE_User);
      }
      TdDestroyObjectNullaryObject (R);
    }
  }
  if (U) {
    assert (U->ID == CODE_User);
    on_user_update (U);
  }
  if (U && U->my_link_->ID == CODE_LinkStateContact) {
    mpush_color (ev, COLOR_REDB);
  } else {
    mpush_color (ev, COLOR_RED);
  }

  if (U && (U->first_name_ || U->last_name_)) {
    if (U->first_name_) {
      if (U->last_name_) {
        mprintf (ev, "%s %s", U->first_name_, U->last_name_);
      } else {
        mprintf (ev, "%s", U->first_name_);
      }
    } else {
      mprintf (ev, "%s", U->last_name_);
    }
  } else {
    mprintf (ev, "user#id%d", U ? U->id_ : id);
  }
  
  mpop_color (ev);
}

void print_chat_name (struct in_ev *ev, struct TdChat *C, long long id) {
  if (!C) {
    struct TdNullaryObject *R = TdCClientSendCommandSync (TLS, (void *)TdCreateObjectGetChat (id));
    if (R) {
      if (R->ID == CODE_Chat) {
        on_chat_update ((void *)R);
        C = get_chat (id);
        assert (C);
      }
      TdDestroyObjectNullaryObject (R);
    }
  }

  if (!C) {
    mpush_color (ev, COLOR_RED);
    mprintf (ev, "chat#id%lld", id);
    mpop_color (ev);
  } else {   
    switch (C->type_->ID) {
      case CODE_PrivateChatInfo:
        print_user_name (ev, ((struct TdPrivateChatInfo *)(C->type_))->user_, 0);
        return;
      case CODE_GroupChatInfo:
        mpush_color (ev, COLOR_MAGENTA);
        mprintf (ev, "%s", C->title_);
        mpop_color (ev);
        break;
      case CODE_ChannelChatInfo:
        if (((struct TdChannelChatInfo *)(C->type_))->channel_->is_supergroup_) {
          mpush_color (ev, COLOR_MAGENTA);
        } else {
          mpush_color (ev, COLOR_CYAN);
        }
        mprintf (ev, "%s", C->title_);
        mpop_color (ev);
        break;
      case CODE_SecretChatInfo:
        mpush_color (ev, COLOR_LCYAN);
        mprintf (ev, "%s", C->title_);
        mpop_color (ev);
        break;
    }
  }
}

void print_animation (struct in_ev *ev, struct TdAnimation *animation) {
  mprintf (ev, "[animation %d", animation->animation_->id_);
  if (animation->file_name_) {
    mprintf (ev, " name=%s", animation->file_name_);
  }
  if (animation->mime_type_) {
    mprintf (ev, " type=%s", animation->mime_type_);
  }
  if (animation->width_ && animation->height_) {
    mprintf (ev, " size=%dx%d", animation->width_, animation->height_);
  }
  int size = animation->animation_->size_;

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

void print_audio (struct in_ev *ev, struct TdAudio *audio) {
  mprintf (ev, "[audio %d", audio->audio_->id_);
  if (audio->file_name_) {
    mprintf (ev, " name=%s", audio->file_name_);
  }
  if (audio->mime_type_) {
    mprintf (ev, " type=%s", audio->mime_type_);
  }
  if (audio->title_) {
    mprintf (ev, " title=%s", audio->title_);
  }
  if (audio->performer_) {
    mprintf (ev, " artist=%s", audio->performer_);
  }
  int size = audio->audio_->size_;

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

void print_document (struct in_ev *ev, struct TdDocument *document) {
  mprintf (ev, "[document %d", document->document_->id_);
  if (document->file_name_) {     
    mprintf (ev, " name=%s", document->file_name_);
  }
  if (document->mime_type_) {
    mprintf (ev, " type=%s", document->mime_type_);
  }
  int size = document->document_->size_;

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

void print_photo (struct in_ev *ev, struct TdPhoto *photo) {
  mprintf (ev, "[photo");
  if (photo->sizes_->len > 0) {
    int j;
    for (j = 0; j < photo->sizes_->len; j++) {
      struct TdPhotoSize *t = photo->sizes_->data[j];
      if (t->width_ != 0 || t->height_ != 0) {
        mprintf (ev, " [photo_size %d size=%dx%d]", t->photo_->id_, t->width_, t->height_);
      }
    }
  }

  mprintf (ev, "]");
}

void print_sticker (struct in_ev *ev, struct TdSticker *sticker) {
  mprintf (ev, "[sticker %d", sticker->sticker_->id_);
  if (sticker->emoji_) {
    mprintf (ev, " emoji=%s", sticker->emoji_);
  }
  mprintf (ev, " set_id=%lld", sticker->set_id_);
  if (sticker->width_ && sticker->height_) {
    mprintf (ev, " size=%dx%d", sticker->width_, sticker->height_);
  }

  int size = sticker->sticker_->size_;

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

void print_video (struct in_ev *ev, struct TdVideo *video) {
  mprintf (ev, "[video %d", video->video_->id_);
  if (video->file_name_) {     
    mprintf (ev, " name=%s", video->file_name_);
  }
  if (video->mime_type_) {
    mprintf (ev, " type=%s", video->mime_type_);
  }
  if (video->height_ && video->width_) {
    mprintf (ev, " size=%dx%d", video->width_, video->height_);
  }
  if (video->duration_) {
    mprintf (ev, " duration=%d", video->duration_);
  }
  int size = video->video_->size_;

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

void print_voice (struct in_ev *ev, struct TdVoice *voice) {
  mprintf (ev, "[voice %d", voice->voice_->id_);
  if (voice->mime_type_) {
    mprintf (ev, " type=%s", voice->mime_type_);
  }
  if (voice->duration_) {
    mprintf (ev, " duration=%d", voice->duration_);
  }
  int size = voice->voice_->size_;

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

void print_location (struct in_ev *ev, struct TdLocation *C) {
  mprintf (ev, "[location https://maps.google.com/?q=%.6lf,%.6lf", C->latitude_, C->longitude_);
}

void print_venue (struct in_ev *ev, struct TdVenue *C) {
  mprintf (ev, "[venue https://maps.google.com/?q=%.6lf,%.6lf", C->location_->latitude_, C->location_->longitude_);
  if (C->title_) {
    mprintf (ev, "title=%s", C->title_);
  }
  if (C->address_) {
    mprintf (ev, "address=%s", C->address_);
  }
  mprintf (ev, "]");
}

void print_contact (struct in_ev *ev, struct TdContact *C) {
  mprintf (ev, "[contact %s", C->phone_number_);
  if (C->first_name_) {
    mprintf (ev, " %s", C->first_name_);
  }
  if (C->last_name_) {
    mprintf (ev, " %s", C->last_name_);
  }
  /*if (C->user_id) {
    struct tdl_user *U = tdlib_instant_get_user (TLS, C->user_id);
    if (U) {
      mprintf (ev, " ");
      print_user_name (ev, U, U->id);
    }
  }*/
  mprintf (ev, "]");
}

void print_game (struct in_ev *ev, struct TdGame *G) {
  mprintf (ev, "[game %lld \"%s\"]", G->id_, G->title_);
  if (G->text_) {
    mprintf (ev, " %s", G->text_);
  }
}

void print_message_animation (struct in_ev *ev, struct TdMessageAnimation *animation) {
  if (animation->caption_) {
    mprintf (ev, "%s ", animation->caption_);
  }
  print_animation (ev, animation->animation_);
}

void print_message_audio (struct in_ev *ev, struct TdMessageAudio *audio) {
  if (audio->caption_) {
    mprintf (ev, "%s ", audio->caption_);
  }
  print_audio (ev, audio->audio_);
}

void print_message_document (struct in_ev *ev, struct TdMessageDocument *document) {
  if (document->caption_) {
    mprintf (ev, "%s ", document->caption_);
  }
  print_document (ev, document->document_);
}

void print_message_photo (struct in_ev *ev, struct TdMessagePhoto *photo) {
  if (photo->caption_) {
    mprintf (ev, "%s ", photo->caption_);
  }
  print_photo (ev, photo->photo_);
}

void print_message_sticker (struct in_ev *ev, struct TdMessageSticker *sticker) {
  print_sticker (ev, sticker->sticker_);
}

void print_message_video (struct in_ev *ev, struct TdMessageVideo *video) {
  if (video->caption_) {
    mprintf (ev, "%s ", video->caption_);
  }
  print_video (ev, video->video_);
}

void print_message_voice (struct in_ev *ev, struct TdMessageVoice *voice) {
  if (voice->caption_) {
    mprintf (ev, "%s ", voice->caption_);
  }
  print_voice (ev, voice->voice_);
}

void print_message_location (struct in_ev *ev, struct TdMessageLocation *location) {
  print_location (ev, location->location_);
}

void print_message_venue (struct in_ev *ev, struct TdMessageVenue *venue) {
  print_venue (ev, venue->venue_);
}

void print_message_contact (struct in_ev *ev, struct TdMessageContact *contact) {
  print_contact (ev, contact->contact_);
}

void print_message_game (struct in_ev *ev, struct TdMessageGame *game) {
  print_game (ev, game->game_);
}

void print_message_text (struct in_ev *ev, struct TdMessageText *text) {
  mprintf (ev, "%s", text->text_);
  if (text->web_page_) {
      mprintf (ev, " [webpage %s: %s]", 
        text->web_page_->title_,
        text->web_page_->description_
      );
  }
}

void print_members (struct in_ev *ev, struct TdVectorUser *members) {
  mprintf (ev, "%d users:", members->len);
  int i;
  for (i = 0; i < members->len; i++) {
    mprintf (ev, " ");
    print_user_name (ev, members->data[i], members->data[i]->id_);
  }
}

void print_message_group_chat_create (struct in_ev *ev, struct TdMessageGroupChatCreate *act) {
  mprintf (ev, "Created group '%s' ", act->title_);
  print_members (ev, act->members_); 
}

void print_message_channel_chat_create (struct in_ev *ev, struct TdMessageChannelChatCreate *act) {
  mprintf (ev, "Created channel '%s' ", act->title_);
}

void print_message_chat_change_title (struct in_ev *ev, struct TdMessageChatChangeTitle *act) {
  mprintf (ev, "renamed to '%s'", act->title_);
}

void print_message_chat_change_photo (struct in_ev *ev, struct TdMessageChatChangePhoto *act) {
  mprintf (ev, "changed photo to ");
  print_photo (ev, act->photo_);
}

void print_message_chat_delete_photo (struct in_ev *ev, struct TdMessageChatDeletePhoto *act) {
  mprintf (ev, "deleted photo");
}

void print_message_chat_add_members (struct in_ev *ev, struct TdMessageChatAddMembers *act) {
  mprintf (ev, "added members ");
  print_members (ev, act->members_);
}

void print_message_chat_join_by_link (struct in_ev *ev, struct TdMessageChatJoinByLink *act) {
  mprintf (ev, "joined by link");
}

void print_message_chat_delete_member (struct in_ev *ev, struct TdMessageChatDeleteMember *act) {
  mprintf (ev, "kicked ");
  print_user_name (ev, act->user_, 0);
}

void print_message_chat_migrate_to (struct in_ev *ev, struct TdMessageChatMigrateTo *act) {
  mprintf (ev, "migrated to channel");
}

void print_message_chat_migrate_from (struct in_ev *ev, struct TdMessageChatMigrateFrom *act) {
  mprintf (ev, "migrated from group");
}

void print_message_pin_message (struct in_ev *ev, struct TdChat *C, struct TdMessagePinMessage *act) {
  mprintf (ev, "pinned message ");
  print_message_id (ev, C, act->message_id_);
}

void print_message_game_score (struct in_ev *ev, struct TdMessageGameScore *act) {
  mprintf (ev, "scored %d points in game %lld", act->score_, act->game_id_);
}

void print_message_screenshot_taken (struct in_ev *ev, struct TdMessageScreenshotTaken *act) {
  mprintf (ev, "taken screenshot");
}

void print_message_chat_set_ttl (struct in_ev *ev, struct TdMessageChatSetTtl *act) {
  mprintf (ev, "set ttl to %d", act->ttl_);
}

void print_message_unsupported (struct in_ev *ev, struct TdMessageUnsupported *act) {
  mprintf (ev, "[unsupported]");
}

void print_message_content (struct in_ev *ev, struct TdChat *chat, struct TdMessageContent *content) {
  switch ((enum List_MessageContent)content->ID) {
  case CODE_MessageAnimation:
    return print_message_animation (ev, (void *)content);
  case CODE_MessageAudio:
    return print_message_audio (ev, (void *)content);
  case CODE_MessageDocument:
    return print_message_document (ev, (void *)content);
  case CODE_MessagePhoto:
    return print_message_photo (ev, (void *)content);
  case CODE_MessageSticker:
    return print_message_sticker (ev, (void *)content);
  case CODE_MessageVideo:
    return print_message_video (ev, (void *)content);
  case CODE_MessageVoice:
    return print_message_voice (ev, (void *)content);
  case CODE_MessageLocation:
    return print_message_location (ev, (void *)content);
  case CODE_MessageVenue:
    return print_message_venue (ev, (void *)content);
  case CODE_MessageContact:
    return print_message_contact (ev, (void *)content);
  case CODE_MessageGame:
    return print_message_game (ev, (void *)content);
  case CODE_MessageText:
    return print_message_text (ev, (void *)content);
  case CODE_MessageGroupChatCreate:
    return print_message_group_chat_create (ev, (void *)content);
  case CODE_MessageChannelChatCreate:
    return print_message_channel_chat_create (ev, (void *)content);
  case CODE_MessageChatChangeTitle:
    return print_message_chat_change_title (ev, (void *)content);
  case CODE_MessageChatChangePhoto:
    return print_message_chat_change_photo (ev, (void *)content);
  case CODE_MessageChatDeletePhoto:
    return print_message_chat_delete_photo (ev, (void *)content);
  case CODE_MessageChatAddMembers:
    return print_message_chat_add_members (ev, (void *)content);
  case CODE_MessageChatJoinByLink:
    return print_message_chat_join_by_link (ev, (void *)content);
  case CODE_MessageChatDeleteMember:
    return print_message_chat_delete_member (ev, (void *)content);
  case CODE_MessageChatMigrateTo:
    return print_message_chat_migrate_to (ev, (void *)content);
  case CODE_MessageChatMigrateFrom:
    return print_message_chat_migrate_from (ev, (void *)content);
  case CODE_MessagePinMessage:
    return print_message_pin_message (ev, chat, (void *)content);
  case CODE_MessageGameScore:
    return print_message_game_score (ev, (void *)content);
  case CODE_MessageScreenshotTaken:
    return print_message_screenshot_taken (ev, (void *)content);
  case CODE_MessageChatSetTtl:
    return print_message_chat_set_ttl (ev, (void *)content);
  case CODE_MessageUnsupported:
    return print_message_unsupported (ev, (void *)content);
  /*default:
    {
      char *s = TdSerializeMessageContent (content);
      logprintf ("Can not parse message media: %s\n", s);
      free (s);
    }*/
  }
}


void print_message_id (struct in_ev *ev, struct TdChat *C, long long id) {
  if (permanent_msg_id_mode) {
    mprintf (ev, "%lld", id);
  } else {
    mprintf (ev, "%d", convert_global_to_local (C->id_, id)->local_id);
  }
}

void print_message (struct in_ev *ev, struct TdMessage *M) {
  assert (M);
  on_message_update (M);

  struct TdChat *C = get_chat (M->chat_id_);

  if (M->sender_user_id_ == my_id) {
    mpush_color (ev, COLOR_GREEN);
  } else {
    mpush_color (ev, COLOR_BLUE);
  }
  print_date (ev, M->date_);
  mprintf (ev, " ");
  print_message_id (ev, C, M->id_);
  mprintf (ev, " ");

  print_chat_name (ev, C, M->chat_id_);

  if (M->sender_user_id_ > 0 && (!C || C->type_->ID != CODE_PrivateChatInfo)) {
    mprintf (ev, " ");
    struct TdUser *U = get_user (M->sender_user_id_);
    print_user_name (ev, U, M->sender_user_id_);
  }

  if (M->sender_user_id_ == my_id) {
    if (C && M->id_ <= C->last_read_outbox_message_id_) {
      mprintf (ev, " ««« "); 
    } else {
      mprintf (ev, " <<< "); 
    }
  } else {
    if (C && M->id_ <= C->last_read_inbox_message_id_) {
      mprintf (ev, " »»» ");
    } else {
      mprintf (ev, " >>> ");
    }
  }

  if (M->forward_info_) {
    mprintf (ev, "[fwd ");  
    if (M->forward_info_->ID == CODE_MessageForwardedFromUser) {
      struct TdMessageForwardedFromUser *FF = (void *)M->forward_info_;
      
      struct TdUser *U = get_user (FF->sender_user_id_);
      print_user_name (ev, U, FF->sender_user_id_);
      mprintf (ev, " ");
      print_date (ev, FF->date_);
    } else {
      struct TdMessageForwardedPost *FF = (void *)M->forward_info_;
      struct TdChat *C = get_chat (FF->chat_id_);
      print_chat_name (ev, C, FF->chat_id_);
      mprintf (ev, "  ");
      if (FF->sender_user_id_) {
        struct TdUser *U = get_user (FF->sender_user_id_);
        print_user_name (ev, U, FF->sender_user_id_);
      }
      mprintf (ev, " ");
      print_date (ev, FF->date_);
    }

    mprintf (ev, "] ");
  }
  
  if (M->reply_to_message_id_) {
    mprintf (ev, "[reply ");
    print_message_id (ev, C, M->reply_to_message_id_);    
    mprintf (ev, "] ");    
  }

  if (M->via_bot_user_id_) {
    struct TdUser *U = get_user (M->via_bot_user_id_); 
    if (U && U->username_) {
      mprintf (ev, "[via @%s] ", U->username_);
    }
  }

  print_message_content (ev, C, M->content_);

  if (M->reply_markup_ && M->reply_markup_->ID == CODE_ReplyMarkupInlineKeyboard) {
    struct TdReplyMarkupInlineKeyboard *R = (void *)M->reply_markup_;

    int i, j;
    int p = 0;
    for (i = 0; i < R->rows_->len; i++) {
      for (j = 0; j < R->rows_->data[i]->len; j++) {
        struct TdInlineKeyboardButton *B = R->rows_->data[i]->data[j];
        mprintf (ev, "\n");
        mprintf (ev, "\tButton %d: %s", ++p, B->text_);
        switch ((enum List_InlineKeyboardButtonType)B->type_->ID) {
        case CODE_InlineKeyboardButtonTypeUrl:
          mprintf (ev, " <%s>", ((struct TdInlineKeyboardButtonTypeUrl *)B->type_)->url_);
          break;
        case CODE_InlineKeyboardButtonTypeCallback:
          mprintf (ev, " <callback>");
          break;
        case CODE_InlineKeyboardButtonTypeCallbackGame:
          mprintf (ev, " <game>");
          break;
        case CODE_InlineKeyboardButtonTypeSwitchInline:
          mprintf (ev, " <switch inline %s>", ((struct TdInlineKeyboardButtonTypeSwitchInline *)B->type_)->query_);
          break;
        }
      }
    }
  }
  
  mpop_color (ev);
  assert (!color_stack_pos);
  mprintf (ev, "\n");
}

void play_sound (void) {
  printf ("\a");
}


#ifndef READLINE_GNU
char **tdcli_attempted_completion (const char *text, int start, int end) {
  return completion_matches (text, command_generator);
}
#endif

void set_interface_callbacks (void) {
  if (readline_disabled) { return; }
  readline_active = 1;
  #ifdef READLINE_GNU
  rl_filename_quote_characters = strdup (" ");
  #else
  int res = rl_initialize ();
  assert (res >= 0);
  #endif
  rl_basic_word_break_characters = strdup (" ");
  
  
  rl_callback_handler_install (get_default_prompt (), interpreter);
  #ifdef READLINE_GNU
  rl_completion_entry_function = command_generator;
  #else
  rl_attempted_completion_function = tdcli_attempted_completion;
  #endif
}
