#pragma once

#include <string>

#include "nq.h"

namespace nq {
/*
class Closure : public nq_closure_t {
public:
	inline bool operator () (nq_conn_t s) {
		return on_conn_open(arg, s);
	}
	inline void operator () (nq_conn_t s, nq_result_t error, const std::string& detail, bool close_by_peer_or_self) {
		on_conn_close(arg, s, error, detail.c_str(), close_by_peer_or_self);
	}
	inline void *operator () (const char *p, nq_size_t plen, nq_size_t *p_rec_len) {
		return stream_reader(arg, p, plen, p_rec_len);
	}
	inline nq_size_t operator () (nq_stream_t s, const void *ptr, nq_size_t len) {
		return stream_writer(arg, ptr, len, s);
	}
	inline void operator () (nq_stream_t s, const void *ptr, nq_size_t len) {
		return on_stream_record(arg, s, ptr, len);
	}
	inline void operator () (nq_rpc_t rpc, uint16_t type, const void *ptr, nq_size_t len) {
		return on_rpc_record(arg, rpc, type, ptr, len);
	}
	inline void operator () (nq_conn_t c) {
		return create_stream(arg, c);
	}

	Closure() { nq_closure_init(*this); }
	const Closure &operator = (const nq_closure_t &cl) {
		proc = cl.proc;
		arg = cl.arg;
		return *this;
	}

	static inline const Closure &CastFrom(const nq_closure_t &cl) {
		return *(const Closure *)(&cl);
	}
};
*/
}
