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
#include "core/nq_client_loop.h"

namespace net {

NqClient::NqClient(QuicSocketAddress server_address,
                           const QuicServerId& server_id,
                           const QuicVersionVector& supported_versions,
                           const NqClientConfig &config,
                           std::unique_ptr<ProofVerifier> proof_verifier)
  //FYI(iyatomi): loop_ should be initialized inside of 
  : QuicClientBase(
          server_id,
          supported_versions,
          config,
          new NqStubConnectionHelper(*loop_),
          new NqStubAlarmFactory(*loop_),
          QuicWrapUnique(new NqNetworkHelper(loop_, this)),
          std::move(proof_verifier)), 
          on_close_(config.client().on_close), 
          on_open_(config.client().on_open), 
          on_finalize_(config.client().on_finalize),
          session_index_(loop_->new_session_index()), 
          stream_manager_(), connect_state_(DISCONNECT),
          context_(nullptr) {
  set_server_address(server_address);
}
NqClient::~NqClient() {
  ResetSession();
}
void* NqClient::operator new(std::size_t sz) {
  ASSERT(false);
  auto r = reinterpret_cast<NqClient *>(std::malloc(sz));
  r->loop_ = nullptr;
  return r;
}
void* NqClient::operator new(std::size_t sz, NqClientLoop* l) {
  auto r = reinterpret_cast<NqClient *>(l->client_allocator().Alloc(sz));
  r->loop_ = l;
  return r;
}
void NqClient::operator delete(void *p) noexcept {
  auto r = reinterpret_cast<NqClient *>(p);
  if (r->loop_ == nullptr) {
    std::free(r);
  } else {
    r->loop_->client_allocator().Free(r);
  }
}
void NqClient::operator delete(void *p, NqClientLoop *l) noexcept {
  l->client_allocator().Free(p);
}



std::unique_ptr<QuicSession> NqClient::CreateQuicClientSession(QuicConnection* connection) {
  auto s = new NqClientSession(connection, loop_, this, *config());
  OnOpen(NQ_HS_START);
  return QuicWrapUnique(s);
}
void NqClient::InitializeSession() {
  QuicClientBase::InitializeSession();
  nq_session()->GetClientCryptoStream()->CryptoConnect();
  //make connection valid
  loop_->client_map().Activate(session_index_, this);
  connect_state_ = CONNECT;
  alarm_.reset(); //free existing reconnection alarm
}
nq_conn_t NqClient::ToHandle() { return loop_->Box(this); }


// implements QuicAlarm::Delegate
void NqClient::OnAlarm() { 
  nq_closure_call(on_finalize_, on_client_conn_finalize, ToHandle(), context_);
  loop_->RemoveClient(this); 
  alarm_.release(); //release QuicAlarm which should contain *this* pointer, to prevent double free.
  delete this;
}

//implements QuicCryptoClientStream::ProofHandler
void NqClient::OnProofValid(const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(iyatomi): Handle the proof verification.
}

void NqClient::OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) {
  // TODO(iyatomi): Handle the proof verification.
}

