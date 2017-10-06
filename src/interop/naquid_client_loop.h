#pragma once

#include "net/quic/core/quic_server_id.h"

#include "interop/naquid_loop.h"
#include "interop/naquid_config.h"

namespace net {
class NaquidClient;
class NaquidClientLoop : public NaquidLoop {
  nq::HandlerMap hdmap_;
  std::map<std::string, NaquidClient*> client_map_; //TODO(iyatomi): faster hash key
 public:
  NaquidClientLoop() : hdmap_(), client_map_() {}
  ~NaquidClientLoop() { Close(); }
  void Close();
  void RemoveClient(NaquidClient *cl);

  inline nq::HandlerMap *hdmap() { return &hdmap_; }
  NaquidClient *Create(const std::string &url, 
                       NaquidClientConfig &config);
  static bool ParseUrl(const std::string &url, 
                       QuicServerId& server_id, 
                       QuicSocketAddress &address, 
                       QuicConfig &config);

};
}