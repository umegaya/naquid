#pragma once

#include <string>

#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_stream.h"

#include "core/handler_map.h"

namespace net {

// A QUIC session with a headers stream.
class NaquidSession : public QuicSession {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnClose(QuicErrorCode error,
                         const std::string& error_details,
                         ConnectionCloseSource close_by_peer_or_self) = 0;
    virtual bool OnOpen() = 0;

    virtual void Disconnect() = 0;
    virtual void Reconnect() = 0; //only supported for client 
    virtual bool IsClient() = 0;
    virtual QuicStream *NewStream(const std::string &name) = 0;
    virtual QuicCryptoStream *NewCryptoStream(NaquidSession *session) = 0;
    virtual nq::HandlerMap *GetHandlerMap() = 0;
    virtual nq::HandlerMap *ResetHandlerMap() = 0;
  };
 private:
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
  Delegate *delegate_;
 public:
  NaquidSession(QuicConnection *connection,
                Delegate* delegate,
              	const QuicConfig& config);

  bool IsClient() const { return connection()->perspective() == Perspective::IS_CLIENT; }
  Delegate *delegate() { return delegate_; }
  nq::HandlerMap *handler_map() { return delegate_->GetHandlerMap(); }
  nq_conn_t conn() { return CastFrom(delegate()); }
  static inline nq_conn_t CastFrom(NaquidSession::Delegate *d) { return (nq_conn_t)d; }

  //implements QuicSession
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicStream* CreateOutgoingDynamicStream() override;
  QuicCryptoStream *GetMutableCryptoStream() override;
  const QuicCryptoStream *GetCryptoStream() const override;

  //implements QuicConnectionVisitorInterface
  void OnConnectionClosed(QuicErrorCode error,
                                  const std::string& error_details,
                                  ConnectionCloseSource source) override;
  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;
};

}
