#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <mutex>
#include <stack>
#include <functional>

#include <arpa/inet.h>

#include "net/tools/quic/quic_client_base.h"

#include "basis/closure.h"
#include "core/nq_client_loop.h"
#include "core/nq_client_session.h"
#include "core/nq_config.h"

namespace net {

class QuicServerId;
class NqClientStream;

class NqClient : public QuicClientBase, 
                 public QuicAlarm::Delegate,
                 public QuicCryptoClientStream::ProofHandler, 
                 public NqSession::Delegate {
 public:
  enum ConnectState : uint8_t {
    DISCONNECT,
    CONNECT,
    FINALIZED,
    RECONNECTING,
  };
  class ReconnectAlarm : public QuicAlarm::Delegate {
   public:
    ReconnectAlarm(NqClient *client) : client_(client) {}
    void OnAlarm() { 
      client_->Initialize();
      client_->StartConnect(); 
    }
   private:
    NqClient *client_;
  };
  class StreamManager {
    struct Entry {
      std::vector<NqClientStream*> streams_;
      std::string name_;
    };
    std::vector<Entry> out_entries_;
    std::vector<NqClientStream*> in_entries_;
    std::stack<NqStreamIndexPerNameId> in_empty_indexes_;
    std::mutex entries_mutex_;
   public:
    StreamManager() : out_entries_(), in_entries_(), in_empty_indexes_(), entries_mutex_() {}
    
    inline bool OnOpen(const std::string &name, NqClientStream *s) {
      return ((s->id() % 2) == 0) ? OnIncomingOpen(s) : OnOutgoingOpen(name, s);
    }
    inline void OnClose(NqClientStream *s) {
      if ((s->id() % 2) == 0) {
        OnIncomingClose(s);
      } else {
        OnOutgoingClose(s);
      }
    }
    NqClientStream *FindOrCreateStream(
      NqClientSession *session, 
      NqStreamNameId name_id, 
      NqStreamIndexPerNameId index_per_name_id, 
      bool connected);
    
    //it should be used by non-owner thread of this client. 
    const NqClientStream *Find(NqStreamNameId id, NqStreamIndexPerNameId index) const;
    inline const std::string &Find(NqStreamNameId id) const {
      static std::string empty_;
      return id <= out_entries_.size() ? out_entries_[id - 1].name_ : empty_;
    }
   protected:
    NqStreamNameId Add(const std::string &name);
    bool OnOutgoingOpen(const std::string &name, NqClientStream *s);
    void OnOutgoingClose(NqClientStream *s);
    bool OnIncomingOpen(NqClientStream *s);
    void OnIncomingClose(NqClientStream *s);    
  };
 public:
  // This will create its own QuicClientEpollNetworkHelper.
  NqClient(QuicSocketAddress server_address,
           const QuicServerId& server_id,
           const QuicVersionVector& supported_versions,
           const NqClientConfig &config,
           NqClientLoop* loop,
           std::unique_ptr<ProofVerifier> proof_verifier);
  ~NqClient() override;

  // operation
  NqClientSession *nq_session() { return static_cast<NqClientSession *>(session()); }
  const NqClientSession *nq_session() const { 
    return static_cast<const NqClientSession *>(const_cast<NqClient *>(this)->session()); 
  }
  inline bool destroyed() const { return connect_state_ == FINALIZED; }
  inline NqSessionIndex session_index() const { return session_index_; }
  inline StreamManager &stream_manager() { return stream_manager_; }
  inline const StreamManager &stream_manager() const { return stream_manager_; }
  inline nq_conn_t ToHandle() { return loop_->Box(this); }
  NqClientStream *FindOrCreateStream(NqStreamNameId name_id, NqStreamIndexPerNameId index_per_name_id);


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
  uint64_t Id() const override { return connect_state_ == CONNECT ? nq_session()->connection_id() : 0; }
  void *Context() const override { return context_; }
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  void OnOpen(nq_handshake_event_t hsev) override;
  bool IsClient() const override { return true; }
  void Disconnect() override;
  bool Reconnect() override;
  uint64_t ReconnectDurationUS() const override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
  QuicStream* NewStream(const std::string &name) override;
  QuicCryptoStream *NewCryptoStream(NqSession *session) override;
  NqLoop *GetLoop() override { return loop_; }
  NqBoxer *GetBoxer() override { return static_cast<NqBoxer *>(loop_); }
  NqSessionIndex SessionIndex() const override { return session_index_; }
  QuicConnection *Connection() override { return session()->connection(); }


 private:
  NqClientLoop* loop_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
  std::unique_ptr<QuicAlarm> alarm_;
  nq_closure_t on_close_, on_open_, on_finalize_;
  NqSessionIndex session_index_;
  StreamManager stream_manager_;
  uint64_t next_reconnect_us_ts_;
  ConnectState connect_state_;
  void *context_;

  DISALLOW_COPY_AND_ASSIGN(NqClient);
};

} //net
