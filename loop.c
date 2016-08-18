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
#include <readline/readline.h>
#include <readline/history.h>

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

#include "tdc/tdlib-c-bindings.h"

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
extern struct tdlib_state *TLS;
extern int ipv6_enabled;

struct event *term_ev = 0;
int read_one_string;
#define MAX_ONE_STRING_LEN 511
char one_string[MAX_ONE_STRING_LEN + 1];
int one_string_len;
void (*one_string_cb)(struct tdlib_state *TLS, const char *string, void *arg);

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

void do_get_string (struct tdlib_state *TLS, char *prompt, int flags) {
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
    rl_callback_read_char ();
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
      tdlib_get_updates (TLS);
    
      update_prompt ();
      //need_update = 0;
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
          cmd->chat_mode_chat = NULL;
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

void on_started (struct tdlib_state *TLS) {
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
      cmd->chat_mode_chat = NULL;
      cmd->ev = NULL;
      interpreter_ex (cmd);
      in_command_decref (cmd);
    }
  }
}

char *hint;
enum tdl_auth_state_type auth_state;

void on_got_auth_state (struct tdlib_state *TLS, void *extra, int success, union tdl_auth_state *state);

void on_got_password (struct tdlib_state *TLS, const char *password, void *extra) {
  tdlib_check_auth_password (TLS, on_got_auth_state, extra, password);
}

void on_got_code (struct tdlib_state *TLS, const char *code, void *extra) {
  void **arr = extra;
  
  tdlib_check_auth_code (TLS, on_got_auth_state, arr[0], code, arr[1], arr[2]);

  if (arr[1]) {
    free (arr[1]);
  }
  if (arr[2]) {
    free (arr[2]);
  }
  free (arr);
}

void on_got_last_name (struct tdlib_state *TLS, const char *last_name, void *extra) {
  void **arr = extra;
  
  arr[2] = strdup (last_name);
    
  one_string_cb_arg = arr;
  one_string_cb = on_got_code;
  do_get_string (TLS, "code: ", 0);
}

void on_got_first_name (struct tdlib_state *TLS, const char *first_name, void *extra) {
  void **arr = extra;
  arr[1] = strdup (first_name);
    
  one_string_cb_arg = arr;
  one_string_cb = on_got_last_name;
  do_get_string (TLS, "last_name: ", 0);
}


void on_got_phone (struct tdlib_state *TLS, const char *phone, void *extra) {
  tdlib_set_auth_phone (TLS, on_got_auth_state, extra, phone, 0, 0);
}

void on_got_unknown_cb (evutil_socket_t fd, short what, void *arg) {
  tdlib_get_auth_state (TLS, on_got_auth_state, arg);
}

void on_got_auth_state (struct tdlib_state *TLS, void *extra, int success, union tdl_auth_state *state) {
  if (!success) {
    logprintf ("Error %d: %s\n", TLS->error_code, TLS->error);
  } else {
    if (hint) {
      free (hint);
      hint = NULL;
    }
    auth_state = state->type;
    if (state->type == tdl_auth_state_wait_password) {
      hint = state->wait_password.hint ? strdup (state->wait_password.hint) : NULL;
    }
  }


  switch (auth_state) {
  case tdl_auth_state_wait_phone_number:
    if (bot_mode) {
      tdlib_check_auth_bot_token (TLS, on_got_auth_state, extra, bot_hash);
    } else {
      one_string_cb_arg = extra;
      one_string_cb = on_got_phone;
      do_get_string (TLS, "phone: ", 0);
    }
    break;
  case tdl_auth_state_wait_code:
    if (state->wait_code.is_registered) {
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
    break;
  case tdl_auth_state_wait_password:
    one_string_cb_arg = extra;
    one_string_cb = on_got_password;
    do_get_string (TLS, "password: ", 1);
    break;
  case tdl_auth_state_unknown:
    {
      struct event *ev = evtimer_new (ev_base, on_got_unknown_cb, extra);
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 10000;
      event_add (ev, &tv);
    }
    break;
  case tdl_auth_state_ok:
  case tdl_auth_state_logging_out:
    break;
  }
}

void tgl_login (void) {
  auth_state = tdl_auth_state_unknown;
  tdlib_get_auth_state (TLS, on_got_auth_state, NULL);
}

void wakeup (struct tdlib_state *TLS) {  
  int x = 0;
  write (write_pipe, &x, 4);
}

int loop (void) {
  tgl_set_callback (TLS, &upd_cb);
  struct event_base *ev = event_base_new ();
  ev_base = ev;
  tgl_register_app_id (TLS, TELEGRAM_CLI_APP_ID, TELEGRAM_CLI_APP_HASH); 
  tgl_set_app_version (TLS, "Telegram-cli " TELEGRAM_CLI_VERSION);

  int p[2];
  if (pipe (p) < 0) {
    perror ("pipe");
    exit (2);
  }
  read_pipe = p[0];
  write_pipe = p[1];
  
  struct event *pipe_ev = event_new (ev_base, read_pipe, EV_READ | EV_PERSIST, pipe_read_cb, 0);
  event_add (pipe_ev, 0);

  /*if (disable_link_preview) {
    tgl_disable_link_preview (TLS);
  }*/
  tdlib_do_start (TLS);
  
  if (sfd >= 0) {
    struct event *ev = event_new (ev_base, sfd, EV_READ | EV_PERSIST, accept_incoming, 0);
    event_add (ev, 0);
  }
  if (usfd >= 0) {
    struct event *ev = event_new (ev_base, usfd, EV_READ | EV_PERSIST, accept_incoming, 0);
    event_add (ev, 0);
  }
  update_prompt ();

  set_interface_callbacks ();

  tgl_login ();
  net_loop ();
  return 0;
}

