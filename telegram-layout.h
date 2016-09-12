#pragma once

#pragma pack(push,4)
struct tdl_message_id {
  long long chat_id;
  int message_id;
};

struct tgl_peer_id {
  int peer_type;
  int peer_id;
};

#pragma pack(pop)

typedef struct tdl_message_id tdl_message_id_t;
typedef struct tgl_peer_id tgl_peer_id_t;
