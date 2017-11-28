#include "common.h"

static const int kThreads = 1;  //4 thread server
static const char NotifySuccess[] = "notify success";

using namespace nqtest;

/* conn callbacks */
struct conn_context {
  int count;
};
void on_conn_open(void *, nq_conn_t, nq_handshake_event_t hsev, void **ppctx) {
  fprintf(stderr, "on_conn_open event:%d\n", hsev);
  if (hsev != NQ_HS_DONE) {
    return;
  }
  auto ctx = (conn_context *)malloc(sizeof(conn_context));
  ctx->count = 3;
  *ppctx = ctx;
}
bool g_reject = true;
void on_conn_open_reject(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void **ppctx) {
  fprintf(stderr, "on_conn_open event:%d\n", hsev);
  if (hsev != NQ_HS_DONE) {
    return;
  }
  if (g_reject) {
    g_reject = true;
    nq_conn_close(c);
    return;
  }
  on_conn_open(arg, c, hsev, ppctx);
}
void on_conn_close(void *, nq_conn_t c, nq_result_t, const char *detail, bool) {
  fprintf(stderr, "on_conn_close reason:%s\n", detail);
  free(nq_conn_ctx(c));
}


/* alarm callback */
struct alarm_context {
  nq_time_t invoke_time, start;
  nq_rpc_t rpc;
  nq_msgid_t msgid;
  nq_alarm_t alarm;
};
void on_alarm(void *p, nq_time_t *pnext) {
  auto ac = (alarm_context *)p;
  if (*pnext != ac->invoke_time) {
    nq_rpc_reply(ac->rpc, RpcError::InternalError, ac->msgid, "", 0);
    return;
  }
  TRACE("on_alarm for %u replies with %llu delay", ac->msgid, nq_time_now() - ac->start);
  nq_rpc_reply(ac->rpc, RpcError::None, ac->msgid, "", 0);
  free(ac);
  //no write for *pnext will cause deletion of ac->alarm
}


/* rpc callbacks */
bool on_rpc_open(void *p, nq_rpc_t rpc, void **) {
  return true;
}
bool on_rpc_open_reject(void *p, nq_rpc_t rpc, void **ppctx) {
  auto c = nq_rpc_conn(rpc);
  conn_context *ctx = reinterpret_cast<conn_context *>(nq_conn_ctx(c));
  if (ctx == nullptr) {
    ASSERT(false);
    return false;
  }
  ctx->count--;
  if (ctx->count <= 0) {
    return on_rpc_open(p, rpc, ppctx);
  }
  return false;
}
void on_rpc_close(void *p, nq_rpc_t rpc) {
}
void on_rpc_request(void *p, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t len) {
  TRACE("rpc req: %u", type);
  switch (type) {
    case RpcType::Ping:
      ASSERT(len == sizeof(nq_time_t));
      //auto peer_ts = nq::Endian::NetbytesToHost<uint64_t>((const char *)data);
      //fprintf(stderr, "peer_ts => %llu\n", peer_ts);
      nq_rpc_reply(rpc, RpcError::None, msgid, data, len);
      break;
    case RpcType::Raise:
      nq_rpc_reply(rpc, nq::Endian::NetbytesToHost<int32_t>(data), msgid, 
        static_cast<const char *>(data) + sizeof(int32_t), len - sizeof(int32_t));
      break;
    case RpcType::NotifyText:
      nq_rpc_notify(rpc, RpcType::TextNotification, data, len);
      nq_rpc_reply(rpc, RpcError::None, msgid, NotifySuccess, sizeof(NotifySuccess) - 1);
      break;
    case RpcType::ServerStream:
      {
        auto s = MakeString(data, len);
        auto idx = s.find('|');
        if (idx == std::string::npos) {
          s = std::string("parse fails:") + s;
          nq_rpc_reply(rpc, RpcError::Parse, msgid, s.c_str(), s.length());
          return;
        }
        auto name = s.substr(0, idx);
        auto msg = "from server:" + s.substr(idx + 1);
        auto c = nq_rpc_conn(rpc);
        auto rpc2 = nq_conn_rpc(c, name.c_str());
        if (!nq_rpc_is_valid(rpc2)) {
          nq_rpc_reply(rpc, RpcError::NoSuchStream, msgid, s.c_str(), s.length());
          return;          
        }
        RPC(rpc2, RpcType::ServerRequest, msg.c_str(), msg.length(), ([rpc2, msg](
          nq_rpc_t rpc3, nq_result_t r, const void *data, nq_size_t dlen) {
          ASSERT(r >= 0 && nq_rpc_sid(rpc2) == nq_rpc_sid(rpc3) && MakeString(data, dlen) == ("from client:" + msg));
        }));
        nq_rpc_reply(rpc, RpcError::None, msgid, "", 0);
      }
      break;
    case RpcType::Sleep:
      {
        nq_time_t duration = nq::Endian::NetbytesToHost<nq_time_t>(data);
        nq_closure_t cl;
        alarm_context *ac = (alarm_context *)malloc(sizeof(alarm_context));
        nq_closure_init(cl, on_alarm, on_alarm, ac);
        ac->start = nq_time_now();
        ac->invoke_time = ac->start + duration;
        ac->rpc = rpc;
        ac->msgid = msgid;
        ac->alarm = nq_rpc_alarm(rpc);
        nq_alarm_set(ac->alarm, ac->invoke_time, cl);
      }
      break;
    case RpcType::Close:
      {
        nq_conn_t c = nq_rpc_conn(rpc);
        nq_rpc_reply(rpc, RpcError::None, msgid, "", 0);
        nq_conn_close(c);
      }
      break;
  }
}
void on_rpc_reply(void *p, nq_rpc_t rpc, nq_result_t result, const void *data, nq_size_t len) {

}
void on_rpc_notify(void *p, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t len) {

}


