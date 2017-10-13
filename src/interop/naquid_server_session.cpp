#include "interop/naquie_server_session.h"

#include "net/quic/core/quic_crypto_server_stream.h"

namespace net {
NaquidServerSession::NaquidServerSession(QuicConnection *connection,
                                         const NaquidServer &server,
                                         const QuicConfig& config)
                                         : NaquidSession(connection, this, config), 
                                         server_(server) {}

//implements NaquidSession::Delegate
void NaquidServerSession::OnClose(QuicErrorCode error,
                     const std::string& error_details,
                     ConnectionCloseSource close_by_peer_or_self) {
  nq_closure_call(on_close_, on_conn_close, NaquidSession::CastFrom(this), 
                  (int)error, 
                  error_details.c_str(), 
                  close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
}
bool NaquidServerSession::OnOpen() {
  return nq_closure_call(on_open_, on_conn_open, NaquidSession::CastFrom(this));
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
  auto s = CreateOutgoingDynamicStream();
  s->set_protocol(name);
  return s;
}
QuicCryptoStream *NaquidServerSession::NewCryptoStream(NaquidSession *session) {
  return new QuicCryptoServerStream(server_id(), session,  new ProofVerifyContext(), crypto_config(), this);
}
nq::HandlerMap *NaquidServerSession::GetHandlerMap() {
  return own_hander_map_ != nullptr ? own_hander_map_.get() : server_.handler_map();
}
nq::HandlerMap *NaquidServerSession::ResetHandlerMap() {
  own_hander_map_.reset(nq::HandlerMap());
  return own_hander_map_.get();
}
} //net
