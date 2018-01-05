#include "core/nq_client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/core/quic_crypto_client_stream.h"

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
  //FYI(iyatomi): loop_ should be initialized inside of placement new
  //TODO(iyatomi): turn of warning
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
          stream_manager_(), connect_state_(DISCONNECT),
          context_(nullptr) {
  set_server_address(server_address);
}
NqClient::~NqClient() {
  ASSERT(session_serial_ == 0);
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



void NqClient::InitSerial() {
  auto session_index = loop_->client_map().Add(this);
  session_serial_ = NqConnSerialCodec::ClientEncode(session_index);
}
nq_conn_t NqClient::ToHandle() { 
  return {
    .p = static_cast<NqSession::Delegate *>(this),
    .s = session_serial_,
  };
}
std::mutex &NqClient::static_mutex() {
  return loop_->client_allocator().Bss(this)->mutex();
}
NqBoxer *NqClient::boxer() { 
  return static_cast<NqBoxer *>(loop_); 
}



// implements QuicClientBase
std::unique_ptr<QuicSession> NqClient::CreateQuicClientSession(QuicConnection* connection) {
  auto s = new NqClientSession(connection, loop_, this, *config());
  OnOpen(NQ_HS_START);
  return QuicWrapUnique(s);
}
void NqClient::InitializeSession() {
  QuicClientBase::InitializeSession();
  nq_session()->GetClientCryptoStream()->CryptoConnect();
  connect_state_ = CONNECTING;
  alarm_.reset(); //free existing reconnection alarm
}



// implements QuicAlarm::Delegate
void NqClient::OnAlarm() { 
  if (!nq_closure_is_empty(on_finalize_)) {
    nq_closure_call(on_finalize_, on_client_conn_finalize, ToHandle(), context_);
  }
  loop_->RemoveClient(this); 
  alarm_.release(); //release QuicAlarm which should contain *this* pointer, to prevent double free.
  InvalidateSerial();
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
NqLoop *NqClient::GetLoop() { 
  return loop_; 
}
void NqClient::OnOpen(nq_handshake_event_t hsev) { 
  nq_closure_call(on_open_, on_client_conn_open, ToHandle(), hsev, &context_); 
  //order is important because connect_state_ may change in above callback.
  if (hsev == NQ_HS_DONE && connect_state_ == CONNECTING) {
    connect_state_ = CONNECTED;
    stream_manager_.RecoverOutgoingStreams(nq_session());
  }
}
void NqClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  uint64_t next_connect_us = nq::clock::to_us(nq_closure_call(on_close_, on_client_conn_close, ToHandle(), 
                                              static_cast<int>(error), 
                                              error_details.c_str(), 
                                              close_by_peer_or_self == ConnectionCloseSource::FROM_PEER));
  if (destroyed()) {
    alarm_.reset(alarm_factory()->CreateAlarm(this));
    alarm_->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
    InvalidateSerial();
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
  return;
}
void NqClient::Disconnect() {
  if (connect_state_ != FINALIZED && alarm_ != nullptr) {
    alarm_->Cancel();
    alarm_.reset();
  }
  if (connect_state_ == CONNECTING || connect_state_ == CONNECTED) {
    connect_state_ = FINALIZED;
    QuicClientBase::Disconnect(); 
  } else if (connect_state_ == DISCONNECT || connect_state_ == RECONNECTING) {
    connect_state_ = FINALIZED;
    alarm_.reset(alarm_factory()->CreateAlarm(this));
    alarm_->Set(NqLoop::ToQuicTime(loop_->NowInUsec()));
  } else {
    //finalize do nothing, because this client already scheduled to destroy.
    ASSERT(alarm_ != nullptr);
  }
}
bool NqClient::Reconnect() {
  if (connect_state_ == CONNECTED || connect_state_ == CONNECTING) {
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
//TODO(iyatomi): concurrency limitation with NewStream
NqClientStream *NqClient::FindOrCreateStream(NqStreamIndex index) {
  return stream_manager_.FindOrCreateStream(nq_session(), index, connect_state_ == CONNECTED);
}
//TODO(iyatomi): concurrency limitation with FindOrCreateStream
bool NqClient::NewStream(const std::string &name, void *ctx) {
  return stream_manager_.OnOutgoingOpen(nq_session(), connect_state_ == CONNECTED, name, ctx);
}
QuicCryptoStream *NqClient::NewCryptoStream(NqSession* session) {
  return new QuicCryptoClientStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}



//ReconnectAlarm
void NqClient::ReconnectAlarm::OnAlarm() { 
  auto loop = client_->loop_;
  loop->LockSession(client_->session_index());
  auto m = &(client_->static_mutex());
  std::unique_lock<std::mutex> session_lock(*m);

  client_->Initialize();
  client_->StartConnect(); 
  //here, this already become invalid. but loop can be used

  loop->UnlockSession();  
}



//StreamManager
NqClient::StreamManager::Entry *
NqClient::StreamManager::FindEntry(NqStreamIndex index) const {
  auto it = stream_map_.find(index);
  return it != stream_map_.end() ? const_cast<Entry *>(&(it->second)) : nullptr;
}
void NqClient::StreamManager::RecoverOutgoingStreams(NqClientSession *session) {
  for (auto &kv : stream_map_) {
    auto &e = kv.second;
    TRACE("RecoverOutgoingStreams: %u %s %p", kv.first, e.name_.c_str(), e.handle_);
    if (e.name_.length() > 0) {
      auto s = e.Stream();
      if (s == nullptr) {
        s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
        s->set_stream_index(kv.first);
        s->InitSerial();
        TRACE("RecoverOutgoingStreams: serial %llx", s->stream_serial());
      }
      e.SetStream(s);
      if (!s->OpenHandler(e.name_, true)) {
        e.ClearStream();
        delete s;
      }
    }
  }
}
bool NqClient::StreamManager::OnIncomingOpen(NqClientStream *s) {
  ASSERT((s->id() % 2) == 0); //must be incoming stream from server (server outgoing stream)
  auto idx = index_seed_.New();
  stream_map_.emplace(idx, Entry(s));
  s->set_stream_index(idx);
  return true;
}
void NqClient::StreamManager::OnIncomingClose(NqClientStream *s) {
  stream_map_.erase(s->stream_index());
}

bool NqClient::StreamManager::OnOutgoingOpen(NqClientSession *session, bool connected, 
                                            const std::string &name, void *ctx) {
  auto idx = index_seed_.New();
  auto r = stream_map_.emplace(idx, Entry(nullptr, name));
  if (!r.second) {
    return false;
  } else {
    r.first->second.context_ = ctx;
    if (connected) {
      //when connected means RecoverOutgoingStreams is not called automatically in NqClient::OnOpen.
      RecoverOutgoingStreams(session);
    }
    return true;
  }
}
void NqClient::StreamManager::OnOutgoingClose(NqClientStream *s) {
  stream_map_.erase(s->stream_index());
}
NqClientStream *NqClient::StreamManager::FindOrCreateStream(
      NqClientSession *session, 
      NqStreamIndex stream_index, 
      bool connected) {
  auto it = stream_map_.find(stream_index);
  if (it != stream_map_.end()) {
    auto &e = it->second;
    //TODO(iyatomi): somehow enable this code to allow user to create 
    /*if (e.name_.length() > 0) {
      auto s = e.Stream();
      if (s == nullptr && connected) {
        s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
        s->set_stream_index(stream_index);
        s->InitSerial();
        if (s->OpenHandler(e.name_, true)) {
          e.SetStream(s);
        }
      }
    }*/
    return e.Stream();
  }
  return nullptr;
}
void NqClient::StreamManager::OnClose(NqClientStream *s) {
  if ((s->id() % 2) == 0) {
    OnIncomingClose(s);
  } else {
    OnOutgoingClose(s);
  }
}
}  // namespace net
