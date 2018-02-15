#include <nq.h>

#include <map>
#include <stdlib.h>
#include <inttypes.h>

#include <basis/endian.h>
#include <basis/convert.h>

#include "rpctypes.h"

using namespace nqtest;

#define N_CLIENT (16)
#define N_RECV (10)

/* static variables */
enum {
  RPC_STATE_INIT,
  RPC_STATE_BCAST,
  RPC_STATE_CLOSE_BY_CLIENT,
  RPC_STATE_CLOSE_BY_SERVER,
  RPC_STATE_FINISH,
};
struct closure_ctx {
  uint64_t id;
  uint64_t acked_max;
  uint8_t state;
};
closure_ctx *g_ctxs;
nq_on_rpc_reply_t *g_reps;
nq::atomic<uint64_t> g_fin(0);
bool g_alive = true;

/* setting static variable */
int g_disconnect_type = 1; //0 client/server random, 1 client close, 2 server close
int g_client_num = N_CLIENT;
int g_recv_num = N_RECV;


/* conn callback */
void on_conn_open(void *arg, nq_conn_t c, void **) {
  TRACE("on_conn_open:%p %llx", arg, c.s.data[0]);
  intptr_t i = (intptr_t)arg;
  nq_conn_rpc(c, "rpc", g_ctxs + i);
}
nq_time_t on_conn_close(void *arg, nq_conn_t c, nq_error_t e, const nq_error_detail_t *detail, bool remote) {
  TRACE("on_conn_close:%p %llx, %d(from %s) reason:%s(%d)\n", arg, c.s.data[0], e, remote ? "remote": "local", detail->msg, detail->code);
  return nq_time_msec(10);
}
void on_conn_finalize(void *arg, nq_conn_t c, void *) {
  TRACE("on_conn_finalize:%p %llx", arg, c.s.data[0]);
  g_fin++;
  if (g_fin >= g_client_num) {
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
  if (result < 0) {
    ASSERT(result == NQ_EGOAWAY);
    return;
  }
  closure_ctx *c = (closure_ctx *)nq_rpc_ctx(rpc);
  TRACE("on_rpc_reply %llu %p state %u %llu", c->id, c, c->state, c->acked_max);
  switch (c->state) {
  case RPC_STATE_INIT: {
    uint64_t acked_max = nq::Endian::NetbytesToHost<uint64_t>(data);
    ASSERT(c->acked_max <= acked_max);
    c->acked_max = acked_max;
    char buffer[sizeof(uint64_t)];
    nq::Endian::HostToNetbytes(c->id, buffer);
    nq_rpc_notify(rpc, RpcType::Bcast, buffer, sizeof(buffer));
    c->state = RPC_STATE_BCAST;
  } break;
  case RPC_STATE_BCAST: {
    ASSERT(false);
  } break;
  case RPC_STATE_CLOSE_BY_CLIENT: {
    TRACE("on_rpc_reply %llu close conn", c->id);
    nq_conn_reset(nq_rpc_conn(rpc));
  } break;
  case RPC_STATE_CLOSE_BY_SERVER: {
    //may not come
  } break;
  case RPC_STATE_FINISH: {
    nq_conn_close(nq_rpc_conn(rpc));
  } break;
  }
}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {
  closure_ctx *c = (closure_ctx *)nq_rpc_ctx(rpc);
  switch (type) {
    case RpcType::BcastNotify:
      {
        const char *tmp = (const char *)data;
        uint64_t id = nq::Endian::NetbytesToHost<uint64_t>(tmp);
        uint64_t acked_max = nq::Endian::NetbytesToHost<uint64_t>(tmp + sizeof(uint64_t));
        TRACE("rpc notif bcast(%p): %llu %llu %llu %llu", c, c->id, id, acked_max, c->acked_max);
        ASSERT(id == c->id);
        if (c->acked_max >= acked_max) {
          //TRACE("%llu: old acked %llu ignored (now: %llu)\n", c->id, c->acked_max, acked_max);
          return;
        }
        ASSERT((c->acked_max + 1) == acked_max);
        if (c->state != RPC_STATE_BCAST) {
          TRACE("%llu: already reply or bcast not finished yet (%u)", c->id, c->state);
          //should not come RPC_STATE_INIT state because should not join room yet.
          ASSERT(c->state == RPC_STATE_CLOSE_BY_SERVER || c->state == RPC_STATE_CLOSE_BY_CLIENT || c->state == RPC_STATE_FINISH);
          return;
        }
        TRACE("%llu(%d/%p): acked %llu => %llu (%u)\n", c->id, nq_conn_fd(nq_rpc_conn(rpc)), c, c->acked_max, acked_max, c->state);
        c->acked_max = acked_max;
        char buffer[sizeof(uint64_t) + sizeof(uint64_t) + 1];
        nq::Endian::HostToNetbytes(c->id, buffer);
        nq::Endian::HostToNetbytes(c->acked_max, buffer + sizeof(uint64_t));
        if (c->acked_max == g_recv_num) {
          buffer[sizeof(buffer) - 1] = 1;
          c->state = RPC_STATE_FINISH;
          nq_rpc_call(rpc, RpcType::BcastReply, buffer, sizeof(buffer), g_reps[c->id - 1]);
        } else {
          char dc_type = 0;
          switch (g_disconnect_type) {
            case 0: 
              dc_type = rand() % 2;
              break;
            case 1:
              dc_type = 1;
              break;
            case 2:
              dc_type = 0;
              break; 
          }
          buffer[sizeof(buffer) - 1] = dc_type;
          nq_rpc_call(rpc, RpcType::BcastReply, buffer, sizeof(buffer), g_reps[c->id - 1]);
          if (buffer[sizeof(buffer) - 1] == 1) {
            TRACE("%llu: send close by client (ack:%u)", c->id, c->acked_max);
            c->state = RPC_STATE_CLOSE_BY_CLIENT;
          } else {
            c->state = RPC_STATE_CLOSE_BY_SERVER;            
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
  if (argc > 1) {
    g_disconnect_type = nq::convert::Do(argv[1], g_disconnect_type);
    if (argc > 2) {
      g_client_num = nq::convert::Do(argv[2], g_client_num);
      if (argc > 3) {
        g_recv_num = nq::convert::Do(argv[3], g_recv_num);
      }
    }
  }

  g_ctxs = new closure_ctx[g_client_num];
  g_reps = new nq_on_rpc_reply_t[g_client_num];

  nq_client_t cl = nq_client_create(g_client_num, g_client_num * 4, nullptr); //g_client_num connection client

  srand(nq_time_unix());

  nq_hdmap_t hm;
  hm = nq_client_hdmap(cl);
  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_rpc_open, on_rpc_open, nullptr);
  nq_closure_init(handler.on_rpc_close, on_rpc_close, nullptr);
  handler.use_large_msgid = false;
  handler.timeout = nq_time_sec(60);
  nq_hdmap_rpc_handler(hm, "rpc", handler);

  nq_addr_t addr = {
    "127.0.0.1", nullptr, nullptr, nullptr,
    8443
  };
  nq_clconf_t conf;
  conf.insecure = false;
  conf.track_reachability = false;
  conf.idle_timeout = nq_time_sec(60);
  conf.handshake_timeout = nq_time_sec(120);

  for (int i = 0; i < g_client_num; i++) {
    //reinitialize closure, with giving client index as arg
    nq_closure_init(conf.on_open, on_conn_open, (void *)(intptr_t)i);
    nq_closure_init(conf.on_close, on_conn_close, (void *)(intptr_t)i);
    nq_closure_init(conf.on_finalize, on_conn_finalize, (void *)(intptr_t)i);
    if (!nq_client_connect(cl, &addr, &conf)) {
      return -1;
    }
    g_ctxs[i].id = (i + 1);
    g_ctxs[i].acked_max = 0;
    nq_closure_init(g_reps[i], on_rpc_reply, g_ctxs + i);
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
