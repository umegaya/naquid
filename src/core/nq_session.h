#pragma once

#include <string>

#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_crypto_client_stream.h"

#include "basis/defs.h"
#include "basis/id_factory.h"
#include "core/nq_static_section.h"
#include "core/nq_session_delegate.h"

namespace net {
class NqLoop;
class NqStream;
// A QUIC session with a headers stream.
class NqSession : public QuicSession {
 public:
  //dummy class to call protected method of QuicConnection...
  class NqConnection : public QuicConnection {
   public: 
    void Cleanup() {
      CancelAllAlarms();
    }
  };
  typedef NqSessionDelegate Delegate;
 private:
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
  Delegate *delegate_;
 public:
  //NqSession takes ownership of connection
  NqSession(QuicConnection *connection,
            Visitor* owner,
            Delegate* delegate,
          	const QuicConfig& config);
  ~NqSession() override;

  inline void RegisterStreamPriority(QuicStreamId id, SpdyPriority priority) {
    write_blocked_streams()->RegisterStream(id, priority);
  }

  inline bool IsClient() const { return connection()->perspective() == Perspective::IS_CLIENT; }
  inline Delegate *delegate() { return delegate_; }
  inline const Delegate *delegate() const { return delegate_; }
  inline const nq::HandlerMap *handler_map() { return delegate_->GetHandlerMap(); }
  inline nq_conn_t ToHandle() { return MakeHandle<nq_conn_t, Delegate>(delegate_, delegate_->SessionSerial()); }

  //implements QuicConnectionVisitorInterface
  void OnConnectionClosed(QuicErrorCode error,
                          const std::string& error_details,
                          ConnectionCloseSource source) override;
  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

 protected:
  //implements QuicSession
  QuicCryptoStream *GetMutableCryptoStream() override;
  const QuicCryptoStream *GetCryptoStream() const override;
  void SetCryptoStream(QuicCryptoStream *cs) { crypto_stream_.reset(cs); }
};
}
