#include "core/nq_client_loop.h"

#include "net/base/sockaddr_storage.h"

#include "core/nq_client.h"

namespace net {
nq::IdFactory<uint32_t> NqClientLoop::client_worker_index_factory_;

void NqClientLoop::Poll() {
  processor_.Poll(this);
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
                               int port,  
                               NqClientConfig &config) {
  QuicServerId server_id;
  QuicSocketAddress server_address;
  if (!ParseUrl(host, port, 0, server_id, server_address, config)) {
    return nullptr;
  }
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
  c->StartConnect();
  c->InitSerial(); //insert c into client_map_
  return c;
}
/* static */
bool NqClientLoop::ParseUrl(const std::string &host, int port, int address_family, 
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
}	
} //net