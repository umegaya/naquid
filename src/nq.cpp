#include "nq.h"

#include "basis/timespec.h"

#include "core/nq_client.h"
#include "core/nq_server.h"
#include "core/nq_stream.h"

#include "base/at_exit.h"

extern base::AtExitManager *nq_at_exit_manager();

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



// --------------------------
//
// client API
//
// --------------------------
nq_client_t nq_client_create(int max_nfd) {
	auto l = new NqClientLoop();
	if (l->Open(max_nfd) < 0) {
		return nullptr;
	}
	return l->ToHandle();
}
void nq_client_destroy(nq_client_t cl) {
	delete NqClientLoop::FromHandle(cl);
}
void nq_client_poll(nq_client_t cl) {
	NqClientLoop::FromHandle(cl)->Poll();
}
nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf) {
  auto loop = NqClientLoop::FromHandle(cl);
  auto cf = NqClientConfig(*conf);
	auto c = loop->Create(addr->host, addr->port, cf);
  if (c == nullptr) {
    return INVALID_HANDLE<nq_conn_t>("fail to create client");
  }
  return loop->Box(c);
}
nq_hdmap_t nq_client_hdmap(nq_client_t cl) {
	return NqClientLoop::FromHandle(cl)->mutable_handler_map()->ToHandle();
}
void nq_client_set_thread(nq_client_t cl) {
  NqClientLoop::FromHandle(cl)->set_main_thread();
}




// --------------------------
//
// server API
//
// --------------------------
nq_server_t nq_server_create(int n_worker) {
	auto sv = new NqServer(n_worker);
	return sv->ToHandle();
}
nq_hdmap_t nq_server_listen(nq_server_t sv, const nq_addr_t *addr, const nq_svconf_t *conf) {
	auto aem = nq_at_exit_manager();
	auto psv = NqServer::FromHandle(sv);
	return psv->Open(addr, conf)->ToHandle();
}
void nq_server_start(nq_server_t sv, bool block) {
	auto psv = NqServer::FromHandle(sv);
	psv->Start(block);
}
void nq_server_join(nq_server_t sv) {
	auto psv = NqServer::FromHandle(sv);
	psv->Join();
	delete psv;
}



// --------------------------
//
// hdmap API
//
// --------------------------
bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler) {
	return nq::HandlerMap::FromHandle(h)->AddEntry(name, handler);
}
bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler) {
	return nq::HandlerMap::FromHandle(h)->AddEntry(name, handler);
}
bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory) {
	return nq::HandlerMap::FromHandle(h)->AddEntry(name, factory);
}



// --------------------------
//
// conn API
//
// --------------------------
NQ_THREADSAFE void nq_conn_close(nq_conn_t conn) {
  NqBoxer::From(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Disconnect);
}
NQ_THREADSAFE void nq_conn_reset(nq_conn_t conn) {
  NqBoxer::From(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Reconnect);
} 
NQ_THREADSAFE void nq_conn_flush(nq_conn_t conn) {
  NqBoxer::From(conn)->InvokeConn(conn.s,NqBoxer::OpCode::Flush);
} 
NQ_THREADSAFE bool nq_conn_is_client(nq_conn_t conn) {
	return NqBoxer::From(conn)->IsClient();
}
NQ_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn) {
  return NqBoxer::From(conn)->Find(conn) != nullptr;
}
NQ_OWNERTHREAD nq_hdmap_t nq_conn_hdmap(nq_conn_t conn) {
  auto c = NqBoxer::Unbox(conn);
	return c != nullptr ? c->ResetHandlerMap()->ToHandle() : nullptr;
}
NQ_THREADSAFE uint64_t nq_conn_reconnect_wait(nq_conn_t conn) {
  auto c = NqBoxer::From(conn)->Find(conn);
  return c != nullptr ? c->ReconnectDurationUS() : 0;
}
NQ_THREADSAFE nq_cid_t nq_conn_id(nq_conn_t conn) {
  auto c = NqBoxer::From(conn)->Find(conn);
  return c != nullptr ? c->Id() : 0;
}


// --------------------------
//
// stream API
//
// --------------------------
NQ_THREADSAFE nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name) {
  NqSession::Delegate *d;
  return NqBoxer::From(conn)->Unbox(conn.s, &d) == NqBoxer::UnboxResult::Ok ? 
    d->NewStreamCast<NqStream>(name)->ToHandle<nq_stream_t>() : 
    INVALID_HANDLE<nq_stream_t>("fail to unbox");
}
NQ_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s) {
  auto b = NqBoxer::From(s);
  nq_conn_t c = {
    .p = s.p, 
    .s = b->IsClient() ? 
      NqConnSerialCodec::FromClientStreamSerial(s.s) :
      NqConnSerialCodec::FromServerStreamSerial(s.s) 
  };
  return c;
}
NQ_THREADSAFE bool nq_stream_is_valid(nq_stream_t s) {
  return NqBoxer::From(s)->Find(s) != nullptr;
}
NQ_THREADSAFE void nq_stream_close(nq_stream_t s) {
	NqBoxer::From(s)->InvokeStream(s.s, NqBoxer::OpCode::Disconnect);
}
NQ_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen) {
  NqBoxer::From(s)->InvokeStream(s.s, NqBoxer::OpCode::Send, data, datalen);
}




// --------------------------
//
// rpc API
//
// --------------------------
NQ_THREADSAFE nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name) {
  NqSession::Delegate *d;
  return NqBoxer::From(conn)->Unbox(conn.s, &d) == NqBoxer::UnboxResult::Ok ? 
    d->NewStreamCast<NqStream>(name)->ToHandle<nq_rpc_t>() : 
    INVALID_HANDLE<nq_rpc_t>("fail to unbox");
}
NQ_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc) {
  auto b = NqBoxer::From(rpc);
  nq_conn_t c = {
    .p = rpc.p, 
    .s = b->IsClient() ? 
      NqConnSerialCodec::FromClientStreamSerial(rpc.s) :
      NqConnSerialCodec::FromServerStreamSerial(rpc.s) 
  };
  return c;
}
NQ_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc) {
  return NqBoxer::From(rpc)->Find(rpc) != nullptr;
}
NQ_THREADSAFE void nq_rpc_close(nq_rpc_t rpc) {
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Disconnect);
}
NQ_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply) {
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Call, type, data, datalen, on_reply);
}
NQ_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen) {
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Notify, type, data, datalen);
}
NQ_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen) {
  NqBoxer::From(rpc)->InvokeStream(rpc.s, NqBoxer::OpCode::Reply, result, msgid, data, datalen);
}



// --------------------------
//
// time API
//
// --------------------------
NQ_THREADSAFE nq_time_t nq_time_now() {
	return nq::clock::now();
}
NQ_THREADSAFE nq_unix_time_t nq_time_unix() {
	long s, us;
	nq::clock::now(s, us);
	return s;
}
NQ_THREADSAFE nq_time_t nq_time_sleep(nq_time_t d) {
	return nq::clock::sleep(d);
}
NQ_THREADSAFE nq_time_t nq_time_pause(nq_time_t d) {
	return nq::clock::pause(d);
}
