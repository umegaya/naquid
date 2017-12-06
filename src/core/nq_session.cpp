#include "core/nq_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_stream.h"
#include "core/nq_boxer.h"

namespace net {

NqSession::NqSession(QuicConnection* connection,
                     Visitor* owner,
                     Delegate* delegate,
                     const QuicConfig& config) : 
  QuicSession(connection, owner, config), delegate_(delegate) {
  //chromium implementation treat initial value (3) as special stream (header stream for QPDY)
  auto id = GetNextOutgoingStreamId();
  ASSERT(perspective() == Perspective::IS_SERVER || id == kHeadersStreamId);
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
  delegate_->OnClose(error, error_details, close_by_peer_or_self);
}
void NqSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  QuicSession::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    delegate_->OnOpen(NQ_HS_DONE);
  }
}

} //net
