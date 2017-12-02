#include "core/nq_client_loop.h"

#include "net/base/sockaddr_storage.h"

#include "core/nq_client.h"

namespace net {
void NqClientLoop::Poll() {
  processor_.Poll(this);
  NqLoop::Poll();
}
void NqClientLoop::Close() {
  client_map_.Clear();
  NqLoop::Close();
}
//implements NqBoxer
nq_conn_t NqClientLoop::Box(NqSession::Delegate *d) {
  return {
    .p = static_cast<NqBoxer*>(this),
    .s = NqConnSerialCodec::ClientEncode(
      d->SessionIndex()
    ),
  };
}
nq_stream_t NqClientLoop::Box(NqStream *s) {
  auto cs = static_cast<NqClientStream *>(s);
  return {
    .p = static_cast<NqBoxer*>(this),
    .s = NqStreamSerialCodec::ClientEncode(
      cs->session_index(),
      cs->name_id(),
      cs->index_per_name_id()
    ),
  };
}
nq_alarm_t NqClientLoop::Box(NqAlarm *a) {
  AddAlarm(a);
  return {
    .p = static_cast<NqBoxer*>(this),
    .s = NqAlarmSerialCodec::ClientEncode(a->alarm_index()),
  };
}
NqBoxer::UnboxResult 
NqClientLoop::Unbox(uint64_t serial, NqSession::Delegate **unboxed) {
  if (!main_thread()) {
    return NqBoxer::UnboxResult::NeedTransfer;
  }
  auto index = NqConnSerialCodec::ClientSessionIndex(serial);
  auto it = client_map_.find(index);
  if (it != client_map_.end()) {
    *unboxed = it->second;
    return NqBoxer::UnboxResult::Ok;
  }
  return NqBoxer::UnboxResult::SerialExpire;
}
NqBoxer::UnboxResult 
NqClientLoop::Unbox(uint64_t serial, NqAlarm **unboxed) {
  if (!main_thread()) {
    return NqBoxer::UnboxResult::NeedTransfer;
  }
  auto index = NqAlarmSerialCodec::ClientAlarmIndex(serial);
  auto it = alarm_map_.find(index);
  if (it != alarm_map_.end()) {
    *unboxed = it->second;
    return NqBoxer::UnboxResult::Ok;
  }
  return NqBoxer::UnboxResult::SerialExpire;
}
NqBoxer::UnboxResult
NqClientLoop::Unbox(uint64_t serial, NqStream **unboxed) {
  if (!main_thread()) {
    return NqBoxer::UnboxResult::NeedTransfer;
  }
  auto index = NqStreamSerialCodec::ClientSessionIndex(serial);
  auto name_id = NqStreamSerialCodec::ClientStreamNameId(serial);
  auto stream_index = NqStreamSerialCodec::ClientStreamIndexPerName(serial);
  auto it = client_map_.find(index);
  if (it != client_map_.end()) {
    *unboxed = it->second->FindOrCreateStream(name_id, stream_index);
    if (*unboxed != nullptr) {
      return NqBoxer::UnboxResult::Ok;
    }
  }
  return NqBoxer::UnboxResult::SerialExpire;
}
const NqSession::Delegate *NqClientLoop::FindConn(uint64_t serial, NqBoxer::OpTarget target) const {
  switch (target) {
  case Conn:
    return client_map().Active(NqConnSerialCodec::ClientSessionIndex(serial));
  case Stream:
    return client_map().Active(NqStreamSerialCodec::ClientSessionIndex(serial));
  default:
    ASSERT(false);
    return nullptr;
  }
}
const NqStream *NqClientLoop::FindStream(uint64_t serial) const {
  auto c = client_map().Active(NqStreamSerialCodec::ClientSessionIndex(serial));
  if (c == nullptr) { return nullptr; }
  return c->stream_manager().Find(NqStreamSerialCodec::ClientStreamNameId(serial), 
                                  NqStreamSerialCodec::ClientStreamIndexPerName(serial));
}
void NqClientLoop::AddAlarm(NqAlarm *a) {
  if (a->alarm_index() == 0) {
    a->set_alarm_index(new_alarm_index());
    alarm_map_.Add(a->alarm_index(), a);
  } else {
#if defined(DEBUG)
    auto it = alarm_map_.find(a->alarm_index());
    ASSERT(it != alarm_map_.end() && it->second == a);
#endif
  }
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
  client_map_.Add(c->session_index(), c);
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