#pragma once

struct tdcb_methods {
  void (*pack_string)(const char *s);
  void (*pack_long)(long long x);
  void (*pack_double)(double x);
  void (*pack_bool)(int x);
  void (*new_table)(void);
  void (*new_array)(void);
  void (*new_field)(const char *name);
  void (*new_arr_field)(int idx);
  int (*is_string)(void);
  int (*is_long)(void);
  int (*is_double)(void);
  int (*is_array)(void);
  int (*is_table)(void);
  int (*is_nil)(void);
  char *(*get_string)(void);
  long long (*get_long)(void);
  double (*get_double)(void);
  void (*pop)(void);
  void (*get_field)(const char *name);
  void (*get_arr_field)(int idx);
};

void tdcb_universal_pack_answer (struct tdcb_methods *T, struct in_command *cmd, int success, struct res_arg *args);
void tdcb_universal_pack_update (struct tdcb_methods *T, struct update_description *D, struct res_arg args[]);
void tdcb_run_command (struct tdcb_methods *T, struct in_command *cmd);
