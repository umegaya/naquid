#pragma once

#include <cstdint>
#include <memory>
#include <string>
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
  class ReconnectAlarm : public QuicAlarm::Delegate {
   public:
    ReconnectAlarm(NqClient *client) : client_(client) {}
    void OnAlarm() { client_->StartConnect(); }
   private:
    NqClient *client_;
  };
  class StreamManager {
    struct Entry {
      std::vector<NqClientStream*> streams_;
      std::string name_;
    };
    std::map<NqStreamNameId, Entry> map_;
    NqStreamNameId seed_;
   public:
    StreamManager() : map_(), seed_(0) {}
    
    void OnClose(NqClientStream *s);
    bool OnOpen(const std::string &name, NqClientStream *s);
    NqClientStream *FindOrCreateStream(
      NqClientSession *session, 
      NqStreamNameId name_id, 
      NqStreamIndexPerNameId index_per_name_id, 
      bool connected);
    
    inline const std::string &Find(NqStreamNameId id) const {
      static std::string empty_;
      auto it = map_.find(id);
      return it != map_.end() ? it->second.name_ : empty_;
    }
    inline NqStreamNameId Add(const std::string &name) {
      //add entry to name <=> conversion map
      for (auto &kv : map_) {
        if (kv.second.name_ == name) {
          return kv.first;
        }
      }
      auto id = ++seed_;
      Entry e;
      e.name_ = name;
      map_[id] = e;
      return id;
    }
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
  inline void set_destroyed() { destroyed_ = true; }
  inline NqSessionIndex session_index() const { return session_index_; }
  inline StreamManager &stream_manager() { return stream_manager_; }
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
  void OnAlarm() override { loop_->RemoveClient(this); }

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
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  bool OnOpen(nq_handshake_event_t hsev) override;
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

 private:
  NqClientLoop* loop_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
  std::unique_ptr<QuicAlarm> alarm_;
  nq_closure_t on_close_, on_open_;
  NqSessionIndex session_index_;
  StreamManager stream_manager_;
  uint64_t next_reconnect_us_ts_;
  bool destroyed_;

  DISALLOW_COPY_AND_ASSIGN(NqClient);
};

} //net
