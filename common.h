#pragma once
#include "td/db/Pmc.h"
#include "td/utils/MemoryLog.h"
#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"
#include "td/utils/Slice.h"

#include <atomic>

namespace tdl {
using td::uint8;
using td::uint16;
using td::uint32;
using td::uint64;
using td::int8;
using td::int16;
using td::int32;
using td::int64;

using td::Slice;
using td::MutableSlice;
using td::CSlice;
using td::MutableCSlice;

using td::Status;
using td::Result;

using td::string;
using td::make_unique;
using td::unique_ptr;

//extern int verbosity_webhook;
//extern td::MemoryLog<1 << 14> memory_log;
// TODO(now): make it not global
extern td::Pmc webhook_pmc_;
extern double start_timestamp_;
extern int max_webhook_connections_;
extern int local_mode_;
extern int threads_n_;
class GetHostByNameActor;
extern td::ActorId<GetHostByNameActor> get_host_by_name_actor_id_;
extern std::atomic<uint64> query_cnt_;
extern td::ListNode query_list_;
}  // namespace tg_http_client
