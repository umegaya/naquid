#pragma once

#include "core/nq_loop_base.h"

#if defined(NQ_CHROMIUM_BACKEND)

#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/core/quic_alarm_factory.h"
#include "net/quic/core/quic_simple_buffer_allocator.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/platform/api/quic_clock.h"

namespace nq {
using namespace net;
class NqAlarmInterface;
class NqLoopCompat : public NqLoopBase,
               public QuicConnectionHelperInterface,
               public QuicAlarmFactory,
               public QuicClock {
public:
  NqLoopCompat() : NqLoopBase() {}

  static QuicTime ToQuicTime(uint64_t from_us);

  // implements QuicConnectionHelperInterface
  const QuicClock* GetClock() const override { return this; }
  QuicRandom* GetRandomGenerator() override { return QuicRandom::GetInstance(); }
  QuicBufferAllocator* GetStreamFrameBufferAllocator() override { return &buffer_allocator_; }
  QuicBufferAllocator* GetStreamSendBufferAllocator() override { return &buffer_allocator_; }

  // implements QuicAlarmFactory
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

  // implements QuicClock
  QuicTime ApproximateNow() const override;
  QuicTime Now() const override;
  QuicWallTime WallNow() const override;
  QuicTime ConvertWallTimeToQuicTime(
      const QuicWallTime& walltime) const override;

private:
  SimpleBufferAllocator buffer_allocator_;
};
}
#else
namespace nq {
typedef NqLoopBase NqLoopCompat;
}
#endif