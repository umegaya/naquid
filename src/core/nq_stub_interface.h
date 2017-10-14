#pragma once

#include "core/nq_loop.h"
#include "core/nq_dispatcher.h"

namespace net {
class NqStubConnectionHelper : public QuicConnectionHelperInterface {
	NqLoop &loop_;
 public:
  NqStubConnectionHelper(NqLoop &loop) : loop_(loop) {}
  const QuicClock* GetClock() const override { return &loop_; }
  QuicRandom* GetRandomGenerator() override { return loop_.GetRandomGenerator(); }
  QuicBufferAllocator* GetStreamFrameBufferAllocator() override { return loop_.GetStreamFrameBufferAllocator(); }
  QuicBufferAllocator* GetStreamSendBufferAllocator() override { return loop_.GetStreamSendBufferAllocator(); }
};
class NqStubAlarmFactory : public QuicAlarmFactory {
  NqLoop &loop_;
public:
  NqStubAlarmFactory(NqLoop &loop) : loop_(loop) {}
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override { return loop_.CreateAlarm(delegate); }
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override { return loop_.CreateAlarm(std::move(delegate), arena); }
};
class NqStubCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
  NqDispatcher &dispather_;
public:
  NqStubCryptoServerStreamHelper(NqDispatcher &dispather) : dispather_(dispather) {}
  // Given the current connection_id, generates a new ConnectionId to
  // be returned with a stateless reject.
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const override {
    return dispather_.GenerateConnectionIdForReject(connection_id);
  }
  // Returns true if |message|, which was received on |self_address| is
  // acceptable according to the visitor's policy. Otherwise, returns false
  // and populates |error_details|.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                    const QuicSocketAddress& self_address,
                                    std::string* error_details) const override {
    return dispather_.CanAcceptClientHello(message, self_address, error_details);
  }
};
}