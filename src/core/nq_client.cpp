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
          on_finalize_(config.client().on_finalize),
          session_index_(loop->new_session_index()), 
          stream_manager_(), connect_state_(DISCONNECT) {
  set_server_address(server_address);
}
NqClient::~NqClient() {
  ResetSession();
}


std::unique_ptr<QuicSession> NqClient::CreateQuicClientSession(QuicConnection* connection) {
  auto s = new NqClientSession(connection, loop_, this, *config());
  if (!OnOpen(NQ_HS_START)) {
    auto c = loop_->Box(this);
    loop_->Enqueue(new NqBoxer::Op(c.s, NqBoxer::OpCode::Disconnect));
  }
  return QuicWrapUnique(s);
}
void NqClient::InitializeSession() {
  QuicClientBase::InitializeSession();
  nq_session()->GetClientCryptoStream()->CryptoConnect();
  //make connection valid
  loop_->client_map().Activate(this);
  connect_state_ = CONNECT;
}


// implements QuicAlarm::Delegate
void NqClient::OnAlarm() { 
  nq_closure_call(on_finalize_, on_conn_finalize, ToHandle());
  loop_->RemoveClient(this); 
}

//implements QuicCryptoClientStream::ProofHandler
void NqClient::OnProofValid(const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(iyatomi): Handle the proof verification.
}

void NqClient::OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) {
  // TODO(iyatomi): Handle the proof verification.
}

