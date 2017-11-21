#include "common.h"

static const int kThreads = 1;  //4 thread server
static const char NotifySuccess[] = "notify success";

using namespace nqtest;

/* conn callback */
bool on_conn_open(void *, nq_conn_t, nq_handshake_event_t hsev, void *) {
  fprintf(stderr, "on_conn_open event:%d\n", hsev);
  return true;
}
nq_time_t on_conn_close(void *, nq_conn_t, nq_result_t, const char *detail, bool) {
  fprintf(stderr, "on_conn_close reason:%s\n", detail);
  return 0;
}



/* rpc stream callback */
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
  }
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
  conf.quic_secret = "e336e27898ff1e17ac79e82fa0084999";
  conf.quic_cert_cache_size = 0;
  conf.accept_per_loop = 1;
  conf.handshake_timeout = nq_time_sec(120);
  conf.idle_timeout = nq_time_sec(60);
  nq_closure_init(conf.on_open, on_conn_open, on_conn_open, nullptr);
  nq_closure_init(conf.on_close, on_conn_close, on_conn_close, nullptr);
  hm = nq_server_listen(sv, &addr, &conf);

  nq_rpc_handler_t handler;
  nq_closure_init(handler.on_rpc_request, on_rpc_request, on_rpc_request, nullptr);
  nq_closure_init(handler.on_rpc_notify, on_rpc_notify, on_rpc_notify, nullptr);
  nq_closure_init(handler.on_rpc_open, on_rpc_open, on_rpc_open, nullptr);
  nq_closure_init(handler.on_rpc_close, on_rpc_close, on_rpc_close, nullptr);
  nq_hdmap_rpc_handler(hm, "rpc", handler);

  nq_server_start(sv, false);

  nq_time_sleep(nq_time_sec(5));
  nq_server_join(sv);

  return 0;
}
