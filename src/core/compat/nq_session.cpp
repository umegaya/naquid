#include "core/compat/nq_session.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "core/nq_boxer.h"
#include "core/nq_server_session.h"
#include "core/nq_stream.h"

namespace net {

NqSession::NqSession(NqQuicConnection* connection,
                     Visitor* owner,
                     NqSessionDelegate* delegate,
                     const QuicConfig& config) : 
  QuicSession(connection, owner, config), delegate_(delegate) {
  //chromium implementation treats initial value (3) as special stream (header stream for SPDY)
  auto id = GetNextOutgoingStreamId();
  ASSERT(perspective() == Perspective::IS_SERVER || id == kHeadersStreamId);
}
NqSession::~NqSession() {
  for (auto &kv : dynamic_streams()) {
    static_cast<NqStream *>(kv.second.get())->InvalidateSerial();
  }
  if (connection() != nullptr) {
    static_cast<NqConnection*>(connection())->Cleanup();
    delete connection();
  }
}



QuicCryptoStream* NqSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}
const QuicCryptoStream* NqSession::GetCryptoStream() const {
  return crypto_stream_.get();
}



//implements QuicConnectionVisitorInterface
void NqSession::OnConnectionClosed(QuicErrorCode error,
                                const std::string& error_details,
                                ConnectionCloseSource close_by_peer_or_self) {
  QuicSession::OnConnectionClosed(error, error_details, close_by_peer_or_self);
  delegate_->OnClose(error, error_details, close_by_peer_or_self == ConnectionCloseSource::FROM_PEER);
}
void NqSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  QuicSession::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    delegate_->OnOpen();
  }
}

} //net
#endif