//implements NqSession::Delegate
NqLoop *NqClient::GetLoop() { return loop_; }
NqBoxer *NqClient::GetBoxer() { return static_cast<NqBoxer *>(loop_); }
void NqClient::OnOpen(nq_handshake_event_t hsev) { 
  if (hsev == NQ_HS_DONE) {
    stream_manager_.RecoverOutgoingStreams(nq_session());
  }
  nq_closure_call(on_open_, on_client_conn_open, ToHandle(), hsev, &context_); 
}
void NqClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  uint64_t next_connect_us = nq::clock::to_us(nq_closure_call(on_close_, on_client_conn_close, ToHandle(), 
                                              (int)error, 
                                              error_details.c_str(), 
                                              close_by_peer_or_self == ConnectionCloseSource::FROM_PEER));
  if (destroyed()) {
    alarm_.reset(alarm_factory()->CreateAlarm(this));
    alarm_->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
    //cannot touch this client memory afterward. alarm invocation automatically delete the object,
    //via auto free of pointer which holds in QuicArenaScopedPtr<QuicAlarm::Delegate> of QuicAlarm.
  } else if (next_connect_us > 0 || connect_state_ == RECONNECTING) {
    next_reconnect_us_ts_ = loop_->NowInUsec() + next_connect_us;
    alarm_.reset(alarm_factory()->CreateAlarm(new ReconnectAlarm(this)));
    alarm_->Set(NqLoop::ToQuicTime(next_reconnect_us_ts_));
    connect_state_ = RECONNECTING;
  } else {
    //client become disconnect state, but reconnection not scheduled. 
    //user program need to call nq_conn_reset to re-establish connection, 
    //or nq_conn_close to remove completely.
    connect_state_ = DISCONNECT;
  }
  //make connection invalid
  loop_->client_map().Deactivate(session_index_);
  return;
}
void NqClient::Disconnect() {
  if (connect_state_ != FINALIZED && alarm_ != nullptr) {
    alarm_->Cancel();
    alarm_.reset();
  }
  if (connect_state_ == CONNECT) {
    connect_state_ = FINALIZED;
    QuicClientBase::Disconnect(); 
  } else if (connect_state_ == DISCONNECT || connect_state_ == RECONNECTING) {
    connect_state_ = FINALIZED;
    alarm_.reset(alarm_factory()->CreateAlarm(this));
    alarm_->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
  } else {
    //finalize/reconnecting do nothing, because this client already scheduled to destroy.
    ASSERT(alarm_ != nullptr);
  }
}
bool NqClient::Reconnect() {
  if (connect_state_ == CONNECT) {
    connect_state_ = RECONNECTING;
    QuicClientBase::Disconnect(); 
  } else if (connect_state_ == DISCONNECT) {
    Initialize();
    StartConnect();    
  } else {
    //finalize do nothing, because this client already scheduled to destroy.
    ASSERT(alarm_ != nullptr);
  }
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
//this is not thread safe and only guard at nq.cpp nq_conn_rpc, nq_conn_stream.
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
  //this may race with NqClientStream *Find call
  std::unique_lock<std::mutex> lock(entries_mutex_);
  //add entry to name <=> conversion map
  for (int i = 0; i < out_entries_.size(); i++) {
    if (out_entries_[i].name_ == name) {
      return static_cast<NqStreamNameId>(i + 1);
    }
  }
  EntryGroup eg;
  eg.name_ = name;
  out_entries_.push_back(eg);
  return static_cast<NqStreamNameId>(out_entries_.size());
}
NqClient::StreamManager::Entry *NqClient::StreamManager::FindEntry(
  NqStreamNameId id, NqStreamIndexPerNameId index) const {
  std::unique_lock<std::mutex> lock(const_cast<StreamManager *>(this)->entries_mutex_);
  if (id == CLIENT_INCOMING_STREAM_NAME_ID) {
    if (index < in_entries_.size()) {
      return const_cast<StreamManager::Entry*>(&(in_entries_[index]));
    }
    ASSERT(false);
    return nullptr;
  }
  if (id > out_entries_.size()) { return nullptr; } 
  const auto &e = out_entries_[id - 1];
  if (e.streams_.size() <= index) { return nullptr; }
  return const_cast<StreamManager::Entry*>(&(e.streams_[index]));
}
void NqClient::StreamManager::RecoverOutgoingStreams(NqClientSession *session) {
    for (int i = 0; i < out_entries_.size(); i++) {
      auto name_id = (NqStreamNameId)(i + 1);
      auto &e = out_entries_[i];
      for (int j = 0; j < e.streams_.size(); j++) {
        TRACE("RecoverOutgoingStreams: create for %d %d", i, j);
        auto index_per_name_id = (NqStreamIndexPerNameId)(j);
        auto s = e.Stream(index_per_name_id);
        if (s == nullptr) {
          s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
          s->set_protocol(e.name_);
          s->set_name_id(name_id);
          s->set_index_per_name_id(index_per_name_id);
          s->InitHandle();
          //this may race with NqClientStream *Find call
          std::unique_lock<std::mutex> lock(entries_mutex_);
          e.SetStream(s);
        } else {
          TRACE("RecoverOutgoingStreams: create for %d %d: already exists %p", i, j, s);          
        }
      }
    }
}
bool NqClient::StreamManager::OnIncomingOpen(NqClientStream *s) {
  std::unique_lock<std::mutex> lock(entries_mutex_);
  NqStreamIndexPerNameId idx;
  if (in_empty_indexes_.size() > 0) {
    idx = in_empty_indexes_.top();
    in_empty_indexes_.pop();
    in_entries_[idx].SetStream(s);
  } else if (in_entries_.size() >= 65536) {
    ASSERT(false);
    return false;
  } else {
    idx = in_entries_.size();
    in_entries_.emplace_back(s);
  }
  s->set_name_id(CLIENT_INCOMING_STREAM_NAME_ID);
  s->set_index_per_name_id(idx);
  return true;
}
void NqClient::StreamManager::OnIncomingClose(NqClientStream *s) {
  std::unique_lock<std::mutex> lock(entries_mutex_);
  ASSERT(s->index_per_name_id() < in_entries_.size());
  in_entries_[s->index_per_name_id()].ClearStream();
  //pool empty index for incoming slots
  in_empty_indexes_.push(s->index_per_name_id());
}

