#pragma once

#include "core/compat/nq_session.h"

namespace net {
class NqClientStream;
class NqClientSession : public NqSession {
 public:
  NqClientSession(NqQuicConnection *connection,
                  Visitor* owner,
                  NqSessionDelegate* delegate,
                	const QuicConfig& config)
  : NqSession(connection, owner, delegate, config) {
  }

  inline QuicCryptoClientStream *GetClientCryptoStream() {
    ASSERT(delegate()->IsClient());
    return static_cast<QuicCryptoClientStream *>(GetMutableCryptoStream());
  }
  inline const QuicCryptoClientStream *GetClientCryptoStream() const {
    ASSERT(delegate()->IsClient());
    return static_cast<const QuicCryptoClientStream *>(GetCryptoStream());
  }

  //implements QuicSession
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicStream* CreateOutgoingDynamicStream() override;

  //implements QuicConnectionVisitorInterface
  bool WillingAndAbleToWrite() const override {
    auto cs = GetClientCryptoStream();
    if (cs != nullptr && !cs->encryption_established()) {
      return false;
    }
    return QuicSession::WillingAndAbleToWrite();
  }

 private:
  friend class NqQuicClient;
  void InitCryptoStream(QuicCryptoStream *cs) { NqSession::SetCryptoStream(cs); }
};
} // namespace net
