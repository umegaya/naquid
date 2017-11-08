#include <nq.h>
#include "basis/endian.h"
#include <stdio.h>


/* conn callback */
bool on_conn_open(void *, nq_conn_t, nq_handshake_event_t hsev, void *) {
  TRACE("on_conn_open event:%d\n", hsev);
  return true;
}
nq_time_t on_conn_close(void *, nq_conn_t, nq_result_t, const char *detail, bool) {
  TRACE("on_conn_close: reason:%s\n", detail);
  return nq_time_sec(2);
}



/* rpc stream callback */
bool on_stream_open(void *p, nq_stream_t s) {
  return true;
}
void on_stream_close(void *p, nq_stream_t s) {
  return;
}
void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {

}
static int idx = 0;
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_result_t result, const void *data, nq_size_t len) {
  ASSERT(result >= 0);
  auto sent_ts = nq::Endian::NetbytesToHost64((const char *)data);
  printf("req %d: sent_ts: %llu, latency %lf sec\n", ++idx, sent_ts, ((double)(nq_time_now() - sent_ts) / (1000 * 1000 * 1000)));
}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {

}



/* helper */
static void send_rpc(nq_rpc_t rpc, nq_closure_t reply_cb) {
  nq_time_t now = nq::Endian::HostToNet(nq_time_now());
  TRACE("now = %llx\n", now);
  nq_rpc_call(rpc, 1, (const void *)&now, sizeof(now), reply_cb);
}



/* main */
int main(int argc, char *argv[]){
  nq_client_t cl = nq_client_create(1); //1 connection client

  nq_hdmap_t hm;
  hm = nq_client_hdmap(cl);
  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_stream_open, on_stream_open, on_stream_open, nullptr);
  nq_closure_init(handler.on_stream_close, on_stream_close, on_stream_close, nullptr);
  nq_hdmap_rpc_handler(hm, "test", handler);

  nq_addr_t addr = {
    "localhost", nullptr, nullptr, nullptr,
    8443
  };
  nq_clconf_t conf;
  nq_closure_init(conf.on_open, on_conn_open, on_conn_open, nullptr);
  nq_closure_init(conf.on_close, on_conn_close, on_conn_close, nullptr);
  nq_conn_t c = nq_client_connect(cl, &addr, &conf);
  if (!nq_conn_is_valid(c)) {
    printf("fail to create connection\n");
    return -1;
  }

  nq_rpc_t rpc = nq_conn_rpc(c, "test");

  nq_time_t end = nq_time_now() + nq_time_sec(3);
  nq_closure_t reply_cb;
  nq_closure_init(reply_cb, on_rpc_reply, on_rpc_reply, nullptr);
  while (nq_time_now() < end) {
    send_rpc(rpc, reply_cb);
    //nq_time_pause(nq_time_msec(100));
    nq_client_poll(cl);
  }

  nq_client_destroy(cl);

  return 0;
}
