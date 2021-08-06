#include "core/nq_client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "basis/timespec.h"
#include "core/nq_stream.h"
#include "core/compat/chromium/nq_network_helper.h"
#include "core/compat/chromium/nq_stub_interface.h"
#include "core/nq_client_loop.h"

#include "core/platform/nq_reachability.h"

namespace net {

NqClientBase::NqClientBase(NqQuicSocketAddress server_address,
                   NqClientLoop &loop,
                   const NqQuicServerId &server_id,
                   const NqClientConfig &config)
  : loop_(&loop), on_close_(config.client().on_close),
    on_open_(config.client().on_open),
    on_finalize_(config.client().on_finalize),
    stream_manager_(), connect_state_(DISCONNECT),
    context_(nullptr), reachability_(nullptr) {
}
NqClientBase::~NqClientBase() {
  ASSERT(session_serial_.IsEmpty());
}


NqClient *NqClientBase::nq_client() {
  // hack:
  // NqClientBase is abstract, and constructor of NqClientCompat can only be called from NqClient.
  // so we can assume `this` should be NqClient*
  return static_cast<NqClient *>(this);
}


void NqClientBase::InitSerial() {
  auto session_index = loop_->client_map().Add(nq_client());
  NqConnSerialCodec::ClientEncode(session_serial_, session_index);
}
NqStreamIndex NqClientBase::NewStreamIndex() {
  return client_loop()->stream_index_factory().New(); 
}
void NqClientBase::OnFinalize() {
  if (!nq_closure_is_empty(on_finalize_)) {
    nq_closure_call(on_finalize_, ToHandle(), context_);
    on_finalize_ = nq_closure_empty();
  }
  if (reachability_ != nullptr) {
    NqReachability::Destroy(reachability_);
    reachability_ = nullptr;
  }
}
void NqClientBase::OnReachabilityChangeTranpoline(void *self, nq_reachability_t state) {
  auto cl = ((NqClientBase *)self);
  //let process in main thread of this client
  cl->boxer()->InvokeConn(cl->SessionSerial(), cl, NqBoxer::OpCode::Reachability, state);
}
void NqClientBase::OnReachabilityChange(nq_reachability_t state) {
  TRACE("OnReachabilityChange to %u", state);
  switch (state) {
    case NQ_NOT_REACHABLE:
      break; //soon closed 
    case NQ_REACHABLE_WIFI:
    case NQ_REACHABLE_WWAN:
      //try migrate to new network
      if (!MigrateSocket()) {
        TRACE("fail to migrate socket");
        Reconnect(); //give up migrating and re-establish
      }
      break;
    default:
      ASSERT(false);
      break;
  }  
}
bool NqClientBase::TrackReachability(const std::string &host) {
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
void NqClientBase::DoReconnect() {
  Initialize();
  StartConnect(); 
}
void NqClientBase::ScheduleDestroy() {
  OnFinalize();
  loop_->RemoveClient(nq_client());
  InvalidateSerial();
  boxer()->InvokeConn(session_serial_, this, NqBoxer::OpCode::Finalize);
}
void NqClientBase::Destroy() {
  OnFinalize();
  ForceShutdown();
  loop_->RemoveClient(nq_client());
  InvalidateSerial();
  delete this;
}
nq_conn_t NqClientBase::ToHandle() { 
  return MakeHandle<nq_conn_t, NqSessionDelegate>(static_cast<NqSessionDelegate *>(this), session_serial_);
}
std::mutex &NqClientBase::static_mutex() {
  return loop_->client_allocator().Bss(this)->mutex();
}
NqBoxer *NqClientBase::boxer() { 
  return static_cast<NqBoxer *>(loop_); 
}


// implements NqAlarmBase
void NqClientBase::OnFire() { 
  ASSERT(connect_state_ == RECONNECTING);
  boxer()->InvokeConn(session_serial_, this, NqBoxer::OpCode::DoReconnect);
  ClearInvocationTS();
}


//implements NqSessionDelegate
NqLoop *NqClientBase::GetLoop() { 
  return loop_; 
}
void NqClientBase::OnOpen() { 
  nq_closure_call(on_open_, ToHandle(), &context_); 
  //order is important because connect_state_ may change in above callback.
  if (connect_state_ == CONNECTING) {
    connect_state_ = CONNECTED;
    stream_manager_.RecoverOutgoingStreams(this);
  }
}
void NqClientBase::OnClose(int error, const std::string& error_details, bool close_by_peer_or_self) {
  nq_error_detail_t detail = {
    .code = error,
    .msg = error_details.c_str(),
  };
  uint64_t next_connect_us = nq::clock::to_us(nq_closure_call(on_close_, ToHandle(), 
                                              NQ_EQUIC, 
                                              &detail, 
                                              close_by_peer_or_self));
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
void NqClientBase::Disconnect() {
  if (connect_state_ == CONNECTING || connect_state_ == CONNECTED) {
    connect_state_ = FINALIZED;
    StartDisconnect();
  } else if (connect_state_ == DISCONNECT || connect_state_ == RECONNECTING) {
    connect_state_ = FINALIZED;
    ScheduleDestroy();
  } else {
    //finalize do nothing, because this client already scheduled to destroy.
  }
}
bool NqClientBase::Reconnect() {
  if (connect_state_ == CONNECTED || connect_state_ == CONNECTING) {
    connect_state_ = RECONNECTING;
    StartDisconnect();
  } else if (connect_state_ == DISCONNECT) {
    DoReconnect();
  } else {
    //finalize/reconnecting do nothing, because this client already scheduled to destroy.
  }
  return true;
}
uint64_t NqClientBase::ReconnectDurationUS() const {
  auto now = loop_->NowInUsec();
  return now < next_reconnect_us_ts_ ? (next_reconnect_us_ts_ - now) : 0;
}
const nq::HandlerMap *NqClientBase::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : loop_->handler_map();
}
nq::HandlerMap* NqClientBase::ResetHandlerMap() {
  own_handler_map_.reset(new nq::HandlerMap());
  return own_handler_map_.get();
}
NqClientStream *NqClientBase::FindOrCreateStream(NqStreamIndex index) {
  return stream_manager_.FindOrCreateStream(this, index, connect_state_ == CONNECTED);
}
void NqClientBase::InitStream(const std::string &name, void *ctx) {
  TRACE("NqClientBase::InitStream");
  stream_manager_.OnOutgoingOpen(this, connect_state_ == CONNECTED, name, ctx);
}
void NqClientBase::OpenStream(const std::string &, void *) {
  stream_manager_.RecoverOutgoingStreams(this);
}


//StreamManager
NqClientBase::StreamManager::Entry *
NqClientBase::StreamManager::FindEntry(NqStreamIndex index) {
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
void NqClientBase::StreamManager::RecoverOutgoingStreams(NqClientBase *client) {
for (auto &kv : entries_) {
    auto &e = kv.second; 
    TRACE("RecoverOutgoingStreams: %u %s %p", kv.first, e.name_.c_str(), e.handle_);
    if (e.name_.length() > 0) {
      auto s = e.Stream();
      if (s == nullptr) {
        s = client->NewStream();
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
void NqClientBase::StreamManager::CleanupStreamsOnClose() {
  entries_.clear(); //after that entries_.erase may called.
}
NqStreamIndex NqClientBase::StreamManager::OnIncomingOpen(NqClientBase *client, NqClientStream *s) {
  ASSERT((s->id() % 2) == 0); //must be incoming stream from server (server outgoing stream)
  auto idx = client->NewStreamIndex();
  entries_.emplace(idx, Entry(s));
  return idx;
}
void NqClientBase::StreamManager::OnIncomingClose(NqClientStream *s) {
  entries_.erase(NqStreamSerialCodec::ClientStreamIndex(s->stream_serial()));
}

bool NqClientBase::StreamManager::OnOutgoingOpen(NqClientBase *client, bool connected,
                                            const std::string &name, void *ctx) {
  auto idx = client->NewStreamIndex();
  auto r = entries_.emplace(idx, Entry(nullptr, name));
  if (!r.second) {
    return false;
  } else {
    r.first->second.context_ = ctx;
    if (connected) {
      //when connected means RecoverOutgoingStreams is not called automatically in NqClientBase::OnOpen.
      client->boxer()->InvokeConn(client->session_serial_, client, NqBoxer::OpCode::OpenStream, name.c_str(), ctx);
    }
    return true;
  }
}
void NqClientBase::StreamManager::OnOutgoingClose(NqClientStream *s) {
  OnIncomingClose(s);
}
NqClientStream *NqClientBase::StreamManager::FindOrCreateStream(
      NqClientBase *client, 
      NqStreamIndex stream_index, 
      bool connected) {
  auto e = FindEntry(stream_index);
  if (e != nullptr) {
    /*if (e.name_.length() > 0) {
      auto s = e.Stream();
      if (s == nullptr && connected) {
        s = client->NewStream();
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
void NqClientBase::StreamManager::OnClose(NqClientStream *s) {
  if ((s->id() % 2) == 0) {
    OnIncomingClose(s);
  } else {
    OnOutgoingClose(s);
  }
}
}  // namespace net
