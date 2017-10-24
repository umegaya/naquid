#pragma once

#include "net/quic/core/quic_server_id.h"

#include "core/nq_loop.h"
#include "core/nq_config.h"

namespace net {
class NqClient;
class NqClientLoop : public NqLoop {
  nq::HandlerMap handler_map_;
  std::map<std::string, NqClient*> client_map_; //TODO(iyatomi): faster hash key
 public:
  NqClientLoop() : handler_map_(), client_map_() {}
  ~NqClientLoop() { Close(); }
  void Close();
  void RemoveClient(NqClient *cl);

  inline nq::HandlerMap *mutable_handler_map() { return &handler_map_; }
  inline const nq::HandlerMap *handler_map() const { return &handler_map_; }
  NqClient *Create(const std::string &host, 
                       int port, 
                       NqClientConfig &config);
  static bool ParseUrl(const std::string &host, 
                       int port, 
                       int address_family,
                       QuicServerId& server_id, 
                       QuicSocketAddress &address, 
                       QuicConfig &config);

};
}