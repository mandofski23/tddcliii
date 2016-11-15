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

#ifdef USE_LUA
#include "lua-tg.h"

#include <string.h>
#include <stdlib.h>


#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef EVENT_V2
#include <event2/event.h>
#else
#include <event.h>
#include "event-old.h"
#endif
lua_State *luaState;
extern void *TLS;
extern int allocated_commands;

//#include "interface.h"
//#include"auto/constants.h"

#include <assert.h>

struct lua_query_extra {
  int func;
  int param;
};
extern int verbosity;

static int have_file;

void print_start (void);
void print_end (void);

int ps_lua_pcall (lua_State *l, int a, int b, int c) {
  print_start ();
  int r = lua_pcall (l, a, b, c);
  print_end ();
  return r;
}

#define my_lua_checkstack(x) lua_checkstack (luaState, x)

void tdcb_lua_pack_string (const char *s) {
  my_lua_checkstack (1);
  if (s) {
    lua_pushstring (luaState, s);
  } else {
    lua_pushboolean (luaState, 0);
  }
}

void tdcb_lua_pack_bytes (const unsigned char *s, int len) {
  my_lua_checkstack (1);
  if (s) {
    lua_pushlstring (luaState, (char *)s, len);
  } else {
    lua_pushboolean (luaState, 0);
  }
}

void tdcb_lua_pack_long (long long x) {
  my_lua_checkstack (1);
  if (x == (int)x) {
    lua_pushinteger (luaState, x);
  } else {
    char s[22];
    sprintf (s, "%lld", x);
    lua_pushstring (luaState, s);
  }
}

void tdcb_lua_pack_double (double x) {
  my_lua_checkstack (1);
  lua_pushnumber (luaState, x);
}

void tdcb_lua_pack_bool (int x) {
  my_lua_checkstack (1);
  lua_pushboolean (luaState, x);
}

void tdcb_lua_new_table (void) {
  my_lua_checkstack (1);
  lua_newtable (luaState);
}

void tdcb_lua_new_array (void) {
  my_lua_checkstack (1);
  lua_newtable (luaState);
}

void tdcb_lua_new_field (const char *name) {
  my_lua_checkstack (1);
  lua_pushstring (luaState, name);
  lua_insert (luaState, -2);
  lua_settable (luaState, -3);
}

void tdcb_lua_new_arr_field (int id) {
  my_lua_checkstack (1);
  lua_pushinteger (luaState, id);
  lua_insert (luaState, -2);
  lua_settable (luaState, -3);
}

struct TdStackStorerMethods tdcb_lua_storer_methods = {
  .pack_string = tdcb_lua_pack_string,
  .pack_bytes = tdcb_lua_pack_bytes,
  .pack_long = tdcb_lua_pack_long,
  .pack_double = tdcb_lua_pack_double,
  .pack_bool = tdcb_lua_pack_bool,
  .new_table = tdcb_lua_new_table,
  .new_array = tdcb_lua_new_array,
  .new_field = tdcb_lua_new_field,
  .new_arr_field = tdcb_lua_new_arr_field,
};

int tdcb_lua_is_string (void) {
  return lua_isstring (luaState, -1);
}

int tdcb_lua_is_long (void) {
  return lua_isstring (luaState, -1) || lua_isnumber (luaState, -1);
}

int tdcb_lua_is_double (void) {
  return lua_isnumber (luaState, -1);
}

int tdcb_lua_is_array (void) {
  return lua_istable (luaState, -1);
}

int tdcb_lua_is_table (void) {
  return lua_istable (luaState, -1);
}

int tdcb_lua_is_nil (void) {
  return lua_isnil (luaState, -1);
}

char *tdcb_lua_get_string (void) {
  const char *r = lua_tostring (luaState, -1);
  return r ? strdup (r) : NULL;
}

unsigned char *tdcb_lua_get_bytes (int *len) {
  size_t l;
  const char *r = lua_tolstring (luaState, -1, &l);
  *len = (int)l;
  return r ? (unsigned char *)strdup (r) : NULL;
}

