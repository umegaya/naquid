#pragma once

#include <string>

#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_crypto_client_stream.h"

#include "basis/defs.h"
#include "basis/handler_map.h"
#include "basis/id_factory.h"
#include "core/nq_serial_codec.h"
#include "core/nq_static_section.h"


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
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void *Context() const = 0;
    virtual void Destroy() = 0;
    virtual void OnClose(QuicErrorCode error,
                         const std::string& error_details,
                         ConnectionCloseSource close_by_peer_or_self) = 0;
    virtual void OnOpen(nq_handshake_event_t hsev) = 0;
    virtual void Disconnect() = 0;
    virtual bool Reconnect() = 0; //only supported for client 
    virtual void DoReconnect() = 0;
    virtual void OnReachabilityChange(nq_reachability_t state) = 0;
    virtual uint64_t ReconnectDurationUS() const = 0;
    virtual bool IsClient() const = 0;
    virtual bool IsConnected() const = 0;
    virtual void InitStream(const std::string &name, void *ctx) = 0;
    virtual void OpenStream(const std::string &name, void *ctx) = 0;
    virtual QuicCryptoStream *NewCryptoStream(NqSession *session) = 0;
    virtual const nq::HandlerMap *GetHandlerMap() const = 0;
    virtual nq::HandlerMap *ResetHandlerMap() = 0;
    virtual NqLoop *GetLoop() = 0;
    virtual QuicConnection *Connection() = 0;
    virtual const NqSerial &SessionSerial() const = 0;
    inline NqSessionIndex SessionIndex() const { 
      return NqSerial::ObjectIndex<NqSessionIndex>(SessionSerial());
    }
  };
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

  void init_crypto_stream() { crypto_stream_.reset(delegate_->NewCryptoStream(this)); }
};
}
