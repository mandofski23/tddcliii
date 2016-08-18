#ifdef USE_JSON

#include <jansson.h>
#include "tdc/tdlib-c-bindings.h"
#include "json-tg.h"
#include "interface.h"
#include <assert.h>
#include <string.h>
//format time:
#include <time.h>
#include <errno.h>

#include "extension.h"

extern struct tdlib_state *TLS;

#ifndef json_boolean
#define json_boolean(val)      ((val) ? json_true() : json_false())
#endif

#define STACK_SIZE 100

static json_t *stack[STACK_SIZE];
static int stack_pos;

void tdcb_json_pack_string (const char *s) {
  assert (stack_pos < STACK_SIZE);
  stack[stack_pos ++] = json_string (s);
}

void tdcb_json_pack_long (long long x) {
  assert (stack_pos < STACK_SIZE);
  stack[stack_pos ++] = json_integer (x);
}

void tdcb_json_pack_double (double x) {
  assert (stack_pos < STACK_SIZE);
  stack[stack_pos ++] = json_real (x);
}

void tdcb_json_pack_bool (int x) {
  assert (stack_pos < STACK_SIZE);
  stack[stack_pos ++] = json_boolean (x);
}

void tdcb_json_new_table (void) {
  assert (stack_pos < STACK_SIZE);
  stack[stack_pos ++] = json_object ();
}

void tdcb_json_new_array (void) {
  assert (stack_pos < STACK_SIZE);
  stack[stack_pos ++] = json_array ();
}

void tdcb_json_new_field (const char *name) {
  assert (stack_pos >= 2);
  json_t *a = stack[--stack_pos];
  assert (json_object_set (stack[stack_pos - 1], name, a) >= 0);
}

void tdcb_json_new_arr_field (int id) {
  assert (stack_pos >= 2);
  json_t *a = stack[--stack_pos];
  assert (json_array_append (stack[stack_pos - 1], a) >= 0);
}

struct tdcb_methods tdcb_json_methods = {
  .pack_string = tdcb_json_pack_string,
  .pack_long = tdcb_json_pack_long,
  .pack_double = tdcb_json_pack_double,
  .pack_bool = tdcb_json_pack_bool,
  .new_table = tdcb_json_new_table,
  .new_array = tdcb_json_new_array,
  .new_field = tdcb_json_new_field,
  .new_arr_field = tdcb_json_new_arr_field
};


void socket_answer_start (void);
void socket_answer_add_printf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void socket_answer_end (struct in_ev *ev);

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


void json_universal_cb (struct in_command *cmd, int success, struct res_arg *args) {
  assert (stack_pos == 0);
  
  tdcb_universal_pack_answer (&tdcb_json_methods, cmd, success, args);
  
  assert (stack_pos == 1);
  
  mprint_start (cmd->ev);
  char *s = json_dumps (stack[0], 0);
  mprintf (cmd->ev, "%s\n", s);
  json_decref (stack[0]);
  stack_pos --;
  free (s);
  mprint_end (cmd->ev);
}

int json_parse_argument_modifier (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_string (F)) {
    return -1;
  }
  
  const char *s = json_string_value (F);

  if (!s || strlen (s) < 2 || s[0] != '[' || s[strlen (s) - 1] != ']') {
    A->str = NULL;
    return -1;
  } else {
    A->str = strdup (s);
    A->flags = 1;
    return 0;
  }
}

int json_parse_argument_string (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_string (F)) {
    return -1;
  }
  
  A->str = strdup (json_string_value (F));
  A->flags = 1;
  return 0;
}

int json_parse_argument_number (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_integer (F)) {
    A->num = NOT_FOUND;
    return -1;
  }
  
  A->num = json_integer_value (F);
  return 0;
}

int json_parse_argument_double (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_number (F)) {
    A->dval = NOT_FOUND;
    return -1;
  }
  
  A->dval = json_number_value (F);
  return 0;
}

int json_parse_argument_msg_id (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_string (F)) {
    A->msg_id.message_id = -1;
    return -1;
  }

  const char *s = json_string_value (F);
  long long chat_id = 0;
  int message_id = 0;

  sscanf (s, "chat#id%lld@%d", &chat_id, &message_id);
  A->msg_id.chat_id = chat_id;
  A->msg_id.message_id = message_id;

  return 0;
}

int json_parse_argument_chat (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_string (F)) {
    return -1;
  }

  const char *ss = json_string_value (F);

  int op = D->type & 255;
    
  int m = -1;
  if (op == ca_user) { m = tdl_chat_type_user; }
  if (op == ca_group) { m = tdl_chat_type_group; }
  if (op == ca_channel) { m = tdl_chat_type_channel; }
  if (op == ca_secret_chat) { m = tdl_chat_type_secret_chat; }            

  char *s = strdup (ss);
  if (*s == '@') {
    int i;
    int len = (int)strlen (s);
    for (i = 0; i < len; i++) {
      if (s[i] >= 'A' && s[i] <= 'Z') {
        s[i] = (char)(s[i] + 'a' - 'A');
      }
    }
  } 

  struct chat_alias *AL = get_by_alias (s);
  free (s);

  if (!AL || AL->type != -1) {
    return -1;
  }
    
  struct tdl_chat_info *C = AL->chat;
  if (m >= 0 && C->chat->type != m) {
    return -1;
  } else {
    A->chat = C;
    return 0;
  }
}

