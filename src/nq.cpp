#include "nq.h"

#include <ares.h>

#include "basis/logger.h"

#include "basis/defs.h"
#include "basis/timespec.h"

#include "core/nq_at_exit.h"
#include "core/nq_closure.h"
#include "core/nq_client_loop.h"
#include "core/nq_server.h"
#include "core/nq_unwrapper.h"
#include "core/compat/chromium/nq_network_helper.h"

//at exit manager seems optimized out and causes linkder error without following anchor
extern nq_at_exit_manager_t nq_at_exit_manager();
static nq_at_exit_manager_t g_at_exit_manager;

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
// chaos modes
//
// --------------------------
#if defined(DEBUG)
static bool g_chaos_write = false;
extern bool chaos_write() {
  return g_chaos_write;
}
void chaos_init() {
  g_chaos_write = getenv("CHAOS") != nullptr;
}
#else
#define chaos_init()
#endif



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
  NqSerial::Clear(h.s);
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
      if (NqSerial::IsEmpty(h.s)) {
        return "deallocated handle";
      } else {
        return "outdated handle";
      }
  }
}

static inline NqSession::Delegate *ToConn(nq_conn_t c) { 
  return reinterpret_cast<NqSession::Delegate *>(c.p); 
}
static inline NqStream *ToStream(nq_rpc_t c) { 
  return reinterpret_cast<NqStream *>(c.p); 
}
static inline NqStream *ToStream(nq_stream_t c) { 
  return reinterpret_cast<NqStream *>(c.p); 
}
static inline NqAlarm *ToAlarm(nq_alarm_t a) { 
  return reinterpret_cast<NqAlarm *>(a.p); 
}
static inline bool IsOutgoing(bool is_client, nq_sid_t stream_id) {
  return is_client ? ((stream_id % 2) != 0) : ((stream_id % 2) == 0);
}


static nq::logger::level::def cr_severity_to_nq_map[] = {
  nq::logger::level::debug, //const LogSeverity LOG_VERBOSE = -1;  // This is level 1 verbosity
  nq::logger::level::info, //const LogSeverity LOG_INFO = 0;
  nq::logger::level::warn, //const LogSeverity LOG_WARNING = 1;
  nq::logger::level::error, //const LogSeverity LOG_ERROR = 2;
  nq::logger::level::fatal, //const LogSeverity LOG_FATAL = 3;
};
static bool nq_chromium_logger(int severity,
  const char* file, int line, size_t message_start, const std::string& str) {
  auto lv = cr_severity_to_nq_map[severity + 1];
  nq::logger::log(lv, {
    {"tag", "crlog"},
    {"file", file},
    {"line", line},
    {"msg", str.c_str() + message_start},
  });
  ASSERT(severity < logging::LOG_FATAL);
  return true;
}

static void lib_init(bool client) {
  g_at_exit_manager = nq_at_exit_manager(); //anchor
  //set loghandoer for chromium codebase
  logging::SetLogMessageHandler(nq_chromium_logger);
  //break some of the systems according to the env value "CHAOS"
  chaos_init();
  if (client) {
    //initialize c-ares
    auto status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
      nq::logger::fatal({
        {"msg", "fail to init ares"},
        {"status", status}
      });
    }    
  }
}

#define no_ret_closure_call_with_check(__pclsr, ...) \
  if ((__pclsr).proc != nullptr) { \
    (__pclsr).proc((__pclsr).arg, __VA_ARGS__); \
  }



// --------------------------
//
// misc API
//
// --------------------------
NQAPI_THREADSAFE const char *nq_error_detail_code2str(nq_error_t code, int detail_code) {
  if (code == NQ_EQUIC) {
    return QuicErrorCodeToString(static_cast<QuicErrorCode>(code));
  } else if (code == NQ_ERESOLVE) {
    //TODO ares erorr code
    ASSERT(false);
    return "";
  } else {
    ASSERT(false);
    return "";
  }
}


