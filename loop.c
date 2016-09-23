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

#if USE_PYTHON
#include "python-tg.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define READLINE_CALLBACKS

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef READLINE_GNU
#include <readline/readline.h>
#else
#include <editline/readline.h>
#endif

#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>

#ifdef EVENT_V2
#include <event2/event.h>
#include <event2/bufferevent.h>
#else
#include <event.h>
#include "event-old.h"
#endif

#include "interface.h"
#include "telegram.h"
#include "loop.h"
#if USE_LUA
#include "lua-tg.h"
#endif

#include <pthread.h>

struct event_base *ev_base; 
int read_pipe;
int write_pipe;

extern int verbosity;
extern int readline_disabled;
extern char *bot_hash;

extern int bot_mode;

extern int safe_quit;

extern int disable_output;

extern int sfd;
extern int usfd;

void got_it (char *line, int len);

static char *line_buffer;
static ssize_t line_buffer_size;
static ssize_t line_buffer_pos;
static int delete_stdin_event;

extern volatile int sigterm_cnt;

extern char *start_command;
extern void *TLS;
extern int ipv6_enabled;

struct event *term_ev = 0;
int read_one_string;
#define MAX_ONE_STRING_LEN 511
char one_string[MAX_ONE_STRING_LEN + 1];
int one_string_len;
void (*one_string_cb)(void *TLS, const char *string, void *arg);

void *one_string_cb_arg;
char *one_string_prompt;
int one_string_flags;
extern int disable_link_preview;

void deactivate_readline (void);
void reactivate_readline (void);

static void one_string_read_end (void) {
  printf ("\n");
  fflush (stdout);

  read_one_string = 0;
  free (one_string_prompt);
  one_string_prompt = NULL;
  reactivate_readline ();

  one_string_cb (TLS, one_string, one_string_cb_arg);
}

void do_get_string (void *TLS, char *prompt, int flags) {
  deactivate_readline ();
  one_string_prompt = strdup (prompt);
  printf ("%s", one_string_prompt);
  fflush (stdout);
  read_one_string = 1;
  one_string_len = 0;  
  one_string_flags = flags;
}

static void stdin_read_callback (evutil_socket_t fd, short what, void *arg) {
  if (!readline_disabled && !read_one_string) {
    //logprintf ("rl_callback_read_char ()\n");
    rl_callback_read_char ();
    #ifndef READLINE_GNU
    //rl_redisplay ();
    #endif
    return;
  }
  if (read_one_string) {
    char c;
    ssize_t r = read (0, &c, 1);
    if (r <= 0) {
      perror ("read");
      delete_stdin_event = 1;
      return;
    }
    if (c == '\n' || c == '\r') {
      one_string[one_string_len] = 0;
      one_string_read_end ();
      return;
    }
    if (one_string_len < MAX_ONE_STRING_LEN) {
      one_string[one_string_len ++] = c;
      if (!(one_string_flags & 1)) {
        printf ("%c", c);
        fflush (stdout);
      }
    }
    return;
  }

  if (line_buffer_pos == line_buffer_size) {
    line_buffer = realloc (line_buffer, line_buffer_size * 2 + 100);
    assert (line_buffer);
    line_buffer_size = line_buffer_size * 2 + 100;
    assert (line_buffer);
  }
  ssize_t r = read (0, line_buffer + line_buffer_pos, line_buffer_size - line_buffer_pos);
  if (r <= 0) {
    perror ("read");
    delete_stdin_event = 1;
    return;
  }
  line_buffer_pos += r;

  while (1) {
    int p = 0;
    while (p < line_buffer_pos && line_buffer[p] != '\n') { p ++; }
    if (p < line_buffer_pos) {
      line_buffer[p] = 0;
      interpreter (line_buffer);
      memmove (line_buffer, line_buffer + p + 1, line_buffer_pos - p - 1);
      line_buffer_pos -= (p + 1);
    } else {
      break;
    }
  }
}

/*struct event *timer;
static void timer_empty_cb (evutil_socket_t fd, short what, void *arg) {
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  event_add (timer, &tv);
}*/

int need_update;
static void pipe_read_cb (evutil_socket_t fd, short what, void *arg) {
  need_update = 1;
  int x;
  read (fd, &x, 4);
}

extern int need_prompt_update;

