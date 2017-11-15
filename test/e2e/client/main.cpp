#include <nq.h>
#include "basis/endian.h"
#include <stdio.h>

#include <map>

#define N_CLIENT (100)
#define N_SEND (1500)

/* static variables */
struct closure_ctx {
  uint64_t seed;
  uint64_t last_recv;
};
std::map<uint64_t, bool> g_connected;
nq_conn_t g_cs[N_CLIENT];
nq_rpc_t g_rpcs[N_CLIENT];
closure_ctx g_ctxs[N_CLIENT];
nq_closure_t g_reps[N_CLIENT];



/* helper */
static void send_rpc(nq_rpc_t rpc, nq_closure_t reply_cb) {
  auto v = (closure_ctx *)reply_cb.arg;
  v->seed++;
  uint64_t seq = nq::Endian::HostToNet(v->seed);
  //fprintf(stderr, "rpc %llx, seq = %llx\n", rpc.s, seq);
  nq_rpc_call(rpc, 1, (const void *)&seq, sizeof(seq), reply_cb);
}



/* conn callback */
bool on_conn_open(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void *) {
  intptr_t idx = (intptr_t)arg;
  fprintf(stderr, "on_conn_open event:%ld %d\n", idx, hsev);
  if (hsev == NQ_HS_DONE) {
    g_connected[c.s] = true;
    auto rpc = g_rpcs[idx];
    auto rep = g_reps[idx];
    for (int j = 0; j < N_SEND; j++) {
      send_rpc(rpc, rep);
    }
  }
  return true;
}
nq_time_t on_conn_close(void *arg, nq_conn_t c, nq_result_t, const char *detail, bool) {
  intptr_t idx = (intptr_t)arg;
  fprintf(stderr, "on_conn_close: reason:%ld %s\n", idx, detail);
  g_connected[c.s] = false;
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
static uint64_t g_idx = 0;
static bool g_alive = true;
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_result_t result, const void *data, nq_size_t len) {
  ASSERT(result >= 0);
  auto v = (closure_ctx *)p;
  auto recv_seq = nq::Endian::NetbytesToHost<uint64_t>((const char *)data);
  if (recv_seq != (v->last_recv + 1)) {
    fprintf(stderr, "rpc %llx, seq leaps: %llu => %llu\n", rpc.s, v->last_recv, recv_seq);
    exit(1);
  } else {
    v->last_recv = recv_seq;
  }
  g_idx++;
  if (g_idx >= (N_CLIENT * N_SEND)) {
    g_alive = false;
  }
  //printf("req %d: sent_ts: %llu, latency %lf sec\n", ++idx, sent_ts, ((double)(nq_time_now() - sent_ts) / (1000 * 1000 * 1000)));
}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {

}



/* main */
int main(int argc, char *argv[]){
  nq_client_t cl = nq_client_create(N_CLIENT); //N_CLIENT connection client

  nq_hdmap_t hm;
  hm = nq_client_hdmap(cl);
  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_stream_open, on_stream_open, on_stream_open, nullptr);
  nq_closure_init(handler.on_stream_close, on_stream_close, on_stream_close, nullptr);
  handler.use_large_msgid = false;
  nq_hdmap_rpc_handler(hm, "test", handler);

  nq_addr_t addr = {
    "127.0.0.1", nullptr, nullptr, nullptr,
    8443
  };
  nq_clconf_t conf;
  conf.insecure = false;
  conf.handshake_idle_timeout = nq_time_sec(60);
  conf.handshake_timeout = nq_time_sec(120);

  for (int i = 0; i < N_CLIENT; i++) {
    //reinitialize closure, with giving client index as arg
    nq_closure_init(conf.on_open, on_conn_open, on_conn_open, (void *)(intptr_t)i);
    nq_closure_init(conf.on_close, on_conn_close, on_conn_close, (void *)(intptr_t)i);
    g_cs[i] = nq_client_connect(cl, &addr, &conf);
    if (!nq_conn_is_valid(g_cs[i])) {
      printf("fail to create connection\n");
      return -1;
    }
    g_rpcs[i] = nq_conn_rpc(g_cs[i], "test");
    g_ctxs[i].seed = 0;
    g_ctxs[i].last_recv = 0;
    nq_closure_init(g_reps[i], on_rpc_reply, on_rpc_reply, g_ctxs + i);
  }

  nq_time_t start = nq_time_now();
  while (g_alive) {
    //nq_time_pause(nq_time_msec(10));
    nq_client_poll(cl);
  }

  printf("process %llu requests in %lf sec\n", 
    g_idx, ((double)(nq_time_now() - start)) / (1000 * 1000 * 1000));

  nq_client_destroy(cl);

  return 0;
}
