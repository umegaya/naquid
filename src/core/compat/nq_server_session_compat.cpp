#include "core/nq_server_session.h"

#include "core/nq_server.h"
#include "core/nq_stream.h"
#include "core/nq_dispatcher.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/platform/api/quic_ptr_util.h"

namespace nq {
using namespace net;
NqServerSessionCompat::NqServerSessionCompat(NqQuicConnection *connection,
                                             const NqServer::PortConfig &port_config)
  //quic_dispatcher implements QuicSession::Visitor interface                                 
  : NqSession(connection, dispatcher()->chromium(), this, port_config.chromium()) {
  SetCryptoStream(NewCryptoStream());
}


//get/set
NqDispatcher *NqServerSessionCompat::dispatcher() {
  return static_cast<NqDispatcher *>(session_allocator());
}


//operation
NqStream *NqServerSessionCompat::NewStream() {
  return static_cast<NqStream *>(CreateOutgoingDynamicStream());
}
NqStreamIndex NqServerSessionCompat::NewStreamIndex() { 
  return dispatcher()->stream_index_factory().New(); 
}


//implements NqSessionDelegate
void NqServerSessionCompat::DisconnectImpl() {
  connection()->CloseConnection(QUIC_CONNECTION_CANCELLED, "server side close", 
                                ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}
QuicStream* NqServerSessionCompat::CreateIncomingDynamicStream(QuicStreamId id) {
  auto s = new(dispatcher()) NqServerStream(id, this, false);
  s->InitSerial(NewStreamIndex());
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NqServerSessionCompat::CreateOutgoingDynamicStream() {
  auto s = new(dispatcher()) NqServerStream(GetNextOutgoingStreamId(), this, true);
  s->InitSerial(NewStreamIndex());
  ActivateStream(QuicWrapUnique(s)); //activate here. it needs to send packet normally in stream OnOpen handler
  return s;
}
QuicCryptoStream *NqServerSessionCompat::NewCryptoStream() {
  return new QuicCryptoServerStream(
    dispatcher()->crypto_config(),
    dispatcher()->cert_cache(),
    true,
    this,
    dispatcher()->chromium()
  );
}
} // namespace nq
#endif
