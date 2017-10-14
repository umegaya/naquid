#include "interop/naquid_server_session.h"

#include "net/quic/core/quic_crypto_server_stream.h"

#include "interop/naquid_server.h"
#include "interop/naquid_stream.h"
#include "interop/naquid_dispatcher.h"

namespace net {
NaquidServerSession::NaquidServerSession(QuicConnection *connection,
                                         NaquidDispatcher *dispatcher,
                                         const NaquidServer::PortConfig &port_config)
                                         : NaquidSession(connection, this, port_config), 
                                         dispatcher_(dispatcher),
                                         port_config_(port_config) {}

//implements NaquidSession::Delegate
void NaquidServerSession::OnClose(QuicErrorCode error,
                     const std::string& error_details,
                     ConnectionCloseSource close_by_peer_or_self) {
  nq_closure_call(port_config_.server().on_close, on_conn_close, NaquidSession::CastFrom(this), 
                  (int)error, 
                  error_details.c_str(), 
                  close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
}
bool NaquidServerSession::OnOpen() {
  return nq_closure_call(port_config_.server().on_open, on_conn_open, NaquidSession::CastFrom(this));
}
void NaquidServerSession::Disconnect() {
  connection()->CloseConnection(QUIC_CONNECTION_CANCELLED, "server side close", 
                                ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}
bool NaquidServerSession::Reconnect() { //only supported for client 
  return false;
}
bool NaquidServerSession::IsClient() {
  return false;
}
QuicStream *NaquidServerSession::NewStream(const std::string &name) {
  auto s = reinterpret_cast<NaquidStream *>(CreateOutgoingDynamicStream());
  s->set_protocol(name);
  return s;
}
QuicCryptoStream *NaquidServerSession::NewCryptoStream(NaquidSession *session) {
  return new QuicCryptoServerStream(
    port_config_.crypto(),
    dispatcher_->cert_cache(),
    true,
    session,
    dispatcher_
  );
}
const nq::HandlerMap *NaquidServerSession::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : port_config_.handler_map();
}
nq::HandlerMap *NaquidServerSession::ResetHandlerMap() {
  own_handler_map_.reset(new nq::HandlerMap());
  return own_handler_map_.get();
}
} //net
