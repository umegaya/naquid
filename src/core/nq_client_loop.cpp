#include "core/nq_client_loop.h"

#include "core/nq_client.h"

namespace net {
struct NqDnsQuery : public NqAsyncResolver::Query {
  static bool ConvertToIpAddress(struct hostent *entries, QuicIpAddress &ip) {
    return ip.FromPackedString(entries->h_addr_list[0], nq::Syscall::GetIpAddrLen(entries->h_addrtype));
  }
  static bool ConvertToSocketAddress(struct hostent *entries, int port, NqQuicSocketAddress &address) {
    QuicIpAddress ip;
    if (!ConvertToIpAddress(entries, ip)) {
      return false;
    }
    address = NqQuicSocketAddress(ip, port);
    return true;
  }
};

struct NqDnsQueryForClient : public NqDnsQuery {
  NqClientLoop *loop_;
  NqClientConfig config_;
  int port_;

  NqDnsQueryForClient(const nq_clconf_t &conf) : NqDnsQuery(), config_(conf), port_(0) {}
  void OnComplete(int status, int timeouts, struct hostent *entries) override {
    if (ARES_SUCCESS == status) {
      NqQuicSocketAddress server_address;
      NqQuicServerId server_id = NqQuicServerId(host_, port_);
      if (ConvertToSocketAddress(entries, port_, server_address)) {
        loop_->Create(host_, server_id, server_address, config_);
        return;
      } else {
        status = ARES_ENOTFOUND;
      }
    } 
    nq_error_detail_t detail = {
      .code = status,
      .msg = ares_strerror(status),
    };
    //call on close with empty nq_conn_t. 
    nq_conn_t empty = {{{0}}, nullptr};
    nq_closure_call(config_.client().on_close, empty, NQ_ERESOLVE, &detail, false);
  }
};

struct NqDnsQueryForClosure : public NqDnsQuery {
  nq_on_resolve_host_t cb_;
  void OnComplete(int status, int timeouts, struct hostent *entries) override {
    if (ARES_SUCCESS == status) {
      nq_closure_call(cb_, NQ_OK, nullptr, 
        entries->h_addr_list[0], nq::Syscall::GetIpAddrLen(entries->h_addrtype));
    } else {
      nq_error_detail_t detail = {
        .code = status,
        .msg = ares_strerror(status),
      };
      nq_closure_call(cb_, NQ_ERESOLVE, &detail, nullptr, 0);
    }
  }  
};

bool NqClientLoop::Resolve(int family_pref, const std::string &host, int port, const nq_clconf_t *conf) {
  auto q = new NqDnsQueryForClient(*conf);
  q->host_ = host;
  q->loop_ = this;
  q->family_ = family_pref;
  q->port_ = port;
  async_resolver_.StartResolve(q);  
  return true;
}
bool NqClientLoop::Resolve(int family_pref, const std::string &host, nq_on_resolve_host_t cb) {
  auto q = new NqDnsQueryForClosure;
  q->host_ = host;
  q->family_ = family_pref;
  q->cb_ = cb;
  async_resolver_.StartResolve(q);  
  return true;
}
NqClient *NqClientLoop::Create(const std::string &host, 
                               const NqQuicServerId server_id,
                               const NqQuicSocketAddress server_address,
                               NqClientConfig &config) {

  auto supported_versions = AllSupportedVersions();//versions_.GetSupportedVersions();
  auto c = new(this) NqClient(
    server_address,
    *this,
    server_id, 
    config
  );
  if (!c->Initialize()) {
    delete c;
    return nullptr;
  }
  if (config.client().track_reachability) {
    if (!c->TrackReachability(host)) {
      delete c;
      return nullptr;
    }
  }
  c->StartConnect();
  c->InitSerial(); //insert c into client_map_
  return c;
}
}