/* stream callbacks */
struct stream_context {
  nq_size_t send_buf_len;
  char *send_buf;
};
bool on_stream_open(void *p, nq_stream_t s, void **ppctx) {
  stream_context *sc = reinterpret_cast<stream_context *>(malloc(sizeof(stream_context)));
  if (sc == nullptr) { 
    return false; 
  }
  sc->send_buf_len = 256;
  sc->send_buf = reinterpret_cast<char *>(malloc(sc->send_buf_len));
  if (sc->send_buf == nullptr) { 
    free(sc);
    return false; 
  }
  *ppctx = sc;
  return true;
}
bool on_stream_open_reject(void *p, nq_stream_t s, void **ppctx) {
  auto c = nq_stream_conn(s);
  conn_context *ctx = reinterpret_cast<conn_context *>(nq_conn_ctx(c));
  if (ctx == nullptr) {
    ASSERT(false);
    return false;
  }
  ctx->count--;
  if (ctx->count <= 0) {
    return on_stream_open(p, s, ppctx);
  }
  return false;
}
void on_stream_close(void *p, nq_stream_t s) {
  stream_context *sc = reinterpret_cast<stream_context *>(nq_stream_ctx(s));
  free(sc->send_buf);
  free(sc);
}
void on_stream_record(void *p, nq_stream_t s, const void *data, nq_size_t len) {
  auto tmp = MakeString(data, len);
  auto tmp2 = tmp + tmp;
  nq_stream_send(s, tmp2.c_str(), tmp2.length());
}
nq_size_t stream_writer(void *arg, nq_stream_t s, const void *data, nq_size_t len, void **pbuf) {
  stream_context *c = reinterpret_cast<stream_context *>(nq_stream_ctx(s));
  //append \n as delimiter
  auto blen = c->send_buf_len;
  while (c->send_buf_len < (len + 1)) {
    c->send_buf_len <<= 1;
  }
  if (blen != c->send_buf_len) {
    c->send_buf = reinterpret_cast<char *>(realloc(c->send_buf, c->send_buf_len));
    ASSERT(c->send_buf);
  }
  memcpy(c->send_buf, data, len);
  c->send_buf[len] = '\n';
  *pbuf = c->send_buf;
  return len + 1;
}
void *stream_reader(void *arg, const char *data, nq_size_t dlen, int *p_reclen) {
  //auto c = (Conn *)arg;
  //use text protocol (use \n as delimiter)
  auto idx = MakeString(data, dlen).find('\n');
  if (idx != std::string::npos) {
    *p_reclen = idx;
    return const_cast<void *>(reinterpret_cast<const void *>(data));
  }
  return nullptr;
}


