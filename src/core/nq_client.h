#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <mutex>
#include <stack>
#include <functional>

#include <arpa/inet.h>

#include "net/tools/quic/quic_client_base.h"

#include "basis/allocator.h"
#include "core/nq_client_session.h"
#include "core/nq_config.h"

namespace net {

class QuicServerId;
class NqClientLoop;
class NqClientStream;
class NqBoxer;

class NqClient : public QuicClientBase, 
                 public QuicAlarm::Delegate,
                 public QuicCryptoClientStream::ProofHandler, 
                 public NqSession::Delegate {
 public:
  enum ConnectState : uint8_t {
    DISCONNECT,
    CONNECTING,
    CONNECTED,
    FINALIZED,
    RECONNECTING,
  };
  class ReconnectAlarm : public QuicAlarm::Delegate {
   public:
    ReconnectAlarm(NqClient *client) : client_(client) {}
    void OnAlarm();
   private:
    NqClient *client_;
  };
  class StreamManager {
    struct Entry {
      NqClientStream *handle_;
      std::string name_;
      void *context_;
      Entry(NqClientStream *h) : 
        handle_(h), name_(), context_(nullptr) {}
      Entry(NqClientStream *h, const std::string &name) : 
        handle_(h), name_(name), context_(nullptr) {}
      inline void SetStream(NqClientStream *s) { handle_ = s; }
      inline void ClearStream() { handle_ = nullptr; }
      inline NqClientStream *Stream() { return handle_; }
      inline void *Context() const { return context_; }
      inline void **ContextBuffer() { return &context_; }      
      inline void SetName(const std::string &name) { name_ = name; }
      inline const std::string &Name() const { return name_; }
    };

    using StreamMap = QuicSmallMap<NqStreamIndex, Entry, 10>;
    using StreamVector = std::vector<Entry>;

    StreamMap entries_;
    nq::IdFactoryNoAtomic<NqStreamIndex> index_factory_;
   public:
    StreamManager() : entries_(), index_factory_(0x7FFFFFFF) {}
    
    bool OnOutgoingOpen(NqClient *client, bool connected,
                        const std::string &name, void *ctx);
    NqStreamIndex OnIncomingOpen(NqClientStream *s);
    void OnClose(NqClientStream *s);

    NqClientStream *FindOrCreateStream(
      NqClientSession *session, 
      NqStreamIndex stream_index, 
      bool connected);

    //recover all created outgoing streams on reconnection done
    void RecoverOutgoingStreams(NqClientSession *session);
    void CleanupStreamsOnClose();
    
    //it should be used by non-owner thread of this client. 
    inline void *FindContext(NqStreamIndex index) const {
      auto *e = FindEntry(index);
      return e == nullptr ? nullptr : e->Context();
    }
    inline void **FindContextBuffer(NqStreamIndex index) {
      auto *e = FindEntry(index);
      return e == nullptr ? nullptr : e->ContextBuffer();
    }
    inline const std::string &FindStreamName(NqStreamIndex index) const {
      static std::string empty_;
      auto *e = FindEntry(index);
      return e == nullptr ? empty_ : e->Name();
    }
    inline NqStreamIndex NewIndex() { return index_factory_.New(); }
   protected:
    Entry *FindEntry(NqStreamIndex index);
    inline const Entry *FindEntry(NqStreamIndex index) const { 
      return const_cast<StreamManager *>(this)->FindEntry(index); 
    }
    void OnOutgoingClose(NqClientStream *s);
    void OnIncomingClose(NqClientStream *s);    
  };
 public:
  // This will create its own QuicClientEpollNetworkHelper.
  NqClient(QuicSocketAddress server_address,
           const QuicServerId& server_id,
           const QuicVersionVector& supported_versions,
           const NqClientConfig &config,
           std::unique_ptr<ProofVerifier> proof_verifier);
  ~NqClient() override;

  // operation
  inline NqClientSession *nq_session() { return static_cast<NqClientSession *>(session()); }
  inline const NqClientSession *nq_session() const { return const_cast<NqClient *>(this)->nq_session(); }
  inline bool destroyed() const { return connect_state_ == FINALIZED; }
  inline StreamManager &stream_manager() { return stream_manager_; }
  inline const StreamManager &stream_manager() const { return stream_manager_; }
  inline NqClientLoop *client_loop() { return loop_; }
  inline const NqSerial &session_serial() const { return session_serial_; }
  inline NqSessionIndex session_index() const { 
    return NqConnSerialCodec::ClientSessionIndex(session_serial_); }

  std::mutex &static_mutex();
  NqBoxer *boxer();
  nq_conn_t ToHandle();
  NqClientStream *FindOrCreateStream(NqStreamIndex index);
  void InitSerial();
  inline void InvalidateSerial() { 
    std::unique_lock<std::mutex> lk(static_mutex());
    session_serial_.Clear(); 
  }
  inline void Destroy() { OnAlarm(); }


  // implements QuicClientBase. TODO(umegaya): these are really not needed?
  int GetNumSentClientHellosFromSession() override { return 0; }
  int GetNumReceivedServerConfigUpdatesFromSession() override { return 0; }
  void ResendSavedData() override {}
  void ClearDataToResend() override {}
  std::unique_ptr<QuicSession> CreateQuicClientSession(QuicConnection* connection) override;
  void InitializeSession() override;


  // implements QuicAlarm::Delegate
  void OnAlarm() override;
  // implements QuicCryptoClientStream::ProofHandler
  // Called when the proof in |cached| is marked valid.  If this is a secure
  // QUIC session, then this will happen only after the proof verifier
  // completes.
  void OnProofValid(const QuicCryptoClientConfig::CachedState& cached) override;
  // Called when proof verification details become available, either because
  // proof verification is complete, or when cached details are used. This
  // will only be called for secure QUIC connections.
  void OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) override;


  // implements NqSession::Delegate
  void *Context() const override { return context_; }
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  void OnOpen(nq_handshake_event_t hsev) override;
  bool IsClient() const override { return true; }
  bool IsConnected() const override { return connect_state_ == CONNECTED; }
  void Disconnect() override;
  bool Reconnect() override;
  uint64_t ReconnectDurationUS() const override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
  void InitStream(const std::string &name, void *ctx) override;
  void OpenStream(const std::string &name, void *ctx) override;
  QuicCryptoStream *NewCryptoStream(NqSession *session) override;
  NqLoop *GetLoop() override;
  QuicConnection *Connection() override { return session()->connection(); }
  const NqSerial &SessionSerial() const override { return session_serial(); }


  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, NqClientLoop *l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, NqClientLoop *l) noexcept;

 private:
  NqClientLoop* loop_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
  std::unique_ptr<QuicAlarm> alarm_;
  nq_closure_t on_close_, on_open_, on_finalize_;
  NqSerial session_serial_;
  StreamManager stream_manager_;
  uint64_t next_reconnect_us_ts_;
  nq::atomic<ConnectState> connect_state_;
  void *context_;

  DISALLOW_COPY_AND_ASSIGN(NqClient);
};

} //net
