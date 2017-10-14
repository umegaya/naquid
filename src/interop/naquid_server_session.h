#pragma once

#include "interop/naquid_session.h"
#include "interop/naquid_server.h"
#include "interop/naquid_config.h"

namespace net {
class NaquidServerSession : public NaquidSession, 
                            public NaquidSession::Delegate {
 public:
  NaquidServerSession(QuicConnection *connection,
                      NaquidDispatcher *dispatcher,
                      const NaquidServer::PortConfig &port_config);

  //implements NaquidSession::Delegate
  void OnClose(QuicErrorCode error,
                       const std::string& error_details,
                       ConnectionCloseSource close_by_peer_or_self) override;
  bool OnOpen() override;

  void Disconnect() override;
  bool Reconnect() override; //only supported for client 
  bool IsClient() override;
  QuicStream *NewStream(const std::string &name) override;
  QuicCryptoStream *NewCryptoStream(NaquidSession *session) override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
 private:
  NaquidDispatcher *dispatcher_;
  const NaquidServer::PortConfig &port_config_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
};

}
