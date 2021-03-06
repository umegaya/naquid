#include <nq.h>
#include <basis/endian.h>
#include <basis/convert.h>

#include "rpctypes.h"

#define N_CLIENT 4

static bool g_alive = true;
static int g_send_limit = 0;

struct context {
  int n_send;
  nq_rpc_t rpc;
  nq_time_t sum_latency;
  nq_on_rpc_reply_t on_reply;
  nq_on_alarm_t on_alarm;
  nq_alarm_t alarm;
};


void send_ping(context *c) {
  auto now = nq_time_now();
  char buff[sizeof(now)];
  nq::Endian::HostToNetbytes(now, buff);
  nq_rpc_call(c->rpc, nqtest::RpcType::Ping, buff, sizeof(buff), c->on_reply);
}


/* alarm callback */
void on_alarm(void *arg, nq_time_t *next) {
  auto v = (context *)arg;
  send_ping(v);
  *next += nq_time_sec(1);
}


/* conn callback */
void on_conn_open(void *arg, nq_conn_t c, void **) {
  nq_conn_rpc(c, "rpc", arg);
}
nq_time_t on_conn_close(void *arg, nq_conn_t c, nq_error_t e, const nq_error_detail_t *detail, bool remote) {
  TRACE("on_conn_close:%d(from %s) reason:%s(%d)\n", e, remote ? "remote": "local", detail->msg, detail->code);
  return nq_time_sec(1);
}
void on_conn_finalize(void *arg, nq_conn_t c, void *ctx) {
  g_alive = false;
}



/* rpc callback */
bool on_rpc_open(void *p, nq_rpc_t rpc, void **ctx) {
  auto v = *(context **)ctx;
  v->rpc = rpc;
  v->alarm = nq_rpc_alarm(rpc);
  nq_alarm_set(v->alarm, nq_time_now(), v->on_alarm);
  return true;
}
void on_rpc_close(void *p, nq_rpc_t rpc) {
  auto v = (context *)nq_rpc_ctx(rpc);
  TRACE("on_rpc_close at %d", v->n_send);
  nq_alarm_destroy(v->alarm);
  return;
}
void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {

}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {

}
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_error_t result, const void *data, nq_size_t len) {
  if (result < 0) {
    TRACE("rpc error %d", result);
    return;
  }
  auto v = (context *)nq_rpc_ctx(rpc);
  auto sent = nq::Endian::NetbytesToHost<nq_time_t>((const char *)data);
  nq_time_t latency = nq_time_now() - sent;
  v->sum_latency += latency;
  v->n_send++;
  TRACE("ping:%d, latency:%lf msec, average:%lf msec", v->n_send, (double)latency / (1000 * 1000), (double)v->sum_latency / (v->n_send * 1000 * 1000));
  if (g_send_limit > 0 && v->n_send > g_send_limit) {
    nq_conn_close(nq_rpc_conn(rpc));
  }
}



int main(int argc, char *argv[]){
  bool track_reachability = false;
  if (argc > 1) {
    track_reachability = nq::convert::Do(argv[1], 0) != 0;
    if (argc > 2) {
      g_send_limit = nq::convert::Do(argv[2], 0);
    }
  }
  //init client and its stream handler (rpc)
  nq_client_t cl = nq_client_create(N_CLIENT, N_CLIENT * 4, nullptr); //N_CLIENT connection client
  nq_hdmap_t hm = nq_client_hdmap(cl);
  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_rpc_open, on_rpc_open, nullptr);
  nq_closure_init(handler.on_rpc_close, on_rpc_close, nullptr);
  handler.use_large_msgid = false;
  handler.timeout = nq_time_sec(10);
  nq_hdmap_rpc_handler(hm, "rpc", handler);

  //connection context
  context ctx;
  nq_closure_init(ctx.on_reply, on_rpc_reply, &ctx);
  nq_closure_init(ctx.on_alarm, on_alarm, &ctx);
  ctx.n_send = 0;
  ctx.sum_latency = 0;

  //address
  nq_addr_t addr = {
    "test.qrpc.io", nullptr, nullptr, nullptr,
    8443
  };

  //connection config
  nq_clconf_t conf;
  conf.insecure = false;
  conf.track_reachability = track_reachability;
  conf.idle_timeout = nq_time_sec(60);
  conf.handshake_timeout = nq_time_sec(120);
  nq_closure_init(conf.on_open, on_conn_open, &ctx);
  nq_closure_init(conf.on_close, on_conn_close, &ctx);
  nq_closure_init(conf.on_finalize, on_conn_finalize, &ctx)

  //connect
  if (!nq_client_connect(cl, &addr, &conf)) {
    return -1;
  }

  //poll while alive
  while (g_alive) {
    //nq_time_pause(nq_time_msec(10));
    nq_client_poll(cl);
  }

  //clean up
  nq_client_destroy(cl);

  return 0;
}