/* setup server */
struct server_config {
  const char *quic_secret;
  nq_closure_t on_server_conn_open;
  nq_closure_t on_stream_open, on_raw_stream_open;
  nq_closure_t on_rpc_open;
};
#define CONFIG_CB(conf, name, default_value, dest) { \
  if (conf != nullptr && nq_closure_is_empty(conf->name)) { \
    dest = conf->name; \
  } else { \
    nq_closure_init(dest, name, default_value, nullptr); \
  } \
}
#define CONFIG_VAL(conf, name, default_value, dest) {\
  if (conf != nullptr && conf->name != nullptr) { \
    dest = conf->name; \
  } else { \
    dest = default_value; \
  } \
}
void setup_server(nq_server_t sv, int port, server_config *svconfig) {
  nq_addr_t addr = {
    "0.0.0.0", 
    "./test/e2e/server/certs/leaf_cert.pem", 
    "./test/e2e/server/certs/leaf_cert.pkcs8", 
    nullptr,
    port
  };

  nq_svconf_t conf;
  CONFIG_VAL(svconfig, quic_secret, "e336e27898ff1e17ac79e82fa0084999", conf.quic_secret);
  conf.quic_cert_cache_size = 0;
  conf.accept_per_loop = 1;
  conf.handshake_timeout = nq_time_sec(120);
  conf.idle_timeout = nq_time_sec(60);
  CONFIG_CB(svconfig, on_server_conn_open, on_conn_open, conf.on_open);
  nq_closure_init(conf.on_close, on_server_conn_close, on_conn_close, nullptr);

  nq_hdmap_t hm = nq_server_listen(sv, &addr, &conf);

  nq_rpc_handler_t rh;
  nq_closure_init(rh.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(rh.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  CONFIG_CB(svconfig, on_rpc_open, on_rpc_open, rh.on_rpc_open);
  nq_closure_init(rh.on_rpc_close, on_rpc_close, on_rpc_close, nullptr);
  nq_hdmap_rpc_handler(hm, "rpc", rh);

  nq_stream_handler_t rsh;
  CONFIG_CB(svconfig, on_stream_open, on_stream_open, rsh.on_stream_open);
  nq_closure_init(rsh.on_stream_close, on_stream_close, on_stream_close, nullptr);
  nq_closure_init(rsh.on_stream_record, on_stream_record, on_stream_record, nullptr);
  nq_closure_init(rsh.stream_reader, stream_reader, stream_reader, nullptr);
  nq_closure_init(rsh.stream_writer, stream_writer, stream_writer, nullptr);
  nq_hdmap_stream_handler(hm, "rst", rsh);

  nq_stream_handler_t ssh;
  CONFIG_CB(svconfig, on_stream_open, on_stream_open, ssh.on_stream_open);
  nq_closure_init(ssh.on_stream_close, on_stream_close, on_stream_close, nullptr);
  nq_closure_init(ssh.on_stream_record, on_stream_record, on_stream_record, nullptr);
  ssh.stream_reader = nq_closure_empty();
  ssh.stream_writer = nq_closure_empty();
  nq_hdmap_stream_handler(hm, "sst", ssh);
}


/* main */
int main(int argc, char *argv[]){
  nq_server_t sv = nq_server_create(kThreads);

  setup_server(sv, 8443, nullptr);
  server_config scf = {
    .quic_secret = "8b090e1a7f6b40a818f3563160bd43e1"
  };
  int reject_counter[4] = {
    1, 2, 1, 1,
  };
  nq_closure_init(scf.on_server_conn_open, on_server_conn_open, on_conn_open_reject, reject_counter + 0);
  nq_closure_init(scf.on_rpc_open, on_rpc_open, on_rpc_open_reject, reject_counter + 1);
  nq_closure_init(scf.on_stream_open, on_stream_open, on_stream_open_reject, reject_counter + 2);
  nq_closure_init(scf.on_raw_stream_open, on_stream_open, on_stream_open_reject, reject_counter + 3);
  setup_server(sv, 18443, &scf);

  nq_server_start(sv, false);

  nq_time_sleep(nq_time_sec(5));
  nq_server_join(sv);

  return 0;
}
