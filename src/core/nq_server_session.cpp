#include "core/nq_server_session.h"

#include "core/nq_server.h"
#include "core/nq_stream.h"
#include "core/nq_dispatcher.h"

namespace net {
NqServerSession::NqServerSession(NqQuicConnection *connection,
                                 const NqServer::PortConfig &port_config)
  //quic_dispatcher implements QuicSession::Visitor interface                                 
  : NqServerSessionCompat(connection, port_config),
  port_config_(port_config), own_handler_map_(), context_(nullptr) {
}
nq_conn_t NqServerSession::ToHandle() { 
  return MakeHandle<nq_conn_t, NqSessionDelegate>(static_cast<NqSessionDelegate *>(this), session_serial_);
}
std::mutex &NqServerSession::static_mutex() {
  return dispatcher()->session_allocator_body().Bss(this)->mutex();
}
NqBoxer *NqServerSession::boxer() { 
  return static_cast<NqBoxer *>(dispatcher()); 
}


// stream operation
NqStream *NqServerSession::FindStream(QuicStreamId id) {
  auto it = dynamic_streams().find(id);
  return it != dynamic_streams().end() ? static_cast<NqStream *>(it->second.get()) : nullptr;
}
NqStream *NqServerSession::FindStreamBySerial(const nq_serial_t &s, bool include_closed) {
  //TODO(iyatomi): now assume not so much stream with one session. 
  //in that case, below code probably faster because of good memory locality. 
  //but better to handle so-many-stream per session case separately
  for (auto &kv : dynamic_streams()) {
    auto st = static_cast<NqStream *>(kv.second.get());
    TRACE("FindStreamBySerial: %p(a), s = %s, sts = %s %p %p", 
      this, NqSerial::Dump(s).c_str(), st->stream_serial().Dump().c_str(), st, static_cast<NqServerStream *>(st)->context());
    if (st->stream_serial() == s) {
      return st;
    }
  }
  if (include_closed) {
    auto &closed_list = *closed_streams();
    for (auto &e : closed_list) {
      auto st = static_cast<NqStream *>(e.get());
      TRACE("FindStreamBySerial: %p(c), s = %s, sts = %s %p", this, NqSerial::Dump(s).c_str(), st->stream_serial().Dump().c_str(), st);
      if (st->stream_serial() == s) {
          return st;
      }
    }
    for (auto &kv : zombie_streams()) {
      auto st = static_cast<NqStream *>(kv.second.get());
      TRACE("FindStreamBySerial: %p(z), s = %s, sts = %s %p", this, NqSerial::Dump(s).c_str(), st->stream_serial().Dump().c_str(), st);
      if (st->stream_serial() == s) {
        return st;
      }      
    }
  }
  return nullptr;
}
void NqServerSession::InitSerial() {
  auto session_index = dispatcher()->server_map().Add(this);
  NqConnSerialCodec::ServerEncode(session_serial_, session_index);
}


//implements NqSessionDelegate
NqLoop *NqServerSession::GetLoop() { 
  return dispatcher()->loop(); 
}
void NqServerSession::OnClose(int error, const std::string& error_details, bool close_by_peer_or_self) {
  nq_error_detail_t detail = {
    .code = error,
    .msg = error_details.c_str(),
  };
  nq_closure_call(port_config_.server().on_close, ToHandle(), 
                  NQ_EQUIC, 
                  &detail, 
                  close_by_peer_or_self);
  InvalidateSerial(); //now session pointer not valid
}
void NqServerSession::OnOpen() {
  nq_closure_call(port_config_.server().on_open, ToHandle(), &context_);
}
bool NqServerSession::Reconnect() { //only supported for client 
  return false;
}
bool NqServerSession::IsClient() const {
  return false;
}
void NqServerSession::InitStream(const std::string &name, void *ctx) {
  boxer()->InvokeConn(session_serial_, this, NqBoxer::OpCode::OpenStream, name.c_str(), ctx);
}
void NqServerSession::OpenStream(const std::string &name, void *ctx) {
  auto s = NewStream();
  auto ppctx = s->ContextBuffer();
  *ppctx = ctx;
  if (!s->OpenHandler(name, true)) {
    CloseStream(s->id());
  }
}
int NqServerSession::UnderlyingFd() {
  ASSERT(false);
  return -1;
}
const nq::HandlerMap *NqServerSession::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : port_config_.handler_map();
}
nq::HandlerMap *NqServerSession::ResetHandlerMap() {
  own_handler_map_.reset(new nq::HandlerMap());
  return own_handler_map_.get();
}
} //net
