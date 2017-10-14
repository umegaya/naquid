#include "interop/nq_client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/core/quic_crypto_client_stream.h"

#include "core/closure.h"
#include "interop/nq_stream.h"
#include "interop/nq_network_helper.h"

namespace net {

NqClient::NqClient(QuicSocketAddress server_address,
                           const QuicServerId& server_id,
                           const QuicVersionVector& supported_versions,
                           const NqClientConfig &config,
                           NqClientLoop* loop,
                           std::unique_ptr<ProofVerifier> proof_verifier)
  : QuicClientBase(
          server_id,
          supported_versions,
          config,
          loop,
          loop,
          QuicWrapUnique(new NqNetworkHelper(loop, this)),
          std::move(proof_verifier)), loop_(loop), 
          on_close_(config.client().on_close), 
          on_open_(config.client().on_open), 
          destroyed_(false) {
  set_server_address(server_address);
}
NqClient::~NqClient() {}

std::unique_ptr<QuicSession> NqClient::CreateQuicClientSession(QuicConnection* connection) {
  return QuicWrapUnique(new NqSession(connection, this, *config()));
}

//implements QuicCryptoClientStream::ProofHandler
void NqClient::OnProofValid(const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(iyatomi): Handle the proof verification.
}

void NqClient::OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) {
  // TODO(iyatomi): Handle the proof verification.
}

//implements NqSession::Delegate
void NqClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  uint64_t next_connect_us = nq_closure_call(on_close_, on_conn_close, NqSession::CastFrom(this), 
                                            (int)error, 
                                            error_details.c_str(), 
                                            close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
  //TODO: schedule reconnection somehow, if chromium stack does not do it
  if (next_connect_us > 0 && !destroyed_) {
    auto alarm = alarm_factory()->CreateAlarm(new ReconnectAlarm(this));
    alarm->Set(NqLoop::ToQuicTime(next_connect_us));
  } else {
    auto alarm = alarm_factory()->CreateAlarm(this);
    alarm->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
    //cannot touch this client memory afterward. alarm invocation automatically delete the object.
  }
  return;
}
void NqClient::Disconnect() {
  set_destroyed();
  QuicClientBase::Disconnect(); 
}
bool NqClient::Reconnect() {
  QuicClientBase::Disconnect(); 
  return true;
}
const nq::HandlerMap *NqClient::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : loop_->handler_map();
}

nq::HandlerMap* NqClient::ResetHandlerMap() {
  own_handler_map_.reset(new nq::HandlerMap());
  return own_handler_map_.get();
}
QuicStream *NqClient::NewStream(const std::string &name) {
  auto s = static_cast<NqStream *>(bare_session()->CreateOutgoingDynamicStream());
  s->set_protocol(name);
  return s;
}
QuicCryptoStream *NqClient::NewCryptoStream(NqSession* session) {
  return new QuicCryptoClientStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}

}  // namespace net
