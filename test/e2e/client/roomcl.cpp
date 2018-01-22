#include <nq.h>

#include <map>
#include <stdlib.h>
#include <inttypes.h>

#include <basis/endian.h>

#include "rpctypes.h"

using namespace nqtest;

#define N_CLIENT (10)
#define N_RECV (10)

/* static variables */
enum {
  RPC_STATE_INIT,
  RPC_STATE_BCAST,
};
struct closure_ctx {
  uint64_t id;
  uint64_t acked_max;
  uint8_t state;
};
nq_conn_t g_cs[N_CLIENT];
nq_rpc_t g_rpcs[N_CLIENT];
closure_ctx g_ctxs[N_CLIENT];
nq_closure_t g_reps[N_CLIENT];
nq::atomic<uint64_t> g_fin(0);
bool g_alive = true;


/* conn callback */
void on_conn_open(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void **) {
  if (hsev == NQ_HS_DONE) {
    TRACE("conn open for %p", arg);
    intptr_t i = (intptr_t)arg;
    nq_conn_rpc(g_cs[i], "rpc", g_ctxs + i);
  }
}
nq_time_t on_conn_close(void *arg, nq_conn_t c, nq_quic_error_t e, const char *detail, bool) {
  TRACE("conn close for %p", arg);
  return nq_time_msec(10);
}
void on_conn_finalize(void *arg, nq_conn_t, void *) {
  TRACE("conn fin for %p", arg);
  g_fin++;
  if (g_fin >= N_CLIENT) {
    g_alive = false;
  }  
}
  

/* rpc stream callback */
bool on_rpc_open(void *p, nq_rpc_t rpc, void **ctx) {
  closure_ctx *c = *(closure_ctx **)ctx;
  c->state = RPC_STATE_INIT;
  char buffer[sizeof(uint64_t)];
  nq::Endian::HostToNetbytes(c->id, buffer);
  nq_rpc_call(rpc, RpcType::BcastJoin, buffer, sizeof(buffer), g_reps[c->id - 1]);
  return true;
}
void on_rpc_close(void *p, nq_rpc_t rpc) {
}
void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {
}
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_error_t result, const void *data, nq_size_t len) {
  closure_ctx *c = (closure_ctx *)nq_rpc_ctx(rpc);
  TRACE("on_rpc_reply %llu %p state %u", c->id, c, c->state);
  switch (c->state) {
  case RPC_STATE_INIT: {
    c->acked_max = nq::Endian::NetbytesToHost<uint64_t>(data);
    char buffer[sizeof(uint64_t)];
    nq::Endian::HostToNetbytes(c->id, buffer);
    nq_rpc_call(rpc, RpcType::Bcast, buffer, sizeof(buffer), g_reps[c->id - 1]);
    c->state = RPC_STATE_BCAST;
  } break;
  case RPC_STATE_BCAST: {
    //ignore
  } break;
  }
}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {
  closure_ctx *c = (closure_ctx *)nq_rpc_ctx(rpc);
  TRACE("rpc notif: %llu %u", c->id, type);
  switch (type) {
    case RpcType::BcastNotify:
      {
        const char *tmp = (const char *)data;
        uint64_t id = nq::Endian::NetbytesToHost<uint64_t>(tmp);
        uint64_t acked_max = nq::Endian::NetbytesToHost<uint64_t>(tmp + sizeof(uint64_t));
        ASSERT(id == c->id);
        if (c->acked_max >= acked_max) {
          TRACE("%llu: old acked %llu ignored (now: %llu)\n", c->id, c->acked_max, acked_max);
          return;
        }
        ASSERT((c->acked_max + 1) == acked_max);
        TRACE("%llu: acked %llu => %llu\n", c->id, c->acked_max, acked_max);
        c->acked_max = acked_max;
        char buffer[sizeof(uint64_t) + sizeof(uint64_t) + 1];
        nq::Endian::HostToNetbytes(c->id, buffer);
        nq::Endian::HostToNetbytes(c->acked_max, buffer + sizeof(uint64_t));
        if (c->acked_max == N_RECV) {
          buffer[sizeof(buffer) - 1] = 1;
          nq_conn_close(nq_rpc_conn(rpc));
        } else {
          buffer[sizeof(buffer) - 1] = 1;//rand() % 2;
          nq_rpc_call(rpc, RpcType::BcastReply, buffer, sizeof(buffer), g_reps[c->id - 1]);
          if (buffer[sizeof(buffer) - 1] == 1) {
            nq_conn_reset(nq_rpc_conn(rpc));
          }
        }
      }
      break;
    default:
      ASSERT(false);
      break;
  }
}
void on_rpc_validate(void *p, nq_rpc_t rpc, const char *error) {
  if (error != nullptr) {
    printf("fail to create connection %s\n", error);
  }
}


/* main */
int main(int argc, char *argv[]){
  nq_client_t cl = nq_client_create(N_CLIENT, N_CLIENT * 4); //N_CLIENT connection client

  srand(nq_time_unix());

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

  nq_closure_t on_validate;
  nq_closure_init(on_validate, on_rpc_validate, on_rpc_validate, nullptr);
  for (int i = 0; i < N_CLIENT; i++) {
    //reinitialize closure, with giving client index as arg
    nq_closure_init(conf.on_open, on_client_conn_open, on_conn_open, (void *)(intptr_t)i);
    nq_closure_init(conf.on_close, on_client_conn_close, on_conn_close, (void *)(intptr_t)i);
    nq_closure_init(conf.on_finalize, on_client_conn_finalize, on_conn_finalize, (void *)(intptr_t)i);
    g_cs[i] = nq_client_connect(cl, &addr, &conf);
    if (!nq_conn_is_valid(g_cs[i], on_validate)) {
      return -1;
    }
    g_ctxs[i].id = (i + 1);
    g_ctxs[i].acked_max = 0;
    nq_closure_init(g_reps[i], on_rpc_reply, on_rpc_reply, g_ctxs + i);
  }

  nq_time_t start = nq_time_now();
  while (g_alive) {
    //nq_time_pause(nq_time_msec(10));
    nq_client_poll(cl);
  }

  printf("finish %" PRId64 " clents in %lf sec\n", 
    g_fin.load(), ((double)(nq_time_now() - start)) / (1000 * 1000 * 1000));

  nq_client_destroy(cl);

  return 0;
}
