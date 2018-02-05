#pragma once

#include <nq.h>
#include "rpctypes.h"

extern bool room_member_init(nq_rpc_t enter, void **ppctx);
extern void room_exit(nq_rpc_t exit);
extern void room_bcast(nq_rpc_t replier, const void *ptr, nq_size_t sz);
extern void room_enter(nq_rpc_t enter, nq_msgid_t msgid, const void *ptr, nq_size_t sz);
extern void room_bcast_reply(nq_rpc_t replier, nq_msgid_t msgid, const void *ptr, nq_size_t sz);
