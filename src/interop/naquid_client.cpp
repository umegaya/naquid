#include "interop/naquid_client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/core/quic_crypto_client_stream.h"

#include "core/closure.h"
#include "interop/naquid_stream.h"
#include "interop/naquid_network_helper.h"

namespace net {

NaquidClient::NaquidClient(QuicSocketAddress server_address,
                           const QuicServerId& server_id,
                           const QuicVersionVector& supported_versions,
                           const NaquidClientConfig &config,
                           NaquidClientLoop* loop,
                           std::unique_ptr<ProofVerifier> proof_verifier)
  : QuicClientBase(
          server_id,
          supported_versions,
          config,
          loop,
          loop,
          QuicWrapUnique(new NaquidNetworkHelper(loop, this)),
          std::move(proof_verifier)), loop_(loop), 
          on_close_(config.client().on_close), 
          on_open_(config.client().on_open), 
          destroyed_(false) {
  set_server_address(server_address);
}
NaquidClient::~NaquidClient() {}

std::unique_ptr<QuicSession> NaquidClient::CreateQuicClientSession(QuicConnection* connection) {
  return QuicWrapUnique(new NaquidSession(connection, this, *config()));
}

//implements QuicCryptoClientStream::ProofHandler
void NaquidClient::OnProofValid(const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(iyatomi): Handle the proof verification.
}

void NaquidClient::OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) {
  // TODO(iyatomi): Handle the proof verification.
}

//implements NaquidSession::Delegate
void NaquidClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  uint64_t next_connect_us = nq_closure_call(on_close_, on_conn_close, NaquidSession::CastFrom(this), 
                                            (int)error, 
                                            error_details.c_str(), 
                                            close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
  //TODO: schedule reconnection somehow, if chromium stack does not do it
  if (next_connect_us > 0 && !destroyed_) {
    auto alarm = alarm_factory()->CreateAlarm(new ReconnectAlarm(this));
    alarm->Set(NaquidLoop::ToQuicTime(next_connect_us));
  } else {
    auto alarm = alarm_factory()->CreateAlarm(this);
    alarm->Set(NaquidLoop::ToQuicTime(loop_->NowInUsec()));
    //cannot touch this client memory afterward. alarm invocation automatically delete the object.
  }
  return;
}
void NaquidClient::Disconnect() {
  set_destroyed();
  QuicClientBase::Disconnect(); 
}
bool NaquidClient::Reconnect() {
  QuicClientBase::Disconnect(); 
  return true;
}
nq::HandlerMap *NaquidClient::GetHandlerMap() {
  return own_hander_map_ != nullptr ? own_hander_map_.get() : loop_->handler_map();
}

nq::HandlerMap* NaquidClient::ResetHandlerMap() {
  own_hander_map_.reset(new nq::HandlerMap());
  return own_hander_map_.get();
}
QuicStream *NaquidClient::NewStream(const std::string &name) {
  auto s = static_cast<NaquidStream *>(bare_session()->CreateOutgoingDynamicStream());
  s->set_protocol(name);
  return s;
}
QuicCryptoStream *NaquidClient::NewCryptoStream(NaquidSession* session) {
  return new QuicCryptoClientStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}

}  // namespace net
