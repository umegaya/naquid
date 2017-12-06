#include "nq.h"

#include "basis/timespec.h"

#include "core/nq_client_loop.h"
#include "core/nq_server.h"
#include "core/nq_unwrapper.h"

#include "base/at_exit.h"

//at exit manager seems optimized out and causes linkder error without following anchor
extern base::AtExitManager *nq_at_exit_manager();
static base::AtExitManager *g_at_exit_manager;

#if defined(NQAPI_THREADSAFE)
#undef NQAPI_THREADSAFE
#define NQAPI_THREADSAFE
#endif

#if defined(NQAPI_BOOTSTRAP)
#undef NQAPI_BOOTSTRAP
#define NQAPI_BOOTSTRAP
#endif

using namespace net;



// --------------------------
//
// helper
//
// --------------------------
template <class H> 
H INVALID_HANDLE(const char *msg) {
  H h;
  h.p = const_cast<char *>(msg);
  h.s = 0;
  return h;
}

static nq_closure_t g_empty_closure {
  .arg = nullptr,
  .ptr = nullptr,
};
NQAPI_THREADSAFE nq_closure_t nq_closure_empty() {
  return g_empty_closure;
}
NQAPI_THREADSAFE bool nq_closure_is_empty(nq_closure_t clsr) {
  return clsr.ptr == nullptr;
}
static inline NqStream *ToStream(nq_rpc_t rpc) { 
  return reinterpret_cast<NqStream *>(rpc.p); 
}
static inline NqStream *ToStream(nq_stream_t s) { 
  return reinterpret_cast<NqStream *>(s.p); 
}
static inline NqSession::Delegate *ToConn(nq_conn_t c) { 
  return reinterpret_cast<NqSession::Delegate *>(c.p); 
}
static inline NqAlarm *ToAlarm(nq_alarm_t a) { 
  return reinterpret_cast<NqAlarm *>(a.p); 
}



// --------------------------
//
// client API
//
// --------------------------
NQAPI_THREADSAFE nq_client_t nq_client_create(int max_nfd, int max_stream_hint) {
  g_at_exit_manager = nq_at_exit_manager(); //anchor
	auto l = new NqClientLoop(max_nfd, max_stream_hint);
	if (l->Open(max_nfd) < 0) {
		return nullptr;
	}
	return l->ToHandle();
}
NQAPI_BOOTSTRAP void nq_client_destroy(nq_client_t cl) {
	delete NqClientLoop::FromHandle(cl);
}
NQAPI_BOOTSTRAP void nq_client_poll(nq_client_t cl) {
	NqClientLoop::FromHandle(cl)->Poll();
}
NQAPI_BOOTSTRAP nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf) {
  auto loop = NqClientLoop::FromHandle(cl);
  auto cf = NqClientConfig(*conf);
	auto c = loop->Create(addr->host, addr->port, cf);
  if (c == nullptr) {
    return INVALID_HANDLE<nq_conn_t>("fail to create client");
  }
  return c->ToHandle();
}
NQAPI_BOOTSTRAP nq_hdmap_t nq_client_hdmap(nq_client_t cl) {
	return NqClientLoop::FromHandle(cl)->mutable_handler_map()->ToHandle();
}
NQAPI_BOOTSTRAP void nq_client_set_thread(nq_client_t cl) {
  NqClientLoop::FromHandle(cl)->set_main_thread();
}




// --------------------------
//
// server API
//
// --------------------------
NQAPI_THREADSAFE nq_server_t nq_server_create(int n_worker) {
	g_at_exit_manager = nq_at_exit_manager(); //anchor
  auto sv = new NqServer(n_worker);
	return sv->ToHandle();
}
nq_hdmap_t nq_server_listen(nq_server_t sv, const nq_addr_t *addr, const nq_svconf_t *conf) {
	auto psv = NqServer::FromHandle(sv);
	return psv->Open(addr, conf)->ToHandle();
}
NQAPI_BOOTSTRAP void nq_server_start(nq_server_t sv, bool block) {
	auto psv = NqServer::FromHandle(sv);
	psv->Start(block);
}
NQAPI_BOOTSTRAP void nq_server_join(nq_server_t sv) {
	auto psv = NqServer::FromHandle(sv);
	psv->Join();
	delete psv;
}



// --------------------------
//
// hdmap API
//
// --------------------------
NQAPI_BOOTSTRAP bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler) {
	return nq::HandlerMap::FromHandle(h)->AddEntry(name, handler);
}
NQAPI_BOOTSTRAP bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler) {
	return nq::HandlerMap::FromHandle(h)->AddEntry(name, handler);
}
NQAPI_BOOTSTRAP bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory) {
	return nq::HandlerMap::FromHandle(h)->AddEntry(name, factory);
}