//implements NqSession::Delegate
bool NqClient::OnOpen(nq_handshake_event_t hsev) { 
  return nq_closure_call(on_open_, on_conn_open, ToHandle(), hsev, nullptr); 
}
void NqClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  uint64_t next_connect_us = nq::clock::to_us(nq_closure_call(on_close_, on_conn_close, ToHandle(), 
                                              (int)error, 
                                              error_details.c_str(), 
                                              close_by_peer_or_self == ConnectionCloseSource::FROM_PEER));
  //TODO: schedule reconnection somehow, if chromium stack does not do it
  if (next_connect_us > 0 && !destroyed()) {
    next_reconnect_us_ts_ = loop_->NowInUsec() + next_connect_us;
    alarm_.reset(alarm_factory()->CreateAlarm(new ReconnectAlarm(this)));
    alarm_->Set(NqLoop::ToQuicTime(next_reconnect_us_ts_));
  } else {
    alarm_.reset(alarm_factory()->CreateAlarm(this));
    alarm_->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
    //cannot touch this client memory afterward. alarm invocation automatically delete the object.
  }
  //make connection invalid
  loop_->client_map().Deactivate(session_index_);
  connect_state_ = DISCONNECT;
  return;
}
void NqClient::Disconnect() {
  connect_state_ = FINALIZED;
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
//note that FindOrCreateStream and NewStream is never happens concurrently, because calls are limited to owner thread
NqClientStream *NqClient::FindOrCreateStream(NqStreamNameId name_id, NqStreamIndexPerNameId index_per_name_id) {
  return stream_manager_.FindOrCreateStream(
    nq_session(), name_id, index_per_name_id, connect_state_ == CONNECT);
}
QuicStream *NqClient::NewStream(const std::string &name) {
  auto s = static_cast<NqClientStream *>(nq_session()->CreateOutgoingDynamicStream());
  if (!stream_manager_.OnOpen(name, s)) {
    delete s;
    return nullptr;
  }
  s->InitHandle();
  return s;
}
QuicCryptoStream *NqClient::NewCryptoStream(NqSession* session) {
  return new QuicCryptoClientStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}



//StreamManager
NqStreamNameId NqClient::StreamManager::Add(const std::string &name) {
  //add entry to name <=> conversion map
  for (int i = 0; i < out_entries_.size(); i++) {
    if (out_entries_[i].name_ == name) {
      return static_cast<NqStreamNameId>(i + 1);
    }
  }
  Entry e;
  e.name_ = name;
  {
    //this may race with NqClientStream *Find call
    std::unique_lock<std::mutex> lock(entries_mutex_);
    out_entries_.push_back(e);
  }
  return static_cast<NqStreamNameId>(out_entries_.size());
}
const NqClientStream *NqClient::StreamManager::Find(
  NqStreamNameId id, NqStreamIndexPerNameId index) const {
  std::unique_lock<std::mutex> lock(const_cast<StreamManager *>(this)->entries_mutex_);
  if (id == CLIENT_INCOMING_STREAM_NAME_ID) {
    if (index < in_entries_.size()) {
      return in_entries_[index];
    }
    ASSERT(false);
    return nullptr;
  }
  if (id > out_entries_.size()) { return nullptr; } 
  const auto &e = out_entries_[id - 1];
  if (e.streams_.size() <= index) { return nullptr; }
  return e.streams_[index]; 
}
bool NqClient::StreamManager::OnIncomingOpen(NqClientStream *s) {
  std::unique_lock<std::mutex> lock(entries_mutex_);
  NqStreamIndexPerNameId idx;
  if (in_empty_indexes_.size() > 0) {
    idx = in_empty_indexes_.top();
    in_empty_indexes_.pop();
    in_entries_[idx] = s;
  } else if (in_entries_.size() >= 65536) {
    ASSERT(false);
    return false;
  } else {
    idx = in_entries_.size();
    in_entries_.push_back(s);
  }
  s->set_name_id(CLIENT_INCOMING_STREAM_NAME_ID);
  s->set_index_per_name_id(idx);
  return true;
}
void NqClient::StreamManager::OnIncomingClose(NqClientStream *s) {
  std::unique_lock<std::mutex> lock(entries_mutex_);
  if (s->index_per_name_id() >= in_entries_.size()) {
    ASSERT(false);
    return;
  }
  in_entries_[s->index_per_name_id()] = nullptr;
  //pool empty index for incoming slots
  in_empty_indexes_.push(s->index_per_name_id());
}

void NqClient::StreamManager::OnOutgoingClose(NqClientStream *s) {
  auto name_id = s->name_id();
  if (name_id > out_entries_.size()) { 
    ASSERT(false);
    return; 
  } else if (name_id == CLIENT_INCOMING_STREAM_NAME_ID) {
    ASSERT(false);
    return;
  }
  auto &e = out_entries_[name_id - 1]; 
  ASSERT(e.streams_.size() > s->index_per_name_id());
  //invalidate
  //this may race with NqClientStream *Find call
  std::unique_lock<std::mutex> lock(entries_mutex_);
  e.streams_[s->index_per_name_id()] = nullptr;
}
bool NqClient::StreamManager::OnOutgoingOpen(const std::string &name, NqClientStream *s) {
  ASSERT((s->id() % 2) != 0); //must be incoming stream from server (server outgoing stream)
  s->set_protocol(name);
  auto name_id = Add(name);
  s->set_name_id(name_id);
  if (name_id > out_entries_.size()) { 
    return false; 
  } 
  auto &e = out_entries_[name_id - 1];
  if (e.streams_.size() >= 65535) {
    ASSERT(false);
    return false;
  }
  size_t sz = e.streams_.size();
  s->set_index_per_name_id(sz);
  //this may race with NqClientStream *Find call
  std::unique_lock<std::mutex> lock(entries_mutex_);
  e.streams_.push_back(s);
  return true;
}
NqClientStream *NqClient::StreamManager::FindOrCreateStream(
      NqClientSession *session, 
      NqStreamNameId name_id, 
      NqStreamIndexPerNameId index_per_name_id,
      bool connected) {
  if (name_id > out_entries_.size()) { 
    return nullptr; 
  } else if (name_id == CLIENT_INCOMING_STREAM_NAME_ID) {
    if (in_entries_.size() <= index_per_name_id) {
      return in_entries_[index_per_name_id];
    }
    return nullptr;
  }
  auto &e = out_entries_[name_id - 1];
  ASSERT(e.streams_.size() > index_per_name_id);
  auto s = e.streams_[index_per_name_id];
  if (s == nullptr && connected) {
    s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
    s->set_protocol(e.name_);
    s->set_name_id(name_id);
    s->set_index_per_name_id(index_per_name_id);
    s->InitHandle();
    //this may race with NqClientStream *Find call
    std::unique_lock<std::mutex> lock(entries_mutex_);
    e.streams_[index_per_name_id] = s;
  }
  return s;
}
}  // namespace net