void net_loop (void) {
  delete_stdin_event = 0;
  logprintf ("Starting netloop\n");
  term_ev = event_new (ev_base, 0, EV_READ | EV_PERSIST, stdin_read_callback, 0);
  event_add (term_ev, 0);

  /*timer = evtimer_new (ev_base, timer_empty_cb, 0);
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  event_add (timer, &tv);*/
  
  while (1) {
    event_base_loop (ev_base, EVLOOP_ONCE);

    if (term_ev && delete_stdin_event) {
      logprintf ("delete stdin\n");
      event_free (term_ev);
      term_ev = 0;
    }
    
    if (sigterm_cnt > 0) {
      do_halt (0);
    }
  
    //tdlib_do_run_scheduler (0.001);
    if (need_update) {
      TdCClientWork (TLS);
      
      update_prompt ();
      //rl_redisplay ();

      need_update = 0;
    } else if (need_prompt_update) {
      need_prompt_update = 0;
      
      update_prompt ();
      //rl_forced_update_display ();
    }
  }

  if (term_ev) {
    event_free (term_ev);
    term_ev = 0;
  }
  
  logprintf ("End of netloop\n");
}

int readline_active;

extern struct tgl_update_callback upd_cb;

static void read_incoming (struct bufferevent *bev, void *_arg) {
  logprintf ("Read from incoming connection\n");
  struct in_ev *ev = _arg;
  assert (ev->bev == bev);
  ev->in_buf_pos += bufferevent_read (bev, ev->in_buf + ev->in_buf_pos, 4096 - ev->in_buf_pos);


  while (1) {
    int pos = 0;
    int ok = 0;
    while (pos < ev->in_buf_pos) {
      if (ev->in_buf[pos] == '\n') {
        if (!ev->error) {
          ev->in_buf[pos] = 0;
          struct in_command *cmd = malloc (sizeof (*cmd));
          cmd->ev = ev;
          ev->refcnt ++;
          cmd->line = strdup (ev->in_buf);
          cmd->chat_mode_chat_id = 0;
          cmd->refcnt = 1;

          interpreter_ex (cmd);          

          in_command_decref (cmd);
        } else {
          ev->error = 0;
        }
        ok = 1;
        ev->in_buf_pos -= (pos + 1);
        memmove (ev->in_buf, ev->in_buf + pos + 1, ev->in_buf_pos);
        pos = 0;
      } else {
        pos ++;
      }
    }
    if (ok) {
      ev->in_buf_pos += bufferevent_read (bev, ev->in_buf + ev->in_buf_pos, 4096 - ev->in_buf_pos);
    } else {
      if (ev->in_buf_pos == 4096) {
        ev->error = 1;
      }
      break;
    }
  }
}

void event_incoming (struct bufferevent *bev, short what, void *_arg) {
  struct in_ev *ev = _arg;
  if (what & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
    logprintf ("Closing incoming connection\n");
    assert (ev->fd >= 0);
    close (ev->fd);
    bufferevent_free (bev);
    ev->bev = 0;
    if (!--ev->refcnt) { free (ev); }
  }
}

static void accept_incoming (evutil_socket_t efd, short what, void *arg) {
  logprintf ("Accepting incoming connection\n");
  socklen_t clilen = 0;
  struct sockaddr_in cli_addr;
  int fd = accept (efd, (struct sockaddr *)&cli_addr, &clilen);

  assert (fd >= 0);
  struct bufferevent *bev = bufferevent_socket_new (ev_base, fd, 0);
  struct in_ev *e = malloc (sizeof (*e));
  e->bev = bev;
  e->refcnt = 1;
  e->in_buf_pos = 0;
  e->error = 0;
  e->fd = fd;
  bufferevent_setcb (bev, read_incoming, 0, event_incoming, e);
  bufferevent_enable (bev, EV_READ | EV_WRITE);
}

void on_started (void *TLS) {
  if (start_command) {
    safe_quit = 1;
    while (*start_command) {
      char *start = start_command;
      while (*start_command && *start_command != '\n') {
        start_command ++;
      }
      if (*start_command) {
        *start_command = 0;
        start_command ++;
      } 

      struct in_command *cmd = malloc (sizeof (*cmd));
      cmd->refcnt = 1;
      cmd->line = strdup (start);
      cmd->chat_mode_chat_id = 0;
      cmd->ev = NULL;
      interpreter_ex (cmd);
      in_command_decref (cmd);
    }
  }
}

char *hint;
int auth_state;

void on_got_auth_state (void *TLS, void *extra, struct TdNullaryObject *res);

void on_got_password (void *TLS, const char *password, void *extra) {
  TdCClientSendCommand (TLS, (void *)TdCreateObjectCheckAuthPassword ((char *)password), on_got_auth_state, extra);
}