// --------------------------
//
// client API
//
// --------------------------
NQAPI_THREADSAFE nq_client_t nq_client_create(int max_nfd, int max_stream_hint, const nq_dns_conf_t *dns_conf) {
  lib_init(true); //anchor
  auto l = new NqClientLoop(max_nfd, max_stream_hint);
  if (l->Open(max_nfd, dns_conf) < 0) {
    return nullptr;
  }
  return l->ToHandle();
}
NQAPI_BOOTSTRAP void nq_client_destroy(nq_client_t cl) {
  auto c = NqClientLoop::FromHandle(cl);
  c->Close();
  delete c;
}
NQAPI_BOOTSTRAP void nq_client_poll(nq_client_t cl) {
  NqClientLoop::FromHandle(cl)->Poll();
}
NQAPI_BOOTSTRAP bool nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf) {
  auto loop = NqClientLoop::FromHandle(cl);
  //we are not smart aleck and wanna use ipv4 if possible 
  return loop->Resolve(AF_INET, addr->host, addr->port, conf);
}
NQAPI_BOOTSTRAP nq_hdmap_t nq_client_hdmap(nq_client_t cl) {
  return NqClientLoop::FromHandle(cl)->mutable_handler_map()->ToHandle();
}
NQAPI_BOOTSTRAP void nq_client_set_thread(nq_client_t cl) {
  NqClientLoop::FromHandle(cl)->set_main_thread();
}
NQAPI_BOOTSTRAP bool nq_client_resolve_host(nq_client_t cl, int family_pref, const char *hostname, nq_on_resolve_host_t cb) {
  return NqClientLoop::FromHandle(cl)->Resolve(family_pref, hostname, cb);
}
NQAPI_THREADSAFE const char *nq_ntop(const char *src, nq_size_t srclen, char *dst, nq_size_t dstlen) {
  if (NqAsyncResolver::NtoP(src, srclen, dst, dstlen) < 0) {
    return nullptr;
  } else {
    return dst;
  }
}




// --------------------------
//
// server API
//
// --------------------------
NQAPI_THREADSAFE nq_server_t nq_server_create(int n_worker) {
  lib_init(false); //anchor
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
NQAPI_BOOTSTRAP void nq_hdmap_raw_handler(nq_hdmap_t h, nq_stream_handler_t handler) {
  nq::HandlerMap::FromHandle(h)->SetRawHandler(handler);
}


// --------------------------
//
// conn API
//
// --------------------------
NQAPI_THREADSAFE void nq_conn_close(nq_conn_t conn) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), NqBoxer::OpCode::Disconnect);
}
NQAPI_THREADSAFE void nq_conn_reset(nq_conn_t conn) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), NqBoxer::OpCode::Reconnect);
} 
NQAPI_THREADSAFE void nq_conn_flush(nq_conn_t conn) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), NqBoxer::OpCode::Flush);
} 
NQAPI_THREADSAFE bool nq_conn_is_client(nq_conn_t conn) {
  return NqSerial::IsClient(conn.s);
}
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn, nq_on_conn_validate_t cb) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    no_ret_closure_call_with_check(cb, conn, nullptr);
    return true;
  }, "nq_conn_is_valid");
  no_ret_closure_call_with_check(cb, conn, INVALID_REASON(conn));
  return false;
}
NQAPI_THREADSAFE void nq_conn_modify_hdmap(nq_conn_t conn, nq_on_conn_modify_hdmap_t modifier) {
  NqSession::Delegate *d;
  NqBoxer *b;
  UNWRAP_CONN_OR_ENQUEUE(conn, d, b, {
    auto hm = d->ResetHandlerMap()->ToHandle();
    nq_closure_call(modifier, hm);
  }, {
    b->InvokeConn(conn.s, ToConn(conn), NqBoxer::OpCode::ModifyHandlerMap, nq_to_dyn_closure(modifier));
  }, "nq_conn_modify_hdmap");
}
NQAPI_THREADSAFE nq_time_t nq_conn_reconnect_wait(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return nq_time_usec(d->ReconnectDurationUS());
  }, "nq_conn_reconnect_wait");
  return 0;
}
NQAPI_CLOSURECALL void *nq_conn_ctx(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNSAFE_UNWRAP_CONN(conn, d, {
    return d->Context();
  }, "nq_conn_ctx");
  return nullptr;
}
//these are hidden API for test, because returned value is unstable
//when used with client connection (under reconnection)
NQAPI_THREADSAFE nq_cid_t nq_conn_cid(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->ConnectionId();
  }, "nq_conn_cid");
  return 0;
}
NQAPI_THREADSAFE void nq_conn_reachability_change(nq_conn_t conn, nq_reachability_t state) {
  NqUnwrapper::UnwrapBoxer(conn)->InvokeConn(conn.s, ToConn(conn), NqBoxer::OpCode::Reachability, state);
}
NQAPI_THREADSAFE int nq_conn_fd(nq_conn_t conn) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, {
    return d->UnderlyingFd();
  }, "nq_conn_fd");
  return -1; 
}


// --------------------------
//
// stream API
//
// --------------------------
static inline void conn_stream_common(nq_conn_t conn, const char *name, void *ctx, const char *purpose) {
  NqSession::Delegate *d;
  UNWRAP_CONN(conn, d, ({
    d->InitStream(name, ctx);
  }), purpose);
}


