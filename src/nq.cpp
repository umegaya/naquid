#include "nq.h"

#include "basis/timespec.h"

#include "core/nq_client_loop.h"
#include "core/nq_server.h"
#include "core/nq_unwrapper.h"
#include "core/nq_network_helper.h"

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
enum InvalidHandleReason {
  IHR_CONN_CREATE_FAIL = 1,
  IHR_INVALID_CONN = 2,
  IHR_CONN_NOT_FOUND = 3,
  IHR_INVALID_STREAM = 4,
};
template <class H> 
H INVALID_HANDLE(InvalidHandleReason ihr) {
  H h;
  h.p = reinterpret_cast<void *>(ihr);
  h.s = 0;
  return h;
}
template <class H>
const char *INVALID_REASON(const H &h) {
  switch (reinterpret_cast<uintptr_t>(h.p)) {
    case IHR_CONN_CREATE_FAIL:
      return "conn create fail";
    case IHR_INVALID_CONN:
      return "invalid conn";
    case IHR_CONN_NOT_FOUND:
      return "conn not found";
    case IHR_INVALID_STREAM:    
      return "invalid stream";
    default:
      if (h.s == 0) {
        return "deallocated handle";
      } else {
        ASSERT(false);
        return "outdated handle";
      }
  }
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
static inline NqSession::Delegate *ToConn(nq_conn_t c) { 
  return reinterpret_cast<NqSession::Delegate *>(c.p); 
}
static inline NqSession::Delegate *ToConn(nq_rpc_t c) { 
  return reinterpret_cast<NqSession::Delegate *>(c.p); 
}
static inline NqAlarm *ToAlarm(nq_alarm_t a) { 
  return reinterpret_cast<NqAlarm *>(a.p); 
}
static inline bool IsOutgoing(bool is_client, nq_sid_t stream_id) {
  return is_client ? ((stream_id % 2) != 0) : ((stream_id % 2) == 0);
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
    return INVALID_HANDLE<nq_conn_t>(IHR_CONN_CREATE_FAIL);
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
  }, "nq_conn_is_client");
  return false;
}
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return true;
  }, "nq_conn_is_valid");
  return false;
}
NQAPI_THREADSAFE nq_hdmap_t nq_conn_hdmap(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->ResetHandlerMap()->ToHandle();
  }, "nq_conn_hdmap");
  return nullptr;
}
NQAPI_THREADSAFE nq_time_t nq_conn_reconnect_wait(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return nq_time_usec(d->ReconnectDurationUS());
  }, "nq_conn_reconnect_wait");
  return 0;
}
NQAPI_THREADSAFE void *nq_conn_ctx(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->Context();
  }, "nq_conn_ctx");
  return nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_cid_t nq_conn_cid(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->Connection()->connection_id();
  }, "nq_conn_cid");
  return 0;
}
NQAPI_THREADSAFE int nq_conn_fd(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return static_cast<NqNetworkHelper *>(static_cast<NqClient *>(d)->network_helper())->fd();
  }, "nq_conn_cid");
  return -1; 
}