void NqClient::StreamManager::OnOutgoingClose(NqClientStream *s) {
  auto name_id = s->name_id();
  ASSERT(name_id <= out_entries_.size());
  ASSERT(name_id != CLIENT_INCOMING_STREAM_NAME_ID);

  auto &e = out_entries_[name_id - 1]; 
  ASSERT(e.streams_.size() > s->index_per_name_id());
  //invalidate
  //this may race with FindEntry call
  std::unique_lock<std::mutex> lock(entries_mutex_);
  e.ClearStream(s);
}
bool NqClient::StreamManager::OnOutgoingOpen(const std::string &name, NqClientStream *s) {
  ASSERT((s->id() % 2) != 0); //must be incoming stream from server (server outgoing stream)
  auto name_id = Add(name);
  s->set_protocol(name);
  s->set_name_id(name_id);
  if (name_id > out_entries_.size()) { 
    return false; 
  } 
  {
    std::unique_lock<std::mutex> lock(entries_mutex_);
    auto &e = out_entries_[name_id - 1];
    if (e.streams_.size() >= 65535) {
      ASSERT(false);
      return false;
    }
    size_t sz = e.streams_.size();
    s->set_index_per_name_id(sz);
    //this may race with FindEntry call
    e.streams_.emplace_back(s);
  }
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
    if (in_entries_.size() > index_per_name_id) {
      return in_entries_[index_per_name_id].Stream();
    }
    return nullptr;
  }
  auto &e = out_entries_[name_id - 1];
  ASSERT(e.streams_.size() > index_per_name_id);
  auto s = e.streams_[index_per_name_id].Stream();
  if (s == nullptr && connected) {
    s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
    s->set_protocol(e.name_);
    s->set_name_id(name_id);
    s->set_index_per_name_id(index_per_name_id);
    s->InitHandle();
    //this may race with FindEntry call
    {
      std::unique_lock<std::mutex> lock(entries_mutex_);
      e = out_entries_[name_id - 1];
      e.SetStream(s);
    }
  }
  return s;
}
bool NqClient::StreamManager::OnOpen(const std::string &name, NqClientStream *s) {
  return ((s->id() % 2) == 0) ? OnIncomingOpen(s) : OnOutgoingOpen(name, s);
}
void NqClient::StreamManager::OnClose(NqClientStream *s) {
  if ((s->id() % 2) == 0) {
    OnIncomingClose(s);
  } else {
    OnOutgoingClose(s);
  }
}
void NqClient::StreamManager::EntryGroup::SetStream(NqClientStream *s) {
  streams_[s->index_per_name_id()].SetStream(s);
}
void NqClient::StreamManager::EntryGroup::ClearStream(NqClientStream *s) {
  streams_[s->index_per_name_id()].ClearStream();
}
NqClientStream *
NqClient::StreamManager::EntryGroup::Stream(NqStreamIndexPerNameId idx) {
  return streams_[idx].Stream();
}

}  // namespace net
