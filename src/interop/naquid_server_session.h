#pragma once

#include "interop/naquid_session.h"

namespace net {

// A QUIC session with a headers stream.
class NaquidServerSession : public NaquidSession, 
                            public NaquidSession::Delegate {
 public:
  NaquidServerSession(QuicConnection *connection,
                      const NaquidServer &server_,
                      const QuicConfig& config);

  //implements NaquidSession::Delegate
  void OnClose(QuicErrorCode error,
                       const std::string& error_details,
                       ConnectionCloseSource close_by_peer_or_self) override;
  bool OnOpen() override;

  void Disconnect() override;
  void Reconnect() override; //only supported for client 
  bool IsClient() override;
  QuicStream *NewStream(const std::string &name) override;
  QuicCryptoStream *NewCryptoStream(NaquidSession *session) override;
  nq::HandlerMap *GetHandlerMap() override;
  nq::HandlerMap *ResetHandlerMap() override;
 private:
  const NaquidServer &server_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
};

}
