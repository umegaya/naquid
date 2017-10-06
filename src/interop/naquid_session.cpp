#include "interop/naquid_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

#include "interop/naquid_stream.h"

namespace net {

NaquidSession::NaquidSession(QuicConnection* connection,
                                 Delegate* delegate,
                                 const QuicConfig& config) : 
  QuicSession(connection, nullptr, config), delegate_(delegate) {
  hdmap_ = delegate_->GetHandlerMap();
  crypto_stream_.reset(delegate_->NewCryptoStream(this));
}
QuicStream* NaquidSession::CreateIncomingDynamicStream(QuicStreamId id) {
  auto s = new NaquidStream(id, this, false);
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NaquidSession::CreateOutgoingDynamicStream() {
  auto s = new NaquidStream(GetNextOutgoingStreamId(), this, true);
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicCryptoStream* NaquidSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}
const QuicCryptoStream* NaquidSession::GetCryptoStream() const {
  return crypto_stream_.get();
}

//implements QuicConnectionVisitorInterface
void NaquidSession::OnConnectionClosed(QuicErrorCode error,
                                const std::string& error_details,
                                ConnectionCloseSource close_by_peer_or_self) {
  QuicSession::OnConnectionClosed(error, error_details, close_by_peer_or_self);
  delegate_->OnClose(error, error_details, close_by_peer_or_self);
}
void NaquidSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  QuicSession::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    if (!delegate_->OnOpen()) {
      delegate_->Disconnect();
    }
  }
}

} //net
