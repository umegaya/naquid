#include "interop/nq_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

#include "interop/nq_stream.h"

namespace net {

NqSession::NqSession(QuicConnection* connection,
                                 Delegate* delegate,
                                 const QuicConfig& config) : 
  QuicSession(connection, nullptr, config), delegate_(delegate) {
  crypto_stream_.reset(delegate_->NewCryptoStream(this));
}
QuicStream* NqSession::CreateIncomingDynamicStream(QuicStreamId id) {
  auto s = new NqStream(id, this, false);
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NqSession::CreateOutgoingDynamicStream() {
  auto s = new NqStream(GetNextOutgoingStreamId(), this, true);
  ActivateStream(QuicWrapUnique(s));
  return s;
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
    //TODO(iyatomi): if entering shutdown mode, always disconnect no matter what OnOpen returns
    if (!delegate_->OnOpen()) {
      delegate_->Disconnect();
    }
  }
}

} //net