void on_got_code (void *TLS, const char *code, void *extra) {
  void **arr = extra;
  
  TdCClientSendCommand (TLS, (void *)TdCreateObjectCheckAuthCode ((char *)code, arr[1], arr[2]), on_got_auth_state, arr[0]);

  if (arr[1]) {
    free (arr[1]);
  }
  if (arr[2]) {
    free (arr[2]);
  }
  free (arr);
}

void on_got_last_name (void *TLS, const char *last_name, void *extra) {
  void **arr = extra;
  
  arr[2] = strdup (last_name);
    
  one_string_cb_arg = arr;
  one_string_cb = on_got_code;
  do_get_string (TLS, "code: ", 0);
}

void on_got_first_name (void *TLS, const char *first_name, void *extra) {
  void **arr = extra;
  arr[1] = strdup (first_name);
    
  one_string_cb_arg = arr;
  one_string_cb = on_got_last_name;
  do_get_string (TLS, "last_name: ", 0);
}


void on_got_phone (void *TLS, const char *phone, void *extra) {
  TdCClientSendCommand (TLS, (void *)TdCreateObjectSetAuthPhoneNumber ((char *)phone, 0, 0), on_got_auth_state, extra);
}

void on_got_unknown_cb (evutil_socket_t fd, short what, void *arg) {
  TdCClientSendCommand (TLS, (void *)TdCreateObjectGetAuthState (), on_got_auth_state, arg);
}

void on_got_auth_state (void *TLS, void *extra, struct TdNullaryObject *res) {
  if (res->ID == CODE_Error) {
    struct TdError *error = (void *)res;
    logprintf ("Error %d: %s\n", error->code_, error->message_);
  } else {
    if (hint) {
      free (hint);
      hint = NULL;
    }
    auth_state = res->ID;
    if (res->ID == CODE_AuthStateWaitPassword) {
      struct TdAuthStateWaitPassword *w = (void *)res;
      hint = w->hint_ ? strdup (w->hint_) : NULL;
    }
  }


  switch (auth_state) {
  case CODE_AuthStateWaitPhoneNumber:
    if (bot_mode) {
      TdCClientSendCommand (TLS, (void *)TdCreateObjectCheckAuthBotToken (bot_hash), on_got_auth_state, extra);
    } else {
      one_string_cb_arg = extra;
      one_string_cb = on_got_phone;
      do_get_string (TLS, "phone: ", 0);
    }
    break;
  case CODE_AuthStateWaitCode:
    {
      struct TdAuthStateWaitCode *w = (void *)res;
      if (w->is_registered_) {
        void **arr = calloc (sizeof (void *), 3);
        arr[0] = extra;
        one_string_cb_arg = arr;
        one_string_cb = on_got_code;
        do_get_string (TLS, "code: ", 0);
      } else {
        void **arr = calloc (sizeof (void *), 3);
        arr[0] = extra;
        one_string_cb_arg = arr;
        one_string_cb = on_got_first_name;
        do_get_string (TLS, "first_name: ", 0);
      }
    }
    break;
  case CODE_AuthStateWaitPassword:
    one_string_cb_arg = extra;
    one_string_cb = on_got_password;
    do_get_string (TLS, "password: ", 1);
    break;
  case CODE_AuthStateOk:
  case CODE_AuthStateLoggingOut:
    break;
  default:
    {
      struct event *ev = evtimer_new (ev_base, on_got_unknown_cb, extra);
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 10000;
      event_add (ev, &tv);
    }
    break;
  }
}

void tdcli_login (void) {
  auth_state = 0;
  TdCClientSendCommand (TLS, (void *)TdCreateObjectGetAuthState (), on_got_auth_state, NULL);
}

void wakeup (void *TLS) {  
  int x = 0;
  write (write_pipe, &x, 4);
}

extern struct TdCClientParameters params;

int loop (void) {
  set_interface_callbacks ();

  ev_base = event_base_new ();

  int p[2];
  if (pipe (p) < 0) {
    perror ("pipe");
    exit (2);
  }
  read_pipe = p[0];
  write_pipe = p[1];
  
  struct event *pipe_ev = event_new (ev_base, read_pipe, EV_READ | EV_PERSIST, pipe_read_cb, 0);
  event_add (pipe_ev, 0);
  
  if (sfd >= 0) {
    struct event *ev = event_new (ev_base, sfd, EV_READ | EV_PERSIST, accept_incoming, 0);
    event_add (ev, 0);
  }
  if (usfd >= 0) {
    struct event *ev = event_new (ev_base, usfd, EV_READ | EV_PERSIST, accept_incoming, 0);
    event_add (ev, 0);
  }
  update_prompt ();

  TLS = TdCClientStart (&params);
  assert (TLS);

  tdcli_login ();
  net_loop ();
  return 0;
}

