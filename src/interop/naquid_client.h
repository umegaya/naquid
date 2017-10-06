#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <functional>

#include <arpa/inet.h>

#include "net/tools/quic/quic_client_base.h"

#include "core/closure.h"
#include "interop/naquid_client_loop.h"
#include "interop/naquid_session.h"
#include "interop/naquid_config.h"

namespace net {

class QuicServerId;

class NaquidClient : public QuicClientBase, 
                     public QuicAlarm::Delegate,
                     public QuicCryptoClientStream::ProofHandler, 
                     public NaquidSession::Delegate {
 public:
  class ReconnectAlarm : public QuicAlarm::Delegate {
   public:
    ReconnectAlarm(NaquidClient *client) : client_(client) {}
    void OnAlarm() { client_->StartConnect(); }
   private:
    NaquidClient *client_;
  };
 public:
  // This will create its own QuicClientEpollNetworkHelper.
  NaquidClient(QuicSocketAddress server_address,
                 const QuicServerId& server_id,
                 const QuicVersionVector& supported_versions,
                 const NaquidClientConfig &config,
                 NaquidClientLoop* loop,
                 std::unique_ptr<ProofVerifier> proof_verifier);
  ~NaquidClient() override;

  // operation
  inline NaquidSession *bare_session() { return static_cast<NaquidSession *>(session()); }
  inline void set_destroyed() { destroyed_ = true; }

  // implements QuicClientBase. TODO(umegaya): these are really not needed?
  int GetNumSentClientHellosFromSession() override { return 0; }
  int GetNumReceivedServerConfigUpdatesFromSession() override { return 0; }
  void ResendSavedData() override {}
  void ClearDataToResend() override {}
  std::unique_ptr<QuicSession> CreateQuicClientSession(QuicConnection* connection) override;

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


  // implements NaquidSession::Delegate
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  bool OnOpen() override { return nq_closure_call(on_open_, on_conn_open, NaquidSession::CastFrom(this)); }
  bool IsClient() override { return bare_session()->IsClient(); }
  void Disconnect() override;
  void Reconnect() override;
  nq::HandlerMap *GetHandlerMap() override;
  nq::HandlerMap *ResetHandlerMap() override;
  QuicStream* NewStream(const std::string &name) override;
  QuicCryptoStream *NewCryptoStream(NaquidSession *session) override;

 private:
  NaquidClientLoop* loop_;
  nq::HandlerMap* hdmap_;
  nq_closure_t on_close_, on_open_;
  bool destroyed_;

  DISALLOW_COPY_AND_ASSIGN(NaquidClient);
};

} //net
