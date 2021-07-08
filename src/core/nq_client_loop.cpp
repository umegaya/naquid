#include "core/nq_client_loop.h"

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
                               const NqQuicServerId server_id,
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
} //net