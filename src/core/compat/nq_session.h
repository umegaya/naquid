#pragma once

#include <string>

#include "basis/defs.h"
#include "core/nq_session_delegate.h"
#include "core/compat/nq_quic_types.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_crypto_client_stream.h"

namespace nq {
using namespace net;
class NqServerSession;
// A QUIC session with a headers stream.
class NqSession : public QuicSession {
 public:
  //dummy class to call protected method of NqQuicConnection...
  class NqConnection : public NqQuicConnection {
   public: 
    void Cleanup() {
      CancelAllAlarms();
    }
  };
 public:
  //NqSession takes ownership of connection
  NqSession(NqQuicConnection *connection,
            Visitor* owner,
            NqSessionDelegate* delegate,
          	const QuicConfig& config);
  ~NqSession() override;

  //get/set
  inline NqSessionDelegate *delegate() { return delegate_; }
  inline const NqSessionDelegate *delegate() const { return delegate_; }
  inline const HandlerMap *handler_map() { return delegate_->GetHandlerMap(); }
  inline nq_conn_t ToHandle() { 
    return MakeHandle<nq_conn_t, NqSessionDelegate>(delegate_, delegate_->SessionSerial()); 
  }
  
  //operation
  inline void RegisterStreamPriority(QuicStreamId id, SpdyPriority priority) {
    write_blocked_streams()->RegisterStream(id, priority);
  }

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

 private:
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
  NqSessionDelegate *delegate_;
};
#define DefferedFlushStream(__session__) \
  NqQuicConnection::ScopedPacketBundler bundler( \
    (__session__)->connection(), NqQuicConnection::SEND_ACK_IF_QUEUED)
} // namespace nq
#else
#include "core/nq_stream.h"
namespace nq {
class NqStream;
// A QUIC session with a headers stream.
class NqSession {
 public:
  using DynamicStreamMap = std::map<NqQuicStreamId, std::unique_ptr<NqStream>>;
  using ClosedStreams = std::vector<std::unique_ptr<NqStream>>;
  using ZombieStreamMap = std::map<NqQuicStreamId, std::unique_ptr<NqStream>>;

  //NqSession takes ownership of connection
  NqSession(NqQuicConnection *connection,
            NqSessionDelegate* delegate) : 
            connection_(connection), delegate_(delegate), 
            dynamic_stream_map_(), closed_streams_(), zombie_streams_() {}
  virtual ~NqSession() {}

  //get/set
  inline NqSessionDelegate *delegate() { return delegate_; }
  inline const NqSessionDelegate *delegate() const { return delegate_; }
  inline const HandlerMap *handler_map() { return delegate_->GetHandlerMap(); }
  inline DynamicStreamMap& dynamic_streams() { return dynamic_stream_map_; }
  inline const DynamicStreamMap& dynamic_streams() const { return dynamic_stream_map_; }
  inline ClosedStreams* closed_streams() { return &closed_streams_; }
  inline const ZombieStreamMap& zombie_streams() const { return zombie_streams_; }
  inline nq_conn_t ToHandle() { 
    return MakeHandle<nq_conn_t, NqSessionDelegate>(delegate_, delegate_->SessionSerial()); 
  }

  //operation
  void CloseStream(NqQuicStreamId stream_id);

 protected:
  NqQuicConnection *connection_;
  NqSessionDelegate *delegate_;
  DynamicStreamMap dynamic_stream_map_; // active streamsd
  ClosedStreams closed_streams_;        // stream already closed
  ZombieStreamMap zombie_streams_;      // closed stream which still waiting acks
};
#define DefferedFlushStream(__session__)
} // namespace nq
#endif