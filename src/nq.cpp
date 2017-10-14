#include "nq.h"

#include "core/nq_client.h"
#include "core/nq_server.h"
#include "core/nq_stream.h"

using namespace net;

// --------------------------
//
// client API
//
// --------------------------
nq_client_t nq_client_create(int max_nfd) {
	auto l = new NqClientLoop();
	if (l->Open(max_nfd < 0 ? 4196 /* TODO(iyatomi): use `ulimit -n` */ : max_nfd) < 0) {
		return nullptr;
	}
	return (nq_client_t)l;
}
void nq_client_destroy(nq_client_t cl) {
	((NqClientLoop *)cl)->Close();	
}
void nq_client_poll(nq_client_t cl) {
	((NqClientLoop *)cl)->Poll();
}
nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf) {
	NqClientConfig cf(*conf);
	auto c = ((NqClientLoop *)cl)->Create(addr->host, addr->port, cf);
	return (nq_conn_t)c;
}
nq_hdmap_t nq_client_hdmap(nq_client_t cl) {
	return (nq_hdmap_t)((NqClientLoop *)cl)->mutable_handler_map();	
}



// --------------------------
//
// server API
//
// --------------------------
nq_server_t nq_server_create(int n_worker) {
	auto sv = new NqServer(n_worker);
	return (nq_server_t)sv;
}
nq_hdmap_t nq_server_listen(nq_server_t sv, const nq_addr_t *addr, const nq_svconf_t *conf) {
	auto psv = (NqServer *)sv;
	return (nq_hdmap_t)psv->Open(addr, conf);
}
void nq_server_start(nq_server_t sv, bool block) {
	auto psv = (NqServer *)sv;
	psv->Start(block);
}
void nq_server_join(nq_server_t sv) {
	auto psv = (NqServer *)sv;
	psv->Join();
}



// --------------------------
//
// hdmap API
//
// --------------------------
bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler) {
	return ((nq::HandlerMap *)h)->AddEntry(name, handler);
}
bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler) {
	return ((nq::HandlerMap *)h)->AddEntry(name, handler);
}
bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory) {
	return ((nq::HandlerMap *)h)->AddEntry(name, factory);
}



// --------------------------
//
// conn API
//
// --------------------------
void nq_conn_close(nq_conn_t conn) {
	auto d = (NqSession::Delegate *)conn;
	d->Disconnect();
}
int nq_conn_reset(nq_conn_t conn) {
	auto d = (NqSession::Delegate *)conn;
	return d->Reconnect() ? NQ_OK : NQ_NOT_SUPPORT;	
} 
bool nq_conn_is_client(nq_conn_t conn) {
	auto d = (NqSession::Delegate *)conn;
	return d->IsClient();		
}
nq_hdmap_t nq_conn_hdmap(nq_conn_t conn) {
	return (nq_hdmap_t)((NqSession::Delegate*)conn)->ResetHandlerMap();
}



// --------------------------
//
// stream API
//
// --------------------------
nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name) {
	return (nq_stream_t)((NqSession::Delegate *)conn)->NewStream(name);
}
void nq_stream_close(nq_stream_t s) {
	auto h = (NqStreamHandler *)s;
	h->Disconnect();
}
void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen) {
	auto h = (NqStreamHandler *)s;
	h->Send(data, datalen);
}



// --------------------------
//
// rpc API
//
// --------------------------
nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name) {
	return (nq_rpc_t)((NqSession::Delegate *)conn)->NewStream(name);
}
void nq_rpc_close(nq_rpc_t rpc) {
	auto h = (NqSimpleRPCStreamHandler *)rpc;
	h->Disconnect();
}
void nq_rpc_call(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen, nq_closure_t on_result) {
	auto h = (NqSimpleRPCStreamHandler *)rpc;
	h->Send(type, data, datalen, on_result);
}
void nq_rpc_notify(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen) {
	auto h = (NqSimpleRPCStreamHandler *)rpc;
	h->Send(type, data, datalen);
}