long long tdcb_lua_get_long (void) {
  if (lua_isnumber (luaState, -1)) {
    return lua_tointeger (luaState, -1);
  } else {
    return atoll (lua_tostring (luaState, -1));
  }
}

double tdcb_lua_get_double (void) {
  return lua_tonumber (luaState, -1);
}

void tdcb_lua_pop (void) {
  lua_pop (luaState, 1);
}

void tdcb_lua_get_field (const char *name) {
  lua_pushstring (luaState, name);
  lua_gettable (luaState, -2);
}

void tdcb_lua_get_arr_field (int idx) {
  lua_pushinteger (luaState, idx);
  lua_gettable (luaState, -2);
}

int tdcb_lua_get_arr_size (void) {
  int pos = 0;
  while (1) {
    lua_pushinteger (luaState, pos);
    lua_gettable (luaState, -2);
    int b = lua_isnil (luaState, -1);
    lua_pop (luaState, 1);
    if (b) { break; }
    pos ++;
  }
  return pos;
}

struct TdStackFetcherMethods tdcb_lua_fetcher_methods = {
  .get_string = tdcb_lua_get_string,
  .get_bytes = tdcb_lua_get_bytes,
  .get_long = tdcb_lua_get_long,
  .get_double = tdcb_lua_get_double,
  .pop = tdcb_lua_pop,
  .get_field = tdcb_lua_get_field,
  .get_arr_field = tdcb_lua_get_arr_field,
  .is_nil = tdcb_lua_is_nil,
  .get_arr_size = tdcb_lua_get_arr_size
};

void lua_universal_cb (struct in_command *cmd, struct TdNullaryObject *res) {
  struct lua_query_extra *cb = cmd->extra;
  lua_settop (luaState, 0);

  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->func);
  lua_rawgeti (luaState, LUA_REGISTRYINDEX, cb->param);
  
  TdStackStorerNullaryObject (res, &tdcb_lua_storer_methods);

  int stack_size = (lua_gettop (luaState));
  assert (stack_size == 3);

  int r = ps_lua_pcall (luaState, 2, 0, 0);

  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->func);
  luaL_unref (luaState, LUA_REGISTRYINDEX, cb->param);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }

  free (cb);
}

void lua_update_cb (void *extra, struct TdUpdate *res) {
  lua_settop (luaState, 0);
  lua_getglobal (luaState, "tdcli_update_callback");
  
  TdStackStorerUpdate (res, &tdcb_lua_storer_methods);
  
  int r = ps_lua_pcall (luaState, 1, 0, 0);

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
  }
}

int lua_parse_function (lua_State *L) {
  if (lua_gettop (L) != 3) {
    lua_pushboolean (L, 0);
    return 1;
  }
  
  int a1 = luaL_ref (L, LUA_REGISTRYINDEX);
  int a2 = luaL_ref (L, LUA_REGISTRYINDEX);
  
  struct lua_query_extra *e = malloc (sizeof (*e));
  assert (e);
  e->func = a2;
  e->param = a1;

  allocated_commands ++;
  struct in_command *cmd = calloc (sizeof (*cmd), 1);
  cmd->cb = lua_universal_cb;
  cmd->extra = e;

  int res = lua_gettop (L);
  assert (res == 1);
  struct TdFunction *T = TdStackFetcherFunction (&tdcb_lua_fetcher_methods);
  res = lua_gettop (L);
  assert (res == 1);
  lua_pop (luaState, 1);

  TdCClientSendCommand (TLS, T, tdcli_cb, cmd);

  lua_pushboolean (L, 1);
  return 1;
}

void lua_init (const char *file) {
  if (!file) { return; }
  have_file = 1;
  luaState = luaL_newstate ();
  luaL_openlibs (luaState);
  
  lua_register (luaState, "tdcli_function", lua_parse_function);

  print_start ();
  int r = luaL_dofile (luaState, file);
  print_end ();

  if (r) {
    logprintf ("lua: %s\n",  lua_tostring (luaState, -1));
    exit (1);
  }

  subscribe_updates (NULL, lua_update_cb);
}

#endif