// --------------------------
//
// conn API
//
// --------------------------
NQAPI_THREADSAFE void nq_conn_close(nq_conn_t conn) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Disconnect, ToConn(conn));
}
NQAPI_THREADSAFE void nq_conn_reset(nq_conn_t conn) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Reconnect, ToConn(conn));
} 
NQAPI_THREADSAFE void nq_conn_flush(nq_conn_t conn) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Flush, ToConn(conn));
} 
NQAPI_THREADSAFE bool nq_conn_is_client(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->IsClient();
  });
  return false;
}
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn) {
  if (conn.s == 0) { TRACE("invalid handle: %s", conn.p); return false; }
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return true;
  });
  return false;
}
NQAPI_THREADSAFE nq_hdmap_t nq_conn_hdmap(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->ResetHandlerMap()->ToHandle();
  });
  return nullptr;
}
NQAPI_THREADSAFE nq_time_t nq_conn_reconnect_wait(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return nq_time_usec(d->ReconnectDurationUS());
  });
  return 0;
}
NQAPI_THREADSAFE void *nq_conn_ctx(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->Context();
  });
  return nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_cid_t nq_conn_cid(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->Connection()->connection_id();
  });
  return 0;
}


// --------------------------
//
// stream API
//
// --------------------------
NQAPI_THREADSAFE nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->NewStreamCast<NqStream>(name)->ToHandle<nq_stream_t>();
  });
  return INVALID_HANDLE<nq_stream_t>("invalid conn"); 
}
NQAPI_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s) {
  NqSession::Delegate *d;
  UNWRAP_CONN(s, d, ({
    return {.p = d, .s = NqStreamSerialCodec::ToConnSerial(s.s)};
  }));
  return INVALID_HANDLE<nq_conn_t>("fail to find conn");
}
NQAPI_THREADSAFE nq_alarm_t nq_stream_alarm(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->NewAlarm();
  });
  return INVALID_HANDLE<nq_alarm_t>("invalid stream");
}
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return true;
  });
  return false;
}
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s) {
  ASSERT(nq_stream_is_valid(s));
	NqUnwrapper::UnwrapBoxer(s)->InvokeStream(s.s, NqBoxer::OpCode::Disconnect, ToStream(s));
}
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen) {
  ASSERT(nq_conn_is_valid(nq_stream_conn(s)));
  NqUnwrapper::UnwrapBoxer(s)->InvokeStream(s.s, NqBoxer::OpCode::Send, data, datalen, ToStream(s));
}
NQAPI_THREADSAFE void *nq_stream_ctx(nq_stream_t s) {
  NqSession::Delegate *d;
  UNWRAP_CONN(s, d, {
    return d->StreamContext(s.s);
  });
  return nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->id();
  });
  return 0;
}
NQAPI_THREADSAFE const char *nq_stream_name(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->protocol().c_str();
  });
  return nullptr;
}



// --------------------------
//
// rpc API
//
// --------------------------
NQAPI_THREADSAFE nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->NewStreamCast<NqStream>(name)->ToHandle<nq_rpc_t>();
  });
  return INVALID_HANDLE<nq_rpc_t>("invalid conn"); 
}
NQAPI_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc) {
  NqSession::Delegate *d;
  UNWRAP_CONN(rpc, d, ({
    return {.p = d, .s = NqStreamSerialCodec::ToConnSerial(rpc.s)};
  }));
  return INVALID_HANDLE<nq_conn_t>("fail to find conn");
}
NQAPI_THREADSAFE nq_alarm_t nq_rpc_alarm(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->NewAlarm();
  });
  return INVALID_HANDLE<nq_alarm_t>("invalid stream");
}
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return true;
  });
  return false;
}
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc) {
  ASSERT(nq_rpc_is_valid(rpc));
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Disconnect, ToStream(rpc));
}
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(type > 0);
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Call, type, data, datalen, on_reply, ToStream(rpc));
}
NQAPI_THREADSAFE void nq_rpc_call_ex(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_rpc_opt_t *opts) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(type > 0);
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::CallEx, type, data, datalen, *opts, ToStream(rpc));
}
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(type > 0);
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Notify, type, data, datalen, ToStream(rpc));
}
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(result <= 0);
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Reply, result, msgid, data, datalen, ToStream(rpc));
}
NQAPI_THREADSAFE void *nq_rpc_ctx(nq_rpc_t rpc) {
  NqSession::Delegate *d;
  UNWRAP_CONN(rpc, d, {
    return d->StreamContext(rpc.s);
  });
  return nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->id();
  });
  return 0;
}
NQAPI_THREADSAFE const char *nq_rpc_name(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->protocol().c_str();
  });
  return nullptr;
}



// --------------------------
//
// time API
//
// --------------------------
NQAPI_THREADSAFE nq_time_t nq_time_now() {
	return nq::clock::now();
}
NQAPI_THREADSAFE nq_unix_time_t nq_time_unix() {
	long s, us;
	nq::clock::now(s, us);
	return s;
}
NQAPI_THREADSAFE nq_time_t nq_time_sleep(nq_time_t d) {
	return nq::clock::sleep(d);
}
NQAPI_THREADSAFE nq_time_t nq_time_pause(nq_time_t d) {
	return nq::clock::pause(d);
}
NQAPI_THREADSAFE void nq_alarm_set(nq_alarm_t a, nq_time_t invocation_ts, nq_closure_t cb) {
  NqUnwrapper::UnwrapBoxer(a)->InvokeAlarm(a.s, NqBoxer::OpCode::Start, invocation_ts, cb, ToAlarm(a));
}
NQAPI_THREADSAFE void nq_alarm_destroy(nq_alarm_t a) {
  NqUnwrapper::UnwrapBoxer(a)->InvokeAlarm(a.s, NqBoxer::OpCode::Finalize, ToAlarm(a));
}
