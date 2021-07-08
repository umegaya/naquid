#include "core/compat/nq_client_loop.h"
#include "core/compat/nq_quic_types.h"

#include "core/nq_client.h"

namespace net {
nq::IdFactory<uint32_t> NqClientLoopBase::client_worker_index_factory_;

bool NqClientLoopBase::InitResolver(const nq_dns_conf_t *dns_conf) {
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
int NqClientLoopBase::Open(int max_nfd, const nq_dns_conf_t *dns_conf) {
  if (!InitResolver(dns_conf)) {
    return NQ_ERESOLVE;
  }
  return NqLoop::Open(max_nfd, CLIENT_LOOP_WAIT_NS);
}
void NqClientLoopBase::Poll() {
  processor_.Poll(this);
  async_resolver_.Poll(this);
  NqLoop::Poll();
}
void NqClientLoopBase::Close() {
  client_map_.Iter([](NqSessionIndex idx, NqClient *cl) {
    TRACE("NqClientLoopBase::Close %u %p", idx, cl);
    cl->Destroy();
  });
  client_map_.Clear();
  NqLoop::Close();
}
//implements NqBoxer
NqAlarm *NqClientLoopBase::NewAlarm() {
  auto a = new(this) NqAlarm();
  auto idx = alarm_map_.Add(a);
  nq_serial_t s;
  NqAlarmSerialCodec::ClientEncode(s, idx);
  a->InitSerial(s);
  return a;
}
void NqClientLoopBase::RemoveAlarm(NqAlarmIndex index) {
  alarm_map_.Remove(index);
}


//called from diseonnecting alarm
void NqClientLoopBase::RemoveClient(NqClient *cl) {
  client_map_.Remove(cl->session_index());
}
} //net