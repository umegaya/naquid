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
#include "core/compat/nq_client_loop.h"

#include "core/platform/nq_reachability.h"

namespace net {

NqClient::NqClient(NqQuicSocketAddress server_address,
                   const NqQuicServerId& server_id,
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
          context_(nullptr), reachability_(nullptr) {
  set_server_address(server_address);
}
NqClient::~NqClient() {
  ASSERT(session_serial_.IsEmpty());
}
void* NqClient::operator new(std::size_t sz) {
  ASSERT(false);
  auto r = reinterpret_cast<NqClient *>(std::malloc(sz));
  r->loop_ = nullptr;
  return r;
}
void* NqClient::operator new(std::size_t sz, NqClientLoop *l) {
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
  NqConnSerialCodec::ClientEncode(session_serial_, session_index);
}
NqStreamIndex NqClient::NewStreamIndex() {
  return client_loop()->stream_index_factory().New(); 
}
void NqClient::OnFinalize() {
  if (!nq_closure_is_empty(on_finalize_)) {
    nq_closure_call(on_finalize_, ToHandle(), context_);
    on_finalize_ = nq_closure_empty();
  }
  if (reachability_ != nullptr) {
    NqReachability::Destroy(reachability_);
    reachability_ = nullptr;
  }
}
void NqClient::ForceShutdown() {
  if (connected()) {
    session()->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Client being torn down",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }
}
void NqClient::OnReachabilityChangeTranpoline(void *self, nq_reachability_t state) {
  auto cl = ((NqClient *)self);
  //let process in main thread of this client
  cl->boxer()->InvokeConn(cl->SessionSerial(), cl, NqBoxer::OpCode::Reachability, state);
}
void NqClient::OnReachabilityChange(nq_reachability_t state) {
  TRACE("OnReachabilityChange to %u", state);
  switch (state) {
    case NQ_NOT_REACHABLE:
      break; //soon closed 
    case NQ_REACHABLE_WIFI:
    case NQ_REACHABLE_WWAN:
      //try migrate to new network
      if (!MigrateSocket(bind_to_address())) {
        TRACE("fail to migrate socket");
        Reconnect(); //give up migrating and re-establish
      }
      break;
    default:
      ASSERT(false);
      break;
  }  
}
bool NqClient::TrackReachability(const std::string &host) {
  if (reachability_ != nullptr) {
    return true;
  }
  nq_on_reachability_change_t observer;
  nq_closure_init(observer, OnReachabilityChangeTranpoline, this);
  reachability_ = NqReachability::Create(observer);

  bool ret = reachability_->Start(host);
  if (!ret) {
    NqReachability::Destroy(reachability_);
    reachability_ = nullptr;
  }
  return ret;
}
void NqClient::DoReconnect() {
  Initialize();
  StartConnect(); 
}
void NqClient::ScheduleDestroy() {
  OnFinalize();
  loop_->RemoveClient(this);
  InvalidateSerial();
  boxer()->InvokeConn(session_serial_, this, NqBoxer::OpCode::Finalize);
}
void NqClient::Destroy() {
  OnFinalize();
  ForceShutdown();
  loop_->RemoveClient(this);
  InvalidateSerial();
  delete this;
}
nq_conn_t NqClient::ToHandle() { 
  return MakeHandle<nq_conn_t, NqSession::Delegate>(static_cast<NqSession::Delegate *>(this), session_serial_);
}
std::mutex &NqClient::static_mutex() {
  return loop_->client_allocator().Bss(this)->mutex();
}
NqBoxer *NqClient::boxer() { 
  return static_cast<NqBoxer *>(loop_); 
}



// implements QuicClientBase
std::unique_ptr<QuicSession> NqClient::CreateQuicClientSession(QuicConnection* connection) {
  return QuicWrapUnique(new NqClientSession(connection, loop_, this, *config()));
}
void NqClient::InitializeSession() {
  connect_state_ = CONNECTING;
  QuicClientBase::InitializeSession();
  nq_session()->GetClientCryptoStream()->CryptoConnect();
}



// implements NqAlarmBase
void NqClient::OnFire() { 
  ASSERT(connect_state_ == RECONNECTING);
  boxer()->InvokeConn(session_serial_, this, NqBoxer::OpCode::DoReconnect);
  ClearInvocationTS();
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
void NqClient::OnOpen() { 
  nq_closure_call(on_open_, ToHandle(), &context_); 
  //order is important because connect_state_ may change in above callback.
  if (connect_state_ == CONNECTING) {
    connect_state_ = CONNECTED;
    stream_manager_.RecoverOutgoingStreams(nq_session());
  }
}
void NqClient::OnClose(QuicErrorCode error,
             const std::string& error_details,
             ConnectionCloseSource close_by_peer_or_self) {
  nq_error_detail_t detail = {
    .code = error,
    .msg = error_details.c_str(),
  };
  uint64_t next_connect_us = nq::clock::to_us(nq_closure_call(on_close_, ToHandle(), 
                                              NQ_EQUIC, 
                                              &detail, 
                                              close_by_peer_or_self == ConnectionCloseSource::FROM_PEER));
  if (destroyed()) {
    ScheduleDestroy();
    //cannot touch this client memory afterward. alarm invocation automatically delete the object,
    //via auto free of pointer which holds in QuicArenaScopedPtr<QuicAlarm::Delegate> of QuicAlarm.
  } else if (next_connect_us > 0 || connect_state_ == RECONNECTING) {
    next_reconnect_us_ts_ = loop_->NowInUsec() + next_connect_us;
    TRACE("set reconnection alarm: to %llu(%llu)", nq_time_usec(next_reconnect_us_ts_), nq_time_usec(next_connect_us));
    NqAlarmBase::Start(loop_, nq_time_usec(next_reconnect_us_ts_));
    connect_state_ = RECONNECTING;
  } else {
    //client become disconnect state, but reconnection not scheduled. 
    //user program need to call nq_conn_reset to re-establish connection, 
    //or nq_conn_close to remove completely.
    connect_state_ = DISCONNECT;
  }
  //remove non established outgoing streams.
  stream_manager_.CleanupStreamsOnClose();  
  return;
}
void NqClient::Disconnect() {
  if (connect_state_ == CONNECTING || connect_state_ == CONNECTED) {
    connect_state_ = FINALIZED;
    QuicClientBase::Disconnect(); 
  } else if (connect_state_ == DISCONNECT || connect_state_ == RECONNECTING) {
    connect_state_ = FINALIZED;
    ScheduleDestroy();
  } else {
    //finalize do nothing, because this client already scheduled to destroy.
  }
}
bool NqClient::Reconnect() {
  if (connect_state_ == CONNECTED || connect_state_ == CONNECTING) {
    connect_state_ = RECONNECTING;
    QuicClientBase::Disconnect(); 
  } else if (connect_state_ == DISCONNECT) {
    DoReconnect();
  } else {
    //finalize/reconnecting do nothing, because this client already scheduled to destroy.
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
NqClientStream *NqClient::FindOrCreateStream(NqStreamIndex index) {
  return stream_manager_.FindOrCreateStream(nq_session(), index, connect_state_ == CONNECTED);
}
void NqClient::InitStream(const std::string &name, void *ctx) {
  TRACE("NqClient::InitStream");
  stream_manager_.OnOutgoingOpen(this, connect_state_ == CONNECTED, name, ctx);
}
void NqClient::OpenStream(const std::string &, void *) {
  stream_manager_.RecoverOutgoingStreams(nq_session());
}
QuicCryptoStream *NqClient::NewCryptoStream(NqSession* session) {
  return new QuicCryptoClientStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}



//StreamManager
NqClient::StreamManager::Entry *
NqClient::StreamManager::FindEntry(NqStreamIndex index) {
  auto it = entries_.find(index);
#if defined(DEBUG)
  if (it != entries_.end()) {
    return const_cast<Entry *>(&(it->second));
  } else {
    TRACE("FindEntry NOT found for %u(%lu)\n", index, entries_.size());
    for (auto &kv : entries_) {
      TRACE("entry %u %p %p\n", kv.first, kv.second.handle_, kv.second.context_);
    } 
    return nullptr;
  }
#else
  return it != entries_.end() ? const_cast<Entry *>(&(it->second)) : nullptr;
#endif
}
void NqClient::StreamManager::RecoverOutgoingStreams(NqClientSession *session) {
for (auto &kv : entries_) {
    auto &e = kv.second; 
    TRACE("RecoverOutgoingStreams: %u %s %p", kv.first, e.name_.c_str(), e.handle_);
    if (e.name_.length() > 0) {
      auto s = e.Stream();
      if (s == nullptr) {
        s = static_cast<NqClientStream *>(session->CreateOutgoingDynamicStream());
        s->InitSerial(kv.first);
        TRACE("RecoverOutgoingStreams: serial %llx", s->stream_serial());
      }
      ASSERT(s != nullptr);
      e.SetStream(s);
      if (!s->OpenHandler(e.name_, true)) {
        e.ClearStream();
        delete s;
      }
    }
  }
}
void NqClient::StreamManager::CleanupStreamsOnClose() {
  entries_.clear(); //after that entries_.erase may called.
}
NqStreamIndex NqClient::StreamManager::OnIncomingOpen(NqClient *client, NqClientStream *s) {
  ASSERT((s->id() % 2) == 0); //must be incoming stream from server (server outgoing stream)
  auto idx = client->NewStreamIndex();
  entries_.emplace(idx, Entry(s));
  return idx;
}
void NqClient::StreamManager::OnIncomingClose(NqClientStream *s) {
  entries_.erase(NqStreamSerialCodec::ClientStreamIndex(s->stream_serial()));
}

bool NqClient::StreamManager::OnOutgoingOpen(NqClient *client, bool connected,
                                            const std::string &name, void *ctx) {
  auto idx = client->NewStreamIndex();
  auto r = entries_.emplace(idx, Entry(nullptr, name));
  if (!r.second) {
    return false;
  } else {
    r.first->second.context_ = ctx;
    if (connected) {
      //when connected means RecoverOutgoingStreams is not called automatically in NqClient::OnOpen.
      client->boxer()->InvokeConn(client->session_serial_, client, NqBoxer::OpCode::OpenStream, name.c_str(), ctx);
    }
    return true;
  }
}
void NqClient::StreamManager::OnOutgoingClose(NqClientStream *s) {
  OnIncomingClose(s);
}
NqClientStream *NqClient::StreamManager::FindOrCreateStream(
      NqClientSession *session, 
      NqStreamIndex stream_index, 
      bool connected) {
  auto e = FindEntry(stream_index);
  if (e != nullptr) {
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
    return e->Stream();
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
