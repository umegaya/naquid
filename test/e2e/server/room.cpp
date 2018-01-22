#include "room.h"
#include "rpctypes.h"

#include <basis/endian.h>

#include <map>
#include <mutex>

using namespace nqtest;

class Member {
public:
	uint64_t acked_max_seq_id;
	nq_rpc_t conn;
	Member() : acked_max_seq_id(0) {}	
};
class Context {
public:
	uint64_t id;
	Member *m;
	Context() : id(0), m(nullptr) {}
};

static std::mutex g_room_lock;
static std::map<uint64_t, Member*> g_room;


bool room_member_init(nq_rpc_t enter, void **ppctx) {
	*ppctx = new Context();
	return true;
}
void room_enter(nq_rpc_t enter, nq_msgid_t msgid, const void *ptr, nq_size_t sz) {
	Context *c = (Context *)nq_rpc_ctx(enter);
	uint64_t id = nq::Endian::NetbytesToHost<uint64_t>(ptr);
	c->id = id;
	{
		std::unique_lock<std::mutex> lk(g_room_lock);
		auto it = g_room.find(id);
		if (it != g_room.end()) {
			c->m = it->second;
		} else {
			c->m = new Member();
			g_room[id] = c->m;
		}
		c->m->conn = enter;
	
		char buffer[sizeof(uint64_t)];
		nq::Endian::HostToNetbytes(c->m->acked_max_seq_id, buffer);
		nq_rpc_reply(enter, msgid, buffer, sizeof(buffer));
	}
}
void room_exit(nq_rpc_t exit) {
	Context *c = (Context *)nq_rpc_ctx(exit);
	delete c;
}
void room_bcast(nq_rpc_t replier, nq_msgid_t msgid, const void *ptr, nq_size_t sz) {
	std::unique_lock<std::mutex> lk(g_room_lock);
	char buffer[sizeof(uint64_t) + sizeof(uint64_t)];
	for (auto &kv : g_room) {
		nq::Endian::HostToNetbytes(kv.first, buffer);
		nq::Endian::HostToNetbytes(kv.second->acked_max_seq_id + 1, buffer + sizeof(uint64_t));
		nq_rpc_notify(kv.second->conn, RpcType::BcastNotify, buffer, sizeof(buffer));
	}
	nq_rpc_reply(replier, msgid, "", 0);
}
void room_bcast_reply(nq_rpc_t replier, nq_msgid_t msgid, const void *ptr, nq_size_t sz) {
	const char *tmp_p = static_cast<const char *>(ptr);
	uint64_t id = nq::Endian::NetbytesToHost<uint64_t>(tmp_p);
	uint64_t acked_seq_id = nq::Endian::NetbytesToHost<uint64_t>(tmp_p + sizeof(uint64_t));
	bool client_close = (tmp_p[sizeof(uint64_t) + sizeof(uint64_t)] != 0);
	std::unique_lock<std::mutex> lk(g_room_lock);
	auto it = g_room.find(id);
	if (it != g_room.end()) {
		ASSERT(it->second->acked_max_seq_id < acked_seq_id);
		it->second->acked_max_seq_id = acked_seq_id;
	}
	if (!client_close) {
		nq_rpc_close(replier);
	} else {
		nq_rpc_reply(replier, msgid, "", 0);
	}
}
