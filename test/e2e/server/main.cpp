#include <nq.h>
#include "basis/endian.h"

static const int kThreads = 4;  //4 thread server
static const int kRpcOk = 0;
static const int kRpcPing = 1;



/* conn callback */
bool on_conn_open(void *, nq_conn_t, nq_handshake_event_t hsev, void *) {
  TRACE("on_conn_open event:%d\n", hsev);
  return true;
}
nq_time_t on_conn_close(void *, nq_conn_t, nq_result_t, const char *detail, bool) {
  TRACE("on_conn_close reason:%s\n", detail);
  return 0;
}



/* rpc stream callback */
bool on_stream_open(void *p, nq_stream_t s) {
  return true;
}
void on_stream_close(void *p, nq_stream_t s) {
}
void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {
  ASSERT(type == kRpcPing);
  ASSERT(len == sizeof(nq_time_t));
  auto peer_ts = nq::Endian::NetbytesToHost64((const char *)data);
  nq_rpc_reply(rpc, kRpcOk, msgid, &peer_ts, sizeof(peer_ts));
}
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_result_t result, const void *data, nq_size_t len) {

}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {

}



/* main */
int main(int argc, char *argv[]){
  nq_server_t sv = nq_server_create(kThreads);

  nq_hdmap_t hm;
  nq_addr_t addr = {
    "0.0.0.0", "./test/e2e/server/certs/leaf_cert.pem", "./test/e2e/server/certs/leaf_cert.pkcs8", nullptr,
    8443
  };
  nq_svconf_t conf;
  conf.quic_secret = nullptr;
  conf.quic_cert_cache_size = 0;
  nq_closure_init(conf.on_open, on_conn_open, on_conn_open, nullptr);
  nq_closure_init(conf.on_close, on_conn_close, on_conn_close, nullptr);
  hm = nq_server_listen(sv, &addr, &conf);

  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_stream_open, on_stream_open, on_stream_open, nullptr);
  nq_closure_init(handler.on_stream_close, on_stream_close, on_stream_close, nullptr);
  nq_hdmap_rpc_handler(hm, "test", handler);

  nq_server_start(sv, false);

  nq_time_sleep(nq_time_sec(5));
  nq_server_join(sv);

  return 0;
}
