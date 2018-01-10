#include "core/nq_server_session.h"

#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_server.h"
#include "core/nq_stream.h"
#include "core/nq_dispatcher.h"

namespace net {
NqServerSession::NqServerSession(QuicConnection *connection,
                                 const NqServer::PortConfig &port_config)
  : NqSession(connection, dispatcher(), this, port_config), //dispatcher implements QuicSession::Visitor interface
  port_config_(port_config), own_handler_map_(), index_factory_(), context_(nullptr) {
  init_crypto_stream();
}
nq_conn_t NqServerSession::ToHandle() { 
  return {
    .p = static_cast<NqSession::Delegate *>(this),
    .s = session_serial_,
  }; 
}
std::mutex &NqServerSession::static_mutex() {
  return dispatcher()->session_allocator_body().Bss(this)->mutex();
}
NqBoxer *NqServerSession::boxer() { 
  return static_cast<NqBoxer *>(dispatcher()); 
}
NqDispatcher *NqServerSession::dispatcher() {
  return static_cast<NqDispatcher *>(session_allocator());
}



NqStream *NqServerSession::FindStream(QuicStreamId id) {
  auto it = dynamic_streams().find(id);
  return it != dynamic_streams().end() ? static_cast<NqStream *>(it->second.get()) : nullptr;
}
NqStream *NqServerSession::FindStreamBySerial(uint64_t s, bool include_closed) {
  //TODO(iyatomi): now assume not so much stream with one session. 
  //in that case, below code probably faster because of good memory locality. 
  //but better to handle so-many-stream per session case separately
  for (auto &kv : dynamic_streams()) {
    auto st = static_cast<NqStream *>(kv.second.get());
    TRACE("FindStreamBySerial: %p(a), s = %llx, sts = %llx %p %p", this, s, st->stream_serial(), st, static_cast<NqServerStream *>(st)->context());
    if (st->stream_serial() == s) {
      return st;
    }
  }
  if (include_closed) {
    auto &closed_list = *closed_streams();
    for (auto &e : closed_list) {
      auto st = static_cast<NqStream *>(e.get());
      TRACE("FindStreamBySerial: %p(c), s = %llx, sts = %llx %p", this, s, st->stream_serial(), st);
      if (st->stream_serial() == s) {
        return st;
      }
    }
    for (auto &kv : zombie_streams()) {
      auto st = static_cast<NqStream *>(kv.second.get());
      TRACE("FindStreamBySerial: %p(z), s = %llx, sts = %llx %p", this, s, st->stream_serial(), st);
      if (st->stream_serial() == s) {
        return st;
      }      
    }
  }
  return nullptr;
}
void NqServerSession::InitSerial() {
  auto session_index = dispatcher()->server_map().Add(this);
  session_serial_ = NqConnSerialCodec::ServerEncode(session_index, connection_id(), dispatcher()->worker_num());
}



//implements NqSession::Delegate
NqLoop *NqServerSession::GetLoop() { 
  return dispatcher()->loop(); 
}
void *NqServerSession::StreamContext(uint64_t stream_serial) const {
  auto s = const_cast<NqServerSession *>(this)->FindStreamBySerial(stream_serial, true);
  if (s != nullptr) {
    return static_cast<NqServerStream *>(s)->context();
  } else {
    return nullptr;
  }
}
void NqServerSession::OnClose(QuicErrorCode error,
                     const std::string& error_details,
                     ConnectionCloseSource close_by_peer_or_self) {
  nq_closure_call(port_config_.server().on_close, on_server_conn_close, ToHandle(), 
                  (int)error, 
                  error_details.c_str(), 
                  close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
  InvalidateSerial(); //now session pointer not valid
}
void NqServerSession::OnOpen(nq_handshake_event_t hsev) {
  nq_closure_call(port_config_.server().on_open, on_server_conn_open, ToHandle(), hsev, &context_);
}
void NqServerSession::Disconnect() {
  connection()->CloseConnection(QUIC_CONNECTION_CANCELLED, "server side close", 
                                ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}
bool NqServerSession::Reconnect() { //only supported for client 
  return false;
}
bool NqServerSession::IsClient() const {
  return false;
}
QuicStream* NqServerSession::CreateIncomingDynamicStream(QuicStreamId id) {
  auto s = new(dispatcher()) NqServerStream(id, this, false);
  s->InitSerial(index_factory().New());
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NqServerSession::CreateOutgoingDynamicStream() {
  auto s = new(dispatcher()) NqServerStream(GetNextOutgoingStreamId(), this, true);
  s->InitSerial(index_factory().New());
  ActivateStream(QuicWrapUnique(s)); //activate here. it needs to send packet normally in stream OnOpen handler
  return s;
}
void NqServerSession::InitStream(const std::string &name, void *ctx) {
  boxer()->InvokeConn(session_serial_, NqBoxer::OpCode::OpenStream, this, name.c_str(), ctx);
}
void NqServerSession::OpenStream(const std::string &name, void *ctx) {
  auto s = reinterpret_cast<NqStream *>(CreateOutgoingDynamicStream());
  auto ppctx = s->ContextBuffer();
  *ppctx = ctx;
  if (!s->OpenHandler(name, true)) {
    CloseStream(s->id());
  }
}
QuicCryptoStream *NqServerSession::NewCryptoStream(NqSession *session) {
  return new QuicCryptoServerStream(
    dispatcher()->crypto_config(),
    dispatcher()->cert_cache(),
    true,
    session,
    dispatcher()
  );
}
const nq::HandlerMap *NqServerSession::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : port_config_.handler_map();
}
nq::HandlerMap *NqServerSession::ResetHandlerMap() {
  own_handler_map_.reset(new nq::HandlerMap());
  return own_handler_map_.get();
}
} //net
