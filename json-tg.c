#ifdef USE_JSON

#include <jansson.h>
#include "json-tg.h"
#include "interface.h"
#include <assert.h>
#include <string.h>
//format time:
#include <time.h>
#include <errno.h>

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

int tdcb_json_is_string (void) {
  assert (stack_pos >= 1);
  return stack[stack_pos - 1] && json_is_string (stack[stack_pos - 1]);
}

int tdcb_json_is_long (void) {
  assert (stack_pos >= 1);
  return stack[stack_pos - 1] && json_is_integer (stack[stack_pos - 1]);
}

int tdcb_json_is_double (void) {
  assert (stack_pos >= 1);
  return stack[stack_pos - 1] && json_is_number (stack[stack_pos - 1]);
}

int tdcb_json_is_array (void) {
  assert (stack_pos >= 1);
  return stack[stack_pos - 1] && json_is_array (stack[stack_pos - 1]);
}

int tdcb_json_is_table (void) {
  assert (stack_pos >= 1);
  return stack[stack_pos - 1] && json_is_object (stack[stack_pos - 1]);
}

int tdcb_json_is_nil (void) {
  assert (stack_pos >= 1);
  return !stack[stack_pos - 1] || json_is_null (stack[stack_pos - 1]);
}

char *tdcb_json_get_string (void) {
  assert (stack_pos >= 1);
  return strdup (json_string_value (stack[stack_pos - 1]));
}

long long tdcb_json_get_long (void) {
  assert (stack_pos >= 1);
  return json_integer_value (stack[stack_pos - 1]);
}

double tdcb_json_get_double (void) {
  assert (stack_pos >= 1);
  return json_number_value (stack[stack_pos - 1]);
}

void tdcb_json_pop (void) {
  assert (stack_pos >= 1);
  if (stack[stack_pos - 1]) {
    json_decref (stack[stack_pos - 1]);
  }
  stack_pos --;
}

void tdcb_json_get_field (const char *name) {
  assert (stack_pos >= 1);
  assert (stack_pos < 100);
  json_t *a = json_object_get (stack[stack_pos - 1], name);
  stack[stack_pos ++] = a ? json_incref (a) : NULL;
}

void tdcb_json_get_arr_field (int idx) {
  assert (stack_pos >= 1);
  assert (stack_pos < 100);
  json_t *a = json_array_get (stack[stack_pos - 1], idx);
  stack[stack_pos ++] = a ? json_incref (a) : NULL;
}

int tdcb_json_get_arr_size (void) {
  assert (stack_pos >= 1);
  return (int)json_array_size (stack[stack_pos - 1]);
}

struct TdStackStorerMethods tdcb_json_storer_methods = {
  .pack_string = tdcb_json_pack_string,
  .pack_long = tdcb_json_pack_long,
  .pack_double = tdcb_json_pack_double,
  .pack_bool = tdcb_json_pack_bool,
  .new_table = tdcb_json_new_table,
  .new_array = tdcb_json_new_array,
  .new_field = tdcb_json_new_field,
  .new_arr_field = tdcb_json_new_arr_field,
};

struct TdStackFetcherMethods tdcb_json_fetcher_methods = {
  .is_nil = tdcb_json_is_nil,
  .get_string = tdcb_json_get_string,
  .get_long = tdcb_json_get_long,
  .get_double = tdcb_json_get_double,
  .pop = tdcb_json_pop,
  .get_field = tdcb_json_get_field,
  .get_arr_field = tdcb_json_get_arr_field,
  .get_arr_size = tdcb_json_get_arr_size
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


void json_universal_cb (struct in_command *cmd, struct TdNullaryObject *res) {
  assert (stack_pos == 0);

  TdStackStorerNullaryObject (res, &tdcb_json_storer_methods);
  
  assert (stack_pos == 1);
  
  mprint_start (cmd->ev);
  char *s = json_dumps (stack[0], 0);
  mprintf (cmd->ev, "%s\n", s);
  json_decref (stack[0]);
  stack_pos --;
  free (s);
  mprint_end (cmd->ev);
}

void json_update_cb (void *extra, struct TdUpdate *res) {
  assert (stack_pos == 0);
  
  TdStackStorerUpdate (res, &tdcb_json_storer_methods);
  
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

void json_interpreter_ex (struct in_command *cmd) {  
  struct arg args[12];
  memset (&args, 0, sizeof (args));

  cmd->cb = json_universal_cb;

  json_error_t E;
  json_t *F = json_loads (cmd->line, 0, &E);

  if (!F) {
    fail_interface (TLS, cmd, ENOSYS, "invalid json: %s", E.text);
    return;
  }

  assert (!stack_pos);
  stack[stack_pos ++] = F;
  assert (stack_pos == 1);
  struct TdFunction *T = TdStackFetcherFunction (&tdcb_json_fetcher_methods);
  assert (stack_pos == 1);
  json_decref (stack[0]);
  stack_pos --;

  TdCClientSendCommand (TLS, T, tdcli_cb, cmd);
}
#endif
