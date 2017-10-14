#pragma once

#include "net/quic/core/quic_server_id.h"

#include "interop/naquid_loop.h"
#include "interop/naquid_config.h"

namespace net {
class NaquidClient;
class NaquidClientLoop : public NaquidLoop {
  nq::HandlerMap handler_map_;
  std::map<std::string, NaquidClient*> client_map_; //TODO(iyatomi): faster hash key
 public:
  NaquidClientLoop() : handler_map_(), client_map_() {}
  ~NaquidClientLoop() { Close(); }
  void Close();
  void RemoveClient(NaquidClient *cl);

  inline nq::HandlerMap *mutable_handler_map() { return &handler_map_; }
  inline const nq::HandlerMap *handler_map() const { return &handler_map_; }
  NaquidClient *Create(const std::string &host, 
                       int port, 
                       NaquidClientConfig &config);
  static bool ParseUrl(const std::string &host, 
                       int port, 
                       QuicServerId& server_id, 
                       QuicSocketAddress &address, 
                       QuicConfig &config);

};
}