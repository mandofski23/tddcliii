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
#ifndef __INTERFACE_H__
#define __INTERFACE_H__

#include <stdlib.h>

#include "td/telegram/td_c_client.h"
#include "telegram-layout.h"

union tdl_chat;

#define COLOR_RED "\033[0;31m"
#define COLOR_REDB "\033[1;31m"
#define COLOR_NORMAL "\033[0m"
#define COLOR_GREEN "\033[32;1m"
#define COLOR_GREY "\033[37;1m"
#define COLOR_YELLOW "\033[33;1m"
#define COLOR_BLUE "\033[34;1m"
#define COLOR_MAGENTA "\033[35;1m"
#define COLOR_CYAN "\033[36;1m"
#define COLOR_LCYAN "\033[0;36m"

#define COLOR_INVERSE "\033[7m"

char *get_default_prompt (void);
char *complete_none (const char *text, int state);
char **complete_text (char *text, int start, int end);
void interpreter (char *line);
struct in_command;
void interpreter_ex (struct in_command *cmd);

void rprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void logprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));

#define vlogprintf(v,...) \
  do { \
    if (TLS->verbosity >= (v)) {\
      logprintf (__VA_ARGS__);\
    }\
  } while (0);\


//void hexdump (int *in_ptr, int *in_end);

struct bufferevent;
struct in_ev {
  struct bufferevent *bev;
  char in_buf[4096];
  ssize_t in_buf_pos;
  int refcnt;
  int error;
  int fd;
};


struct tdl_user;
struct tdl_chat_info;
struct tdl_message;
struct in_ev;
void print_message (struct in_ev *ev, struct TdMessage *M);
void print_message_id (struct in_ev *ev, struct TdChat *C, long long id);
void print_user_name (struct in_ev *ev, struct TdUser *U, int id);
void print_chat_name (struct in_ev *ev, struct TdChat *C, long long id);
void print_game (struct in_ev *ev, struct TdGame *G);
void print_channel_name (struct in_ev *ev, struct TdChannel *C, int id);
void print_user_status (struct in_ev *ev, struct TdUserStatus *S);
void print_send_message_action (struct in_ev *ev, struct TdSendMessageAction *action);
void print_message_content (struct in_ev *ev, struct TdChat *chat, struct TdMessageContent *content);
/*void print_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
void print_channel_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
void print_user_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *U);
void print_encr_chat_name_full (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);
void print_encr_chat_name (struct in_ev *ev, tgl_peer_id_t id, tgl_peer_t *C);*/
//void print_media (struct tdl_message_media *M);
//
void pop_color (void);
void push_color (const char *color);
void print_start (void);
void print_end (void);
void print_date_full (struct in_ev *ev, long t);
void print_date (struct in_ev *ev, long t);

void play_sound (void);
void update_prompt (void);
void set_interface_callbacks (void);

struct res_arg;
struct in_command {
  char *line;
  struct in_ev *ev;
  int refcnt;
  long long query_id;
  char *str_query_id;
  long long chat_mode_chat_id;
  //struct tdl_chat_info *chat_mode_chat;
  struct command *cmd;
  void *extra;
  void (*cb)(struct in_command *, struct TdNullaryObject *result);
};
void in_command_decref (struct in_command *cmd);

enum result_argument {
  ra_none,
  ra_user,
  ra_group,
  ra_secret_chat,
  ra_channel,
  ra_peer,
  ra_chat,
  ra_message,
  ra_int,
  ra_chat_member,
  ra_string,
  ra_photo,
  ra_invite_link_info,
 
  ra_vector = 128
};

struct result_argument_desc {
  char *name;
  enum result_argument type;
};

struct res_arg {
  int flags;
  union {
    struct {
      int vec_len;
      struct res_arg *vec;
    };
    struct tdl_user *user;
    struct tdl_group *group;
    struct tdl_secret_chat *secret_chat;
    struct tdl_channel *channel;
    union tdl_chat *peer;
    struct tdl_chat_info *chat;
    struct tdl_message *message;
    int num;
    struct tdl_chat_member *chat_member;
    char *str;
    struct tdl_photo *photo;
    struct tdl_chat_invite_link_info *invite_link_info;
    void *ptr;
  };
};

enum command_argument {
  ca_none,
  ca_user,
  ca_group,
  ca_secret_chat,
  ca_channel,
  ca_chat,
  ca_file_name,
  ca_file_name_end,
  ca_number,
  ca_string_end,
  ca_msg_string_end,
  ca_modifier,
  ca_command,
  ca_extf,
  ca_msg_id,
  ca_double,
  ca_string,
  ca_media_type,


  ca_optional = 256,
  ca_period = 512
};

struct command_argument_desc {
  char *name;
  enum command_argument type;
};

struct arg {
  int flags;
  union {
    //tgl_peer_t *P;
    //struct tdl_message *M;
    char *str;
    long long num;
    double dval;
    tdl_message_id_t msg_id;
    long long chat_id;
    int user_id;
    int channel_id;
    int group_id;
    int secret_chat_id;

    struct {
      int vec_len;
      struct arg *vec;
    };
  };
};

enum tdcli_chat_type {
  tdcli_any = 0,
  tdcli_user = CODE_User,
  tdcli_group = CODE_Group,
  tdcli_channel = CODE_Channel,
  tdcli_secret_chat = CODE_SecretChat
};

typedef void (*tdlib_cb_t)(struct in_command *, struct TdNullaryObject *result);
struct command {
  char *name;
  struct command_argument_desc args[10];
  struct result_argument_desc res_args[10];
  void (*fun)(struct command *command, int arg_num, struct arg args[], struct in_command *cmd);
  tdlib_cb_t default_cb;
  char *desc;
  void *arg;
  long params[10];
};

struct tdcli_peer;
struct chat_alias {
  char *name;
  struct tdcli_peer *peer;
  struct chat_alias *next, *prev;
};

struct update_description {
  void (*default_cb)(void *extra, struct TdUpdate *update);
  char *name;
};

struct chat_alias *get_by_alias (const char *name);
//char *print_permanent_msg_id (tdl_message_id_t id);
//char *print_permanent_peer_id (tgl_peer_id_t id);
//tgl_peer_id_t parse_input_peer_id (const char *s, int l, int mask);
//tdl_message_id_t parse_input_msg_id (const char *s, int l);
void fail_interface (void *TLS, struct in_command *cmd, int error_code, const char *format, ...) __attribute__ (( format (printf, 4, 5)));
long long find_modifier (int arg_num, struct arg args[], const char *name, int type);
struct update_subscriber *subscribe_updates (void *extra, void (*on_update)(void *, struct TdUpdate *));
void tdcli_cb (void *instance, void *extra, struct TdNullaryObject *result);

void updates_handler (void *instance, void *arg, struct TdUpdate *result);

extern int bot_mode;
extern int verbosity;
extern int msg_num_mode;
extern int log_level;
#define NOT_FOUND (int)0x80000000
#endif
