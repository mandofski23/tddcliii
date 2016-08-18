#ifndef __JSON_TG_H__
#define __JSON_TG_H__
#ifdef USE_JSON
#include <jansson.h>

#include "interface.h"

void json_universal_cb (struct in_command *cmd, int success, struct res_arg *args);
void json_interpreter_ex (struct in_command *cmd);
void json_update_cb (void *extra, struct update_description *D, struct res_arg args[]);

#endif
#endif
