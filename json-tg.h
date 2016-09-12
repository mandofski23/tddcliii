#ifndef __JSON_TG_H__
#define __JSON_TG_H__
#ifdef USE_JSON
#include <jansson.h>

#include "interface.h"

void json_universal_cb (struct in_command *cmd, struct TdNullaryObject *res);
void json_interpreter_ex (struct in_command *cmd);
void json_update_cb (void *extra, struct TdUpdate *upd);

#endif
#endif
