#pragma once

#include "core/nq_session.h"
#include "core/nq_server.h"
#include "core/nq_config.h"

namespace net {
class NqServerSession : public NqSession, 
                            public NqSession::Delegate {
 public:
  NqServerSession(QuicConnection *connection,
                      NqDispatcher *dispatcher,
                      const NqServer::PortConfig &port_config);

  //implements NqSession::Delegate
  void OnClose(QuicErrorCode error,
                       const std::string& error_details,
                       ConnectionCloseSource close_by_peer_or_self) override;
  bool OnOpen() override;

  void Disconnect() override;
  bool Reconnect() override; //only supported for client 
  bool IsClient() override;
  QuicStream *NewStream(const std::string &name) override;
  QuicCryptoStream *NewCryptoStream(NqSession *session) override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
 private:
  NqDispatcher *dispatcher_;
  const NqServer::PortConfig &port_config_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
};

}
