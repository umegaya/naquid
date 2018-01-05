#include <nq.h>

#include <map>
#include <inttypes.h>

#include "basis/endian.h"

#define N_CLIENT (100)
#define N_SEND (5000)

//#define STORE_DETAIL
#if defined(STORE_DETAIL)
extern bool is_conn_opened(uint64_t cid) {
  return true;
}
extern bool is_packet_received(uint64_t cid) {
  return true;
}
#endif



/* static variables */
struct closure_ctx {
  uint64_t seed;
  uint64_t last_recv;
  uint32_t index;
#if defined(STORE_DETAIL)
  int fd;
  uint64_t cid;
#endif
};
nq_conn_t g_cs[N_CLIENT];
nq_rpc_t g_rpcs[N_CLIENT];
closure_ctx g_ctxs[N_CLIENT];
nq_closure_t g_reps[N_CLIENT];




/* helper */
static void send_rpc(nq_rpc_t rpc, nq_closure_t reply_cb, int index) {
  auto v = (closure_ctx *)reply_cb.arg;
  v->seed++;
  //fprintf(stderr, "rpc %d, seq = %llx\n", index, v->seed);
#if defined(STORE_DETAIL)
  char buffer[12];
  nq::Endian::HostToNetbytes(index, buffer);
  nq::Endian::HostToNetbytes(v->seed, buffer + 4);
  nq_rpc_call(rpc, 1, (const void *)buffer, sizeof(buffer), reply_cb);
#else
  uint64_t seq = nq::Endian::HostToNet(v->seed);
  nq_rpc_call(rpc, 1, (const void *)&seq, sizeof(seq), reply_cb);
#endif
}



/* conn callback */
void on_conn_open(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void **) {
  intptr_t idx = (intptr_t)arg;
#if defined(STORE_DETAIL)
  g_ctxs[idx].fd = nq_conn_fd(c);
  g_ctxs[idx].cid = nq_conn_cid(c);
#endif
  TRACE("on_conn_open event:%ld %d\n", idx, hsev);
}
nq_time_t on_conn_close(void *arg, nq_conn_t c, nq_quic_error_t e, const char *detail, bool) {
  intptr_t idx = (intptr_t)arg;
  fprintf(stderr, "on_conn_close: reason:%ld %s %s\n", idx, detail, nq_quic_error_str(e));
  return nq_time_sec(2);
}



/* rpc stream callback */
bool on_rpc_open(void *p, nq_rpc_t rpc, void **) {
  auto v = (closure_ctx *)nq_rpc_ctx(rpc);
  g_rpcs[v->index] = rpc;
  auto rep = g_reps[v->index];
  for (int i = 0; i < N_SEND; i++) {
    send_rpc(rpc, rep, v->index);
  }
  return true;
}
void on_rpc_close(void *p, nq_rpc_t rpc) {
  auto v = (closure_ctx *)nq_rpc_ctx(rpc);
  fprintf(stderr, "on_rpc_close: idx:%d\n", v->index);
  return;
}
void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {

}
static uint64_t g_idx = 0;
static bool g_alive = true;
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_error_t result, const void *data, nq_size_t len) {
  ASSERT(result >= 0);
#if defined(STORE_DETAIL)
  auto v = (closure_ctx *)p;
  auto recv_seq = nq::Endian::NetbytesToHost<uint64_t>((const char *)data);
  if (recv_seq != (v->last_recv + 1)) {
    fprintf(stderr, "rpc %llx, seq leaps: %llu => %llu\n", rpc.s, v->last_recv, recv_seq);
    exit(1);
  } else {
    v->last_recv = recv_seq;
  }
#endif

  g_idx++;
  if (g_idx >= (N_CLIENT * N_SEND)) {
    g_alive = false;
  }

#if defined(STORE_DETAIL)
  if ((g_idx % 1000) == 0) {
    fprintf(stderr, "%llu.", g_idx / 1000);
  }
#endif
  //printf("req %d: sent_ts: %llu, latency %lf sec\n", ++idx, sent_ts, ((double)(nq_time_now() - sent_ts) / (1000 * 1000 * 1000)));
}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {

}



/* main */
int main(int argc, char *argv[]){
  nq_client_t cl = nq_client_create(N_CLIENT, N_CLIENT * 4); //N_CLIENT connection client

  nq_hdmap_t hm;
  hm = nq_client_hdmap(cl);
  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_rpc_open, on_rpc_open, on_rpc_open, nullptr);
  nq_closure_init(handler.on_rpc_close, on_rpc_close, on_rpc_close, nullptr);
  handler.use_large_msgid = false;
  handler.timeout = nq_time_sec(60);
  nq_hdmap_rpc_handler(hm, "rpc", handler);

  nq_addr_t addr = {
    "127.0.0.1", nullptr, nullptr, nullptr,
    8443
  };
  nq_clconf_t conf;
  conf.insecure = false;
  conf.idle_timeout = nq_time_sec(60);
  conf.handshake_timeout = nq_time_sec(120);

  const char *invalid_reason;
  for (int i = 0; i < N_CLIENT; i++) {
    //reinitialize closure, with giving client index as arg
    nq_closure_init(conf.on_open, on_client_conn_open, on_conn_open, (void *)(intptr_t)i);
    nq_closure_init(conf.on_close, on_client_conn_close, on_conn_close, (void *)(intptr_t)i);
    g_cs[i] = nq_client_connect(cl, &addr, &conf);
    if (!nq_conn_is_valid(g_cs[i], &invalid_reason)) {
      printf("fail to create connection %s\n", invalid_reason);
      return -1;
    }
    g_ctxs[i].seed = 0;
    g_ctxs[i].last_recv = 0;
    g_ctxs[i].index = i;
    nq_conn_rpc(g_cs[i], "rpc", g_ctxs + i);
    nq_closure_init(g_reps[i], on_rpc_reply, on_rpc_reply, g_ctxs + i);
  }

  nq_time_t start = nq_time_now();
  while (g_alive) {
    //nq_time_pause(nq_time_msec(10));
    nq_client_poll(cl);
  }

  printf("process %" PRId64 " requests in %lf sec\n", 
    g_idx, ((double)(nq_time_now() - start)) / (1000 * 1000 * 1000));

  nq_client_destroy(cl);

  return 0;
}