NQAPI_THREADSAFE void nq_conn_stream(nq_conn_t conn, const char *name, void *ctx) {
  conn_stream_common(conn, name, ctx, "nq_conn_stream");
}
NQAPI_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, ({
    return NqUnwrapper::Stream2Conn(s.s, st);
  }), "nq_stream_conn");
  return INVALID_HANDLE<nq_conn_t>(IHR_CONN_NOT_FOUND);
}
NQAPI_THREADSAFE nq_alarm_t nq_stream_alarm(nq_stream_t s) {
  return NqUnwrapper::UnwrapBoxer(s)->NewAlarm()->ToHandle();
}
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s, nq_on_stream_validate_t cb) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    no_ret_closure_call_with_check(cb, s, nullptr);
    return true;
  }, "nq_stream_is_valid");
  no_ret_closure_call_with_check(cb, s, INVALID_REASON(s));
  return false;
}
NQAPI_THREADSAFE bool nq_stream_outgoing(nq_stream_t s, bool *p_valid) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    *p_valid = true;
    return IsOutgoing(NqSerial::IsClient(s.s), st->id());
  }, "nq_stream_close");
  *p_valid = false;
  return false;
}
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s) {
  NqUnwrapper::UnwrapBoxer(s)->InvokeStream(s.s, ToStream(s), NqBoxer::OpCode::Disconnect);
}
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen) {
  NqStream *st; NqBoxer *b;
  UNWRAP_STREAM_OR_ENQUEUE(s, st, b, {
    st->Handler<NqStreamHandler>()->Send(data, datalen);
  }, {
    b->InvokeStream(s.s, st, NqBoxer::OpCode::Send, data, datalen);
  }, "nq_stream_send");
}
NQAPI_THREADSAFE void nq_stream_send_ex(nq_stream_t s, const void *data, nq_size_t datalen, nq_stream_opt_t *opt) {
  NqStream *st; NqBoxer *b;
  UNWRAP_STREAM_OR_ENQUEUE(s, st, b, {
    st->Handler<NqStreamHandler>()->SendEx(data, datalen, *opt);
  }, {
    b->InvokeStream(s.s, st, NqBoxer::OpCode::SendEx, data, datalen, *opt);
  }, "nq_stream_send");
}
NQAPI_THREADSAFE void nq_stream_task(nq_stream_t s, nq_on_stream_task_t cb) {
  NqUnwrapper::UnwrapBoxer(s)->InvokeStream(s.s, ToStream(s), NqBoxer::OpCode::Task, nq_to_dyn_closure(cb));
}
NQAPI_CLOSURECALL void *nq_stream_ctx(nq_stream_t s) {
  NqStream *st;
  UNSAFE_UNWRAP_STREAM(s, st, {
    return st->Context();
  }, "nq_stream_ctx");
}
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s) {
  NqStream *st;
  UNWRAP_STREAM(s, st, {
    return st->id();
  }, "nq_stream_sid");
  return 0;
}



// --------------------------
//
// rpc API
//
// --------------------------
static inline void rpc_reply_common(nq_rpc_t rpc, nq_error_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
  ASSERT(result <= 0);
  NqStream *st; NqBoxer *b;
  UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
    st->Handler<NqSimpleRPCStreamHandler>()->Reply(result, msgid, data, datalen);
  }, {
    b->InvokeStream(rpc.s, st, NqBoxer::OpCode::Reply, result, msgid, data, datalen);
  }, result < 0 ? "nq_rpc_error" : "nq_rpc_reply");
}