int json_parse_argument_any (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int op = D->type & 255;

  switch (op) {
  case ca_user:
  case ca_group:
  case ca_secret_chat:
  case ca_channel:
  case ca_chat:
    return json_parse_argument_chat (F, cmd, A, D);
  case ca_file_name:
  case ca_string:
  case ca_media_type:
  case ca_command:
  case ca_file_name_end:
  case ca_string_end:
  case ca_msg_string_end:
    return json_parse_argument_string (F, cmd, A, D);
  case ca_modifier:
    return json_parse_argument_modifier (F, cmd, A, D);
  case ca_number:
    return json_parse_argument_number (F, cmd, A, D);
  case ca_double:
    return json_parse_argument_double (F, cmd, A, D);
  case ca_msg_id:
    return json_parse_argument_msg_id (F, cmd, A, D);
  case ca_none:
  default:
    logprintf ("type=%d\n", op);
    assert (0);
    return -1;
  }
}

int json_parse_argument_period (json_t *F, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!F || !json_is_array (F)) {
    return -1;
  }
  A->flags = 2;

  A->vec_len = 0;
  A->vec = malloc (0);

  int i;
  json_t *FF;
  struct arg T;
  json_array_foreach (F, i, FF) {
    memset (&T, 0, sizeof (T));
    int r = json_parse_argument_any (FF, cmd, A, D);
    if (r == -1) {
      return -1;
    }
    A->vec = realloc (A->vec, sizeof (struct arg) * (A->vec_len + 1));
    A->vec[A->vec_len ++] = T;
  }

  return 0;
}

void free_argument (struct arg *A);
void free_args_list (struct arg args[], int cnt);

extern struct command_argument_desc carg_0;
extern struct command_argument_desc carg_1;
extern struct command commands[];

int json_parse_command_line (json_t *F, struct arg args[], struct in_command *cmd) {
  //struct command_argument_desc *D = command->args;
  //void (*fun)(struct command *, int, struct arg[], struct in_command *) = command->fun;
  struct command *command = NULL;

  int p = 0;  
  int ok = 0;

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

    json_t *FF = json_object_get (F, D->name);

    if (!FF) {
      if (!(D->type & ca_optional)) {
        fail_interface (TLS, cmd, ENOSYS, "can not parse arg '%s'", D->name);
        ok = -1;
        break;
      } else {
        if (D->type & ca_period) {
          json_parse_argument_period (FF, cmd, &args[p], D);
        } else {
          json_parse_argument_any (FF, cmd, &args[p], D);
        }
      }
    } else {
      int r;
      if (D->type & ca_period) {
        r = json_parse_argument_period (FF, cmd, &args[p], D);
      } else {
        r = json_parse_argument_any (FF, cmd, &args[p], D);
      }
      if (r < 0) {
        fail_interface (TLS, cmd, ENOSYS, "can not parse arg '%s'", D->name);
        ok = r;
        break;
      }
    }

    if (p == 1) {
      command = commands;      
      while (command->name) {
        if (!strcmp (command->name, args[p].str)) {
          break;
        }
        command ++;
      }
      if (!command->name) {
        fail_interface (TLS, cmd, ENOSYS, "unknown command %s", args[p].str);
        ok = -1;
        break;
      }
    }

    p ++;
  }

  return ok;
}

void json_interpreter_ex (struct in_command *cmd) {  
  struct arg args[12];
  memset (&args, 0, sizeof (args));

  json_error_t E;
  json_t *F = json_loads (cmd->line, 0, &E);

  if (!F) {
    fail_interface (TLS, cmd, ENOSYS, "invalid json: %s", E.text);
    return;
  }
  int res = json_parse_command_line (F, args, cmd);

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

    cmd->cb = json_universal_cb;    
    command->fun (command, 12, args, cmd);
  }
  
  free_args_list (args, 12);
}

void json_update_cb (void *extra, struct update_description *D, struct res_arg args[]) {
  assert (stack_pos == 0);
  
  tdcb_universal_pack_update (&tdcb_json_methods, D, args);
  
  assert (stack_pos == 1);
  
  struct in_ev *ev = extra;
  mprint_start (ev);
  char *s = json_dumps (stack[0], 0);
  mprintf (ev, "%s\n", s);
  json_decref (stack[0]);
  stack_pos --;
  free (s);
  mprint_end (ev);
}
#endif
