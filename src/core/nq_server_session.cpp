#include "core/nq_server_session.h"

#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_server.h"
#include "core/nq_stream.h"
#include "core/nq_dispatcher.h"

namespace net {
NqServerSession::NqServerSession(QuicConnection *connection,
                                         NqDispatcher *dispatcher,
                                         const NqServer::PortConfig &port_config)
                                         : NqSession(connection, this, port_config), 
                                         dispatcher_(dispatcher),
                                         port_config_(port_config),
                                         session_index_(dispatcher->new_session_index()) {}

NqStream *NqServerSession::FindStream(QuicStreamId id) {
  auto it = dynamic_streams().find(id);
  return it != dynamic_streams().end() ? static_cast<NqStream *>(it->second.get()) : nullptr;
}

//implements NqSession::Delegate
void NqServerSession::OnClose(QuicErrorCode error,
                     const std::string& error_details,
                     ConnectionCloseSource close_by_peer_or_self) {
  nq_closure_call(port_config_.server().on_close, on_conn_close, ToHandle(), 
                  (int)error, 
                  error_details.c_str(), 
                  close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
}
bool NqServerSession::OnOpen() {
  return nq_closure_call(port_config_.server().on_open, on_conn_open, ToHandle());
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
  auto s = new NqServerStream(id, this, false);
  s->InitHandle();
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NqServerSession::CreateOutgoingDynamicStream() {
  auto s = new NqServerStream(GetNextOutgoingStreamId(), this, true);
  s->InitHandle();
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream *NqServerSession::NewStream(const std::string &name) {
  auto s = reinterpret_cast<NqStream *>(CreateOutgoingDynamicStream());
  s->set_protocol(name);
  return s;
}
QuicCryptoStream *NqServerSession::NewCryptoStream(NqSession *session) {
  return new QuicCryptoServerStream(
    port_config_.crypto(),
    dispatcher_->cert_cache(),
    true,
    session,
    dispatcher_
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
