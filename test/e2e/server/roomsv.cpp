#include "room.h"

#include <basis/defs.h>
#include <basis/convert.h>
#include <basis/endian.h>

using namespace nqtest;

static const int kThreads = 4;  //4 thread server

/* conn callbacks */
void on_conn_open(void *, nq_conn_t c, void **ppctx) {
  TRACE("on_conn_open event:%llx|%p", c.s.data[0], c.p);
}
void on_conn_close(void *, nq_conn_t c, nq_error_t r, const nq_error_detail_t *detail, bool) {
  TRACE("on_conn_close reason:%llx|%p %s", c.s.data[0], c.p, detail->msg);
  free(nq_conn_ctx(c));
}



/* rpc callbacks */
bool on_rpc_open(void *p, nq_rpc_t rpc, void **ppctx) {
  return room_member_init(rpc, ppctx);
}
void on_rpc_close(void *p, nq_rpc_t rpc) {
  room_exit(rpc);
}

void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {
  switch (type) {
    case RpcType::Ping:
    case RpcType::Raise:
    case RpcType::NotifyText:
    case RpcType::ServerStream:
    case RpcType::Sleep:
    case RpcType::Close:
    case RpcType::SetupReject:
      ASSERT(false);
      break;
    case RpcType::BcastJoin:
      {
  TRACE("rpc join: %llu", nq::Endian::NetbytesToHost<uint64_t>(data));
        room_enter(rpc, msgid, data, len);
      }
      break;
    case RpcType::BcastReply:
      {
  TRACE("rpc bcast reply: %llu", nq::Endian::NetbytesToHost<uint64_t>(data));
        room_bcast_reply(rpc, msgid, data, len);
      }
      break;
    default:
      ASSERT(false);
      break;
  }
}
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_error_t result, const void *data, nq_size_t len) {

}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {
  switch (type) {
    case RpcType::Bcast:
      room_bcast(rpc, data, len);
      break;
    default:
      ASSERT(false);
      break;
}
}


/* setup server */
void setup_server(nq_server_t sv, int port) {
  nq_addr_t addr = {
    "0.0.0.0", 
    "./server/certs/leaf_cert.pem", 
    "./server/certs/leaf_cert.pkcs8", 
    nullptr,
    port
  };

  nq_svconf_t conf;
  conf.quic_secret = "e336e27898ff1e17ac79e82fa0084999";
  conf.quic_cert_cache_size = 0; //use default
  conf.accept_per_loop = 0; //use default
  conf.max_session_hint = 1024;
  conf.max_stream_hint = 1024 * 4;
  conf.handshake_timeout = nq_time_sec(120);
  conf.idle_timeout = nq_time_sec(60);
  conf.shutdown_timeout = 0; //use default
  nq_closure_init(conf.on_open, on_server_conn_open, on_conn_open, nullptr);
  nq_closure_init(conf.on_close, on_server_conn_close, on_conn_close, nullptr);

  nq_hdmap_t hm = nq_server_listen(sv, &addr, &conf);

  nq_rpc_handler_t rh;
  rh.timeout = 0; //use default
  nq_closure_init(rh.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(rh.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(rh.on_rpc_open, on_rpc_open, on_rpc_open, nullptr);
  nq_closure_init(rh.on_rpc_close, on_rpc_close, on_rpc_close, nullptr);
  nq_hdmap_rpc_handler(hm, "rpc", rh);
}



/* main */
int main(int argc, char *argv[]){
  int n_threads = kThreads;
  if (argc > 1) {
    n_threads = nq::convert::Do(argv[1], kThreads);
  }
  nq::logger::info({
    {"msg", "launch server"},
    {"n_threads", n_threads},
  });
  nq_server_t sv = nq_server_create(n_threads);

  setup_server(sv, 8443);

  nq_server_start(sv, false);

  nq_time_sleep(nq_time_sec(5));
  nq_server_join(sv);

  return 0;
}