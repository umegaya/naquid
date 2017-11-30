#include "nq.h"

#include "basis/timespec.h"

#include "core/nq_client.h"
#include "core/nq_server.h"
#include "core/nq_stream.h"

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


// --------------------------
//
// client API
//
// --------------------------
NQAPI_THREADSAFE nq_client_t nq_client_create(int max_nfd) {
  g_at_exit_manager = nq_at_exit_manager(); //anchor
	auto l = new NqClientLoop();
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
  return loop->Box(c);
}
NQAPI_THREADSAFE nq_hdmap_t nq_client_hdmap(nq_client_t cl) {
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
  NqBoxer::From(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Disconnect);
}
NQAPI_THREADSAFE void nq_conn_reset(nq_conn_t conn) {
  NqBoxer::From(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Reconnect);
} 
NQAPI_THREADSAFE void nq_conn_flush(nq_conn_t conn) {
  NqBoxer::From(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Flush);
} 
NQAPI_THREADSAFE bool nq_conn_is_client(nq_conn_t conn) {
	return NqBoxer::From(conn)->IsClient();
}
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn) {
  if (conn.s == 0) { TRACE("invalid handle: %s", conn.p); return false; }
  return NqBoxer::From(conn)->Find(conn) != nullptr;
}
NQAPI_THREADSAFE nq_hdmap_t nq_conn_hdmap(nq_conn_t conn) {
  auto c = const_cast<NqSession::Delegate *>(NqBoxer::From(conn)->Find(conn));
	return c != nullptr ? c->ResetHandlerMap()->ToHandle() : nullptr;
}
NQAPI_THREADSAFE nq_time_t nq_conn_reconnect_wait(nq_conn_t conn) {
  auto c = NqBoxer::From(conn)->Find(conn);
  return c != nullptr ? nq_time_usec(c->ReconnectDurationUS()) : 0;
}
NQAPI_THREADSAFE void *nq_conn_ctx(nq_conn_t conn) {
  auto c = NqBoxer::From(conn)->Find(conn);
  return c != nullptr ? c->Context() : nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_cid_t nq_conn_cid(nq_conn_t conn) {
  auto c = NqBoxer::From(conn)->Find(conn);
  return c != nullptr ? c->Id() : 0;
}


// --------------------------
//
// stream API
//
// --------------------------
NQAPI_THREADSAFE nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name) {
NqSession::Delegate *d;
  return NqBoxer::From(conn)->Unbox(conn.s, &d) == NqBoxer::UnboxResult::Ok ? 
    d->NewStreamCast<NqStream>(name)->ToHandle<nq_stream_t>() : 
    INVALID_HANDLE<nq_stream_t>("fail to unbox"); 
}
NQAPI_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s) {
  auto b = NqBoxer::From(s);
  nq_conn_t c = {
    .p = s.p, 
    .s = b->IsClient() ? 
      NqConnSerialCodec::FromClientStreamSerial(s.s) :
      NqConnSerialCodec::FromServerStreamSerial(s.s) 
  };
  return c;
}
NQAPI_THREADSAFE nq_alarm_t nq_stream_alarm(nq_stream_t s) {
  auto pa = new NqAlarm();
  return NqBoxer::From(s)->Box(pa);
}
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s) {
  if (s.s == 0) { TRACE("invalid handle: %s", s.p); return false; }
  return NqBoxer::From(s)->Find(s) != nullptr;
}
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s) {
  ASSERT(nq_stream_is_valid(s));
	NqBoxer::From(s)->InvokeStream(s.s, NqBoxer::OpCode::Disconnect);
}
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen) {
  ASSERT(nq_conn_is_valid(nq_stream_conn(s)));
  NqBoxer::From(s)->InvokeStream(s.s, NqBoxer::OpCode::Send, data, datalen);
}
NQAPI_THREADSAFE void *nq_stream_ctx(nq_stream_t s) {
  auto d = NqBoxer::From(s)->FindConnFromStream(s);
  return d != nullptr ? d->StreamContext(s.s) : nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s) {
  ASSERT(nq_stream_is_valid(s));
  auto st = NqBoxer::From(s)->Find(s);
  return st != nullptr ? st->id() : 0;
}
NQAPI_THREADSAFE const char *nq_stream_name(nq_stream_t s) {
  ASSERT(nq_stream_is_valid(s));
  auto st = NqBoxer::From(s)->Find(s);
  return st != nullptr ? st->protocol().c_str() : "";
}



// --------------------------
//
// rpc API
//
// --------------------------
NQAPI_THREADSAFE nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name) {
NqSession::Delegate *d;
  return NqBoxer::From(conn)->Unbox(conn.s, &d) == NqBoxer::UnboxResult::Ok ? 
    d->NewStreamCast<NqStream>(name)->ToHandle<nq_rpc_t>() : 
    INVALID_HANDLE<nq_rpc_t>("fail to unbox"); 
}
NQAPI_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc) {
  auto b = NqBoxer::From(rpc);
  nq_conn_t c = {
    .p = rpc.p, 
    .s = b->IsClient() ? 
      NqConnSerialCodec::FromClientStreamSerial(rpc.s) :
      NqConnSerialCodec::FromServerStreamSerial(rpc.s) 
  };
  return c;
}
NQAPI_THREADSAFE nq_alarm_t nq_rpc_alarm(nq_rpc_t rpc) {
  auto pa = new NqAlarm();
  return NqBoxer::From(rpc)->Box(pa);
}
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc) {
  if (rpc.s == 0) { TRACE("invalid handle: %s", rpc.p); return false; }
  return NqBoxer::From(rpc)->Find(rpc) != nullptr;
}
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc) {
  ASSERT(nq_rpc_is_valid(rpc));
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Disconnect);
}
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(type > 0);
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Call, type, data, datalen, on_reply);
}
NQAPI_THREADSAFE void nq_rpc_call_ex(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_rpc_opt_t *opts) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(type > 0);
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::CallEx, type, data, datalen, *opts);
}
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(type > 0);
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Notify, type, data, datalen);
}
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
  ASSERT(nq_conn_is_valid(nq_rpc_conn(rpc)));
  ASSERT(result <= 0);
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Reply, result, msgid, data, datalen);
}
NQAPI_THREADSAFE void *nq_rpc_ctx(nq_rpc_t rpc) {
  auto d = NqBoxer::From(rpc)->FindConnFromRpc(rpc);
  return d != nullptr ? d->StreamContext(rpc.s) : nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t rpc) {
  ASSERT(nq_rpc_is_valid(rpc));
  auto st = NqBoxer::From(rpc)->Find(rpc);
  return st != nullptr ? st->id() : 0;
}
NQAPI_THREADSAFE const char *nq_rpc_name(nq_rpc_t rpc) {
  ASSERT(nq_rpc_is_valid(rpc));
  auto st = NqBoxer::From(rpc)->Find(rpc);
  return st != nullptr ? st->protocol().c_str() : "";
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
  NqBoxer::From(a)->InvokeAlarm(a.s, NqBoxer::OpCode::Start, invocation_ts, cb);
}
NQAPI_THREADSAFE void nq_alarm_destroy(nq_alarm_t a) {
  NqBoxer::From(a)->InvokeAlarm(a.s, NqBoxer::OpCode::Finalize);
}
