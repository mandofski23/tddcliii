#ifndef __JSON_TG_H__
#define __JSON_TG_H__
#ifdef USE_JSON
#include <jansson.h>

json_t *json_pack_message (struct tdl_message *M);
json_t *json_pack_chat_member (struct tdl_chat_member *M);
json_t *json_pack_chat (struct tdl_chat_info *C);
json_t *json_pack_read (struct tdl_message *M);
json_t *json_pack_user (struct tdl_user *U);
json_t *json_pack_group (struct tdl_group *G);
json_t *json_pack_channel (struct tdl_channel *Ch);
json_t *json_pack_secret_chat (struct tdl_secret_chat *SC);
#endif
#endif