NQAPI_THREADSAFE void nq_conn_rpc(nq_conn_t conn, const char *name, void *ctx) {
  conn_stream_common(conn, name, ctx, "nq_conn_rpc");
}
NQAPI_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, ({
    return NqUnwrapper::Stream2Conn(rpc.s, st);
  }), "nq_rpc_conn");
  return INVALID_HANDLE<nq_conn_t>(IHR_CONN_NOT_FOUND);
}
NQAPI_THREADSAFE nq_alarm_t nq_rpc_alarm(nq_rpc_t rpc) {
  return NqUnwrapper::UnwrapBoxer(rpc)->NewAlarm()->ToHandle();
}
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc, nq_on_rpc_validate_t cb) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    no_ret_closure_call_with_check(cb, rpc, nullptr);
    return true;
  }, "nq_rpc_is_valid");
  no_ret_closure_call_with_check(cb, rpc, INVALID_REASON(rpc));
  return false;
}
NQAPI_THREADSAFE bool nq_rpc_outgoing(nq_rpc_t rpc, bool *p_valid) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    *p_valid = true;
    return IsOutgoing(NqSerial::IsClient(rpc.s), st->id());
  }, "nq_stream_close");
  *p_valid = false;
  return false;
}
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc) {
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, ToStream(rpc), NqBoxer::OpCode::Disconnect);
}
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_on_rpc_reply_t on_reply) {
  ASSERT(type > 0);
  NqStream *st; NqBoxer *b;
  UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
    st->Handler<NqSimpleRPCStreamHandler>()->Call(type, data, datalen, on_reply);
  }, {
    b->InvokeStream(rpc.s, st, NqBoxer::OpCode::Call, type, data, datalen, on_reply);
  }, "nq_rpc_call");
}
NQAPI_THREADSAFE void nq_rpc_call_ex(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_rpc_opt_t *opts) {
  ASSERT(type > 0);
  NqStream *st; NqBoxer *b;
  UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
    st->Handler<NqSimpleRPCStreamHandler>()->CallEx(type, data, datalen, *opts);
  }, {
    b->InvokeStream(rpc.s, st, NqBoxer::OpCode::CallEx, type, data, datalen, *opts);
  }, "nq_rpc_call_ex");
}
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen) {
  ASSERT(type > 0);
  NqStream *st; NqBoxer *b;
  UNWRAP_STREAM_OR_ENQUEUE(rpc, st, b, {
    st->Handler<NqSimpleRPCStreamHandler>()->Notify(type, data, datalen);
  }, {
    b->InvokeStream(rpc.s, st, NqBoxer::OpCode::Notify, type, data, datalen);
  }, "nq_rpc_notify");
}
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
  rpc_reply_common(rpc, NQ_OK, msgid, data, datalen);
}
NQAPI_THREADSAFE void nq_rpc_error(nq_rpc_t rpc, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
  rpc_reply_common(rpc, NQ_EUSER, msgid, data, datalen);
}
NQAPI_THREADSAFE void nq_rpc_task(nq_rpc_t rpc, nq_on_rpc_task_t cb) {
  NqUnwrapper::UnwrapBoxer(rpc)->InvokeStream(rpc.s, ToStream(rpc), NqBoxer::OpCode::Task, nq_to_dyn_closure(cb));
}
NQAPI_CLOSURECALL void *nq_rpc_ctx(nq_rpc_t rpc) {
  NqStream *st;
  UNSAFE_UNWRAP_STREAM(rpc, st, {
    return st->Context();
  }, "nq_rpc_ctx");
}
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t rpc) {
  NqStream *st;
  UNWRAP_STREAM(rpc, st, {
    return st->id();
  }, "nq_rpc_sid");
  return 0;
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



// --------------------------
//
// alarm API
//
// --------------------------
NQAPI_THREADSAFE void nq_alarm_set(nq_alarm_t a, nq_time_t invocation_ts, nq_on_alarm_t cb) {
  NqUnwrapper::UnwrapBoxer(a)->InvokeAlarm(a.s, ToAlarm(a), NqBoxer::OpCode::Start, invocation_ts, cb);
}
NQAPI_THREADSAFE void nq_alarm_destroy(nq_alarm_t a) {
  NqUnwrapper::UnwrapBoxer(a)->InvokeAlarm(a.s, ToAlarm(a), NqBoxer::OpCode::Finalize);
}
NQAPI_THREADSAFE bool nq_alarm_is_valid(nq_alarm_t a) {
  //because memory pointed to a.p never returned to heap, this check should be work always.
  auto p = static_cast<NqAlarm *>(a.p);
  return p->alarm_serial() == a.s;
}



// --------------------------
//
// log API
//
// --------------------------
NQAPI_BOOTSTRAP void nq_log_config(const nq_logconf_t *conf) {
  nq::logger::configure(conf->callback, conf->id, conf->manual_flush);
}
NQAPI_THREADSAFE void nq_log(nq_loglv_t lv, const char *msg, nq_logparam_t *params, int n_params) {
  nq::json j = {
    {"msg", msg}
  };
  for (int i = 0; i < n_params; i++) {
    auto p = params[i];
    switch (p.type) {
    case NQ_LOG_INTEGER:
      j[p.key] = p.value.n;
      break;
    case NQ_LOG_STRING:
      j[p.key] = p.value.s;
      break;
    case NQ_LOG_DECIMAL:
      j[p.key] = p.value.d;
      break;
    case NQ_LOG_BOOLEAN:
      j[p.key] = p.value.b;
      break;
    }
  }
  nq::logger::log((nq::logger::level::def)(int)lv, j);
}
NQAPI_THREADSAFE void nq_log_flush() {
  nq::logger::flush();
}