// --------------------------
//
// stream API
//
// --------------------------
NQAPI_THREADSAFE void nq_conn_stream(nq_conn_t conn, const char *name, void *ctx) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, NqBoxer::OpCode::CreateStream, ToConn(conn), name, ctx);
}
NQAPI_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s) {
  NqSession::Delegate *d;
  UNWRAP_CONN(s, d, ({
    return {.p = d, .s = NqStreamSerialCodec::ToConnSerial(s.s)};
  }), "nq_stream_conn");
  return INVALID_HANDLE<nq_conn_t>(IHR_CONN_NOT_FOUND);
}
NQAPI_THREADSAFE nq_alarm_t nq_stream_alarm(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->NewAlarm();
  }, "nq_stream_alarm");
  return INVALID_HANDLE<nq_alarm_t>(IHR_INVALID_STREAM);
}
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return true;
  }, "nq_stream_is_valid");
  return false;
}
NQAPI_THREADSAFE bool nq_stream_outgoing(nq_stream_t s, bool *p_valid) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    *p_valid = true;
    return IsOutgoing(NqStreamSerialCodec::IsClient(s.s), st->id());
  }, "nq_stream_close");
  *p_valid = false;
  return false;
}
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    st->Disconnect();
  }, "nq_stream_close");
}
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    st->Handler<NqStreamHandler>()->Send(data, datalen);
  }, "nq_stream_send");
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE void *nq_stream_ctx(nq_stream_t s) {
  NqSession::Delegate *d;
  UNWRAP_CONN(s, d, {
    return d->StreamContext(s.s);
  }, "nq_stream_ctx");
  return nullptr;
}
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->id();
  }, "nq_stream_sid");
  return 0;
}
NQAPI_THREADSAFE const char *nq_stream_name(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->Protocol().c_str();
  }, "nq_stream_name");
  return nullptr;
}



// --------------------------
//
// rpc API
//
// --------------------------
NQAPI_THREADSAFE void nq_conn_rpc(nq_conn_t conn, const char *name, void *ctx) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, NqBoxer::OpCode::CreateRpc, ToConn(conn), name, ctx);
}
NQAPI_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc) {
  NqSession::Delegate *d;
  UNWRAP_CONN(rpc, d, ({
    return {.p = d, .s = NqStreamSerialCodec::ToConnSerial(rpc.s)};
  }), "nq_rpc_conn");
  return INVALID_HANDLE<nq_conn_t>(IHR_CONN_NOT_FOUND);
}
NQAPI_THREADSAFE nq_alarm_t nq_rpc_alarm(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->NewAlarm();
  }, "nq_rpc_alarm");
  return INVALID_HANDLE<nq_alarm_t>(IHR_INVALID_STREAM);
}
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return true;
  }, "nq_rpc_is_valid");
  return false;
}
NQAPI_THREADSAFE bool nq_rpc_outgoing(nq_rpc_t rpc, bool *p_valid) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    *p_valid = true;
    return IsOutgoing(NqStreamSerialCodec::IsClient(rpc.s), st->id());
  }, "nq_stream_close");
  *p_valid = false;
  return false;
}
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    st->Disconnect();
  }, "nq_rpc_close");
}
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply) {
#if defined(USE_WRITE_OP)
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Call, type, data, datalen, on_reply, nullptr, ToConn(rpc));
#else
  ASSERT(type > 0);
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    st->Handler<NqSimpleRPCStreamHandler>()->Call(type, data, datalen, on_reply);
  }, "nq_rpc_call");
#endif
}
NQAPI_THREADSAFE void nq_rpc_call_ex(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_rpc_opt_t *opts) {
  ASSERT(type > 0);
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    st->Handler<NqSimpleRPCStreamHandler>()->CallEx(type, data, datalen, *opts);
  }, "nq_rpc_call_ex");
}
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen) {
  ASSERT(type > 0);
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    st->Handler<NqSimpleRPCStreamHandler>()->Notify(type, data, datalen);
  }, "nq_rpc_notify");
}
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
#if defined(USE_WRITE_OP)
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Reply, result, msgid, data, datalen, nullptr, ToConn(rpc));
#else
  ASSERT(result <= 0);
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    st->Handler<NqSimpleRPCStreamHandler>()->Reply(result, msgid, data, datalen);
  }, "nq_rpc_reply");
#endif
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE void *nq_rpc_ctx(nq_rpc_t rpc) {
  NqSession::Delegate *d;
  UNWRAP_CONN(rpc, d, {
    return d->StreamContext(rpc.s);
  }, "nq_rpc_ctx");
  return nullptr;
}
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->id();
  }, "nq_rpc_sid");
  return 0;
}
NQAPI_THREADSAFE const char *nq_rpc_name(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->Protocol().c_str();
  }, "nq_rpc_name");
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
