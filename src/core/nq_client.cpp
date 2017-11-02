#include "core/nq_client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/core/quic_crypto_client_stream.h"

#include "basis/closure.h"
#include "basis/timespec.h"
#include "core/nq_stream.h"
#include "core/nq_network_helper.h"
#include "core/nq_stub_interface.h"

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
          new NqStubConnectionHelper(*loop),
          new NqStubAlarmFactory(*loop),
          QuicWrapUnique(new NqNetworkHelper(loop, this)),
          std::move(proof_verifier)), loop_(loop), 
          on_close_(config.client().on_close), 
          on_open_(config.client().on_open), 
          session_index_(loop->new_session_index()), 
          stream_manager_(), destroyed_(false) {
  set_server_address(server_address);
}
NqClient::~NqClient() {}


std::unique_ptr<QuicSession> NqClient::CreateQuicClientSession(QuicConnection* connection) {
  return QuicWrapUnique(new NqClientSession(connection, this, *config()));
}
void NqClient::InitializeSession() {
  QuicClientBase::InitializeSession();
  nq_session()->GetClientCryptoStream()->CryptoConnect();
  //make connection invalid
  loop_->client_map().Activate(session_index_);
}


//implements QuicCryptoClientStream::ProofHandler
void NqClient::OnProofValid(const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(iyatomi): Handle the proof verification.
}

void NqClient::OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) {
  // TODO(iyatomi): Handle the proof verification.
}

//implements NqSession::Delegate
bool NqClient::OnOpen() { 
  return nq_closure_call(on_open_, on_conn_open, ToHandle()); 
}
void NqClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  uint64_t next_connect_us = nq::clock::to_us(nq_closure_call(on_close_, on_conn_close, ToHandle(), 
                                              (int)error, 
                                              error_details.c_str(), 
                                              close_by_peer_or_self == ConnectionCloseSource::FROM_PEER));
  //TODO: schedule reconnection somehow, if chromium stack does not do it
  if (next_connect_us > 0 && !destroyed_) {
    next_reconnect_us_ts_ = loop_->NowInUsec() + next_connect_us;
    alarm_.reset(alarm_factory()->CreateAlarm(new ReconnectAlarm(this)));
    alarm_->Set(NqLoop::ToQuicTime(next_reconnect_us_ts_));
  } else {
    alarm_.reset(alarm_factory()->CreateAlarm(this));
    alarm_->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
    //cannot touch this client memory afterward. alarm invocation automatically delete the object.
  }
  //make connection valid
  loop_->client_map().Deactivate(session_index_);
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
uint64_t NqClient::ReconnectDurationUS() const {
  auto now = loop_->NowInUsec();
  return now < next_reconnect_us_ts_ ? (next_reconnect_us_ts_ - now) : 0;
}
const nq::HandlerMap *NqClient::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : loop_->handler_map();
}

nq::HandlerMap* NqClient::ResetHandlerMap() {
  own_handler_map_.reset(new nq::HandlerMap());
  return own_handler_map_.get();
}
void NqClient::StreamManager::OnClose(NqClientStream *s) {
  auto it = map_.find(s->name_id());
  if (it == map_.end()) { 
    ASSERT(false); 
    return; 
  }
  ASSERT(it->second.streams_.size() > s->index_per_name_id());
  //invalidate
  it->second.streams_[s->index_per_name_id()] = nullptr;
}
bool NqClient::StreamManager::OnOpen(const std::string &name, NqClientStream *s) {
  s->set_protocol(name);
  s->set_name_id(Add(name));
  auto it = map_.find(s->name_id());
  if (it == map_.end()) { 
    ASSERT(false); 
    return false; 
  }
  if (it->second.streams_.size() >= 65535) {
    ASSERT(false);
    return false;
  }
  it->second.streams_.push_back(s);
  s->set_index_per_name_id(it->second.streams_.size() - 1);
  return true;
}
NqClientStream *NqClient::StreamManager::FindOrCreateStream(
      NqClientSession *session, 
      NqStreamNameId name_id, 
      NqStreamIndexPerNameId index_per_name_id,
      bool connected) {
  auto it = map_.find(name_id);
  if (it == map_.end()) { 
    ASSERT(false); 
    return nullptr; 
  }
  ASSERT(it->second.streams_.size() > index_per_name_id);
  auto s = it->second.streams_[index_per_name_id];
  if (s == nullptr && connected) {
    s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
    s->set_protocol(it->second.name_);
    s->set_name_id(name_id);
    s->set_index_per_name_id(index_per_name_id);
    s->InitHandle();
    it->second.streams_[index_per_name_id] = s;
  }
  return s;
}
    
NqClientStream *NqClient::FindOrCreateStream(NqStreamNameId name_id, NqStreamIndexPerNameId index_per_name_id) {
  return stream_manager_.FindOrCreateStream(
    nq_session(), name_id, index_per_name_id, loop_->client_map().Active(session_index_));
}
QuicStream *NqClient::NewStream(const std::string &name) {
  auto s = static_cast<NqClientStream *>(nq_session()->CreateOutgoingDynamicStream());
  if (!stream_manager_.OnOpen(name, s)) {
    return nullptr;
  }
  s->InitHandle();
  return s;
}
QuicCryptoStream *NqClient::NewCryptoStream(NqSession* session) {
  return new QuicCryptoClientStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}

}  // namespace net
