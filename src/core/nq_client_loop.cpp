#include "core/nq_client_loop.h"

#include "net/base/sockaddr_storage.h"

#include "core/nq_client.h"

namespace net {
struct NqDnsQuery : public NqAsyncResolver::Query {
  static bool ConvertToIpAddress(struct hostent *entries, QuicIpAddress &ip) {
    return ip.FromPackedString(entries->h_addr_list[0], nq::Syscall::GetIpAddrLen(entries->h_addrtype));
  }
  static bool ConvertToSocketAddress(struct hostent *entries, int port, QuicSocketAddress &address) {
    QuicIpAddress ip;
    if (!ConvertToIpAddress(entries, ip)) {
      return false;
    }
    address = QuicSocketAddress(ip, port);
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
      QuicSocketAddress server_address;
      QuicServerId server_id = QuicServerId(host_, port_, PRIVACY_MODE_ENABLED);
      if (ConvertToSocketAddress(entries, port_, server_address)) {
        loop_->Create(host_, server_id, server_address, config_);
        return;
      } else {
        status = ARES_ENOTFOUND;
      }
    } 
    nq_error_detail_t detail = { status, ares_strerror(status) };
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
	  nq_error_detail_t detail = { status, ares_strerror(status) };
      nq_closure_call(cb_, NQ_ERESOLVE, &detail, nullptr, 0);
    }
  }  
};


nq::IdFactory<uint32_t> NqClientLoop::client_worker_index_factory_;

bool NqClientLoop::InitResolver(const nq_dns_conf_t *dns_conf) {
  NqAsyncResolver::Config c;
  c.SetLookup(true, true);
  if (dns_conf != nullptr) {
    if (dns_conf->query_timeout > 0) {
      c.SetTimeout(dns_conf->query_timeout);
    }
    if (dns_conf->dns_hosts != nullptr && dns_conf->n_dns_hosts > 0) {
      for (int i = 0; i < dns_conf->n_dns_hosts; i++) {
        auto h = dns_conf->dns_hosts + i;
        if (!c.SetServerHostPort(h->addr, h->port)) {
          return false;
        }
      }
    } else {
      c.SetServerHostPort(DEFAULT_DNS);
    }
    if (dns_conf->use_round_robin) {
      c.SetRotateDns();
    }
  } else {
    c.SetServerHostPort(DEFAULT_DNS);    
  }
  if (!async_resolver_.Initialize(c)) {
    return false;
  }
  return true;    
}
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
int NqClientLoop::Open(int max_nfd, const nq_dns_conf_t *dns_conf) {
  if (!InitResolver(dns_conf)) {
    return NQ_ERESOLVE;
  }
  return NqLoop::Open(max_nfd, CLIENT_LOOP_WAIT_NS);
}
void NqClientLoop::Poll() {
  processor_.Poll(this);
  async_resolver_.Poll(this);
  NqLoop::Poll();
}
void NqClientLoop::Close() {
  client_map_.Iter([](NqSessionIndex idx, NqClient *cl) {
    TRACE("NqClientLoop::Close %u %p", idx, cl);
    cl->Destroy();
  });
  client_map_.Clear();
  NqLoop::Close();
}
//implements NqBoxer
NqAlarm *NqClientLoop::NewAlarm() {
  auto a = new(this) NqAlarm();
  auto idx = alarm_map_.Add(a);
  nq_serial_t s;
  NqAlarmSerialCodec::ClientEncode(s, idx);
  a->InitSerial(s);
  return a;
}
void NqClientLoop::RemoveAlarm(NqAlarmIndex index) {
  alarm_map_.Remove(index);
}


//called from diseonnecting alarm
void NqClientLoop::RemoveClient(NqClient *cl) {
  client_map_.Remove(cl->session_index());
}
NqClient *NqClientLoop::Create(const std::string &host, 
                               const QuicServerId server_id,
                               const QuicSocketAddress server_address,  
                               NqClientConfig &config) {

  auto supported_versions = AllSupportedVersions();//versions_.GetSupportedVersions();
  auto c = new(this) NqClient(
    server_address,
    server_id, 
    supported_versions,
    config,
    config.NewProofVerifier()
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
/* static */
/*bool NqClientLoop::ParseUrl(const std::string &host, int port, int address_family, 
                            QuicServerId& server_id, QuicSocketAddress &address, QuicConfig &config) {
  if (host.empty()) {
    return false;
  } else if (port == 0) {
    port = 443;
  }
  const int AF_COUNT = 2;
  int *af_order, af_order_sorted[AF_COUNT], default_af_order[AF_COUNT] = {
    AF_INET, AF_INET6,
  };
  if (address_family == 0) {
    af_order = default_af_order;
  } else {
    int cnt = 0;
    af_order_sorted[cnt++] = address_family;
    for (int i = 0; i < AF_COUNT && cnt < AF_COUNT; i++) {
      if (default_af_order[i] != address_family) {
        af_order_sorted[cnt++] = default_af_order[i];
      }
    }
    af_order = af_order_sorted;
  }
  for (int i = 0; i < AF_COUNT; i++) {
    struct addrinfo filter, *resolved;
    filter.ai_socktype = SOCK_DGRAM;
    filter.ai_family = af_order[i];
    filter.ai_protocol = 0;
    filter.ai_flags = 0;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &filter, &resolved) != 0) {
      continue;
    }
    try {
      SockaddrStorage ss;
      memcpy(ss.addr, resolved->ai_addr, resolved->ai_addrlen);
      server_id = QuicServerId(host, port, PRIVACY_MODE_ENABLED); //TODO: control privacy mode from url
      address = QuicSocketAddress(ss.addr_storage);
      freeaddrinfo(resolved);
      return true;
    } catch (...) {
      freeaddrinfo(resolved);
      return false;
    }
  }
  return false;
}	*/
} //net