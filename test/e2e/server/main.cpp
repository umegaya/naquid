#include "common.h"

static const int kThreads = 1;  //4 thread server
static const char NotifySuccess[] = "notify success";

using namespace nqtest;

/* conn callbacks */
bool on_conn_open(void *, nq_conn_t, nq_handshake_event_t hsev, void *) {
  fprintf(stderr, "on_conn_open event:%d\n", hsev);
  return true;
}
nq_time_t on_conn_close(void *, nq_conn_t, nq_result_t, const char *detail, bool) {
  fprintf(stderr, "on_conn_close reason:%s\n", detail);
  return 0;
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


/* setup stream */
void setup_streams(nq_hdmap_t hm) {
  nq_rpc_handler_t rh;
  nq_closure_init(rh.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(rh.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(rh.on_rpc_open, on_rpc_open, on_rpc_open, nullptr);
  nq_closure_init(rh.on_rpc_close, on_rpc_close, on_rpc_close, nullptr);
  nq_hdmap_rpc_handler(hm, "rpc", rh);

  nq_stream_handler_t rsh;
  nq_closure_init(rsh.on_stream_open, on_stream_open, on_stream_open, nullptr);
  nq_closure_init(rsh.on_stream_close, on_stream_close, on_stream_close, nullptr);
  nq_closure_init(rsh.on_stream_record, on_stream_record, on_stream_record, nullptr);
  nq_closure_init(rsh.stream_reader, stream_reader, stream_reader, nullptr);
  nq_closure_init(rsh.stream_writer, stream_writer, stream_writer, nullptr);
  nq_hdmap_stream_handler(hm, "rst", rsh);

  nq_stream_handler_t ssh;
  nq_closure_init(ssh.on_stream_open, on_stream_open, on_stream_open, nullptr);
  nq_closure_init(ssh.on_stream_close, on_stream_close, on_stream_close, nullptr);
  nq_closure_init(ssh.on_stream_record, on_stream_record, on_stream_record, nullptr);
  ssh.stream_reader = nq_closure_empty();
  ssh.stream_writer = nq_closure_empty();
  nq_hdmap_stream_handler(hm, "sst", ssh);

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
  conf.quic_secret = "e336e27898ff1e17ac79e82fa0084999";
  conf.quic_cert_cache_size = 0;
  conf.accept_per_loop = 1;
  conf.handshake_timeout = nq_time_sec(120);
  conf.idle_timeout = nq_time_sec(60);
  nq_closure_init(conf.on_open, on_conn_open, on_conn_open, nullptr);
  nq_closure_init(conf.on_close, on_conn_close, on_conn_close, nullptr);
  hm = nq_server_listen(sv, &addr, &conf);

  setup_streams(hm);

  nq_server_start(sv, false);

  nq_time_sleep(nq_time_sec(5));
  nq_server_join(sv);

  return 0;
}
