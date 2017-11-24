#pragma once

#include <map>

#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/core/quic_alarm_factory.h"
#include "net/quic/core/quic_simple_buffer_allocator.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/platform/api/quic_clock.h"

#include "basis/loop.h"
#include "basis/handler_map.h"

namespace net {
class NqAlarmInterface;
class NqLoop : public nq::Loop,
               public QuicConnectionHelperInterface,
               public QuicAlarmFactory,
               public QuicClock {
 public:
  NqLoop() : nq::Loop(), 
             approx_now_in_usec_(0),
             alarm_map_() {}

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

 public:
  inline std::multimap<uint64_t, NqAlarmInterface*> &AlarmMap() { return alarm_map_; }
  void Poll();
  uint64_t NowInUsec() const;

 protected:
  friend class NqQuicAlarm;
  friend class NqAlarmBase;
  void SetAlarm(NqAlarmInterface *a, uint64_t timeout_in_us);
  void CancelAlarm(NqAlarmInterface *a, uint64_t timeout_in_us);

 private:
  uint64_t approx_now_in_usec_;
  SimpleBufferAllocator buffer_allocator_;
  std::multimap<uint64_t, NqAlarmInterface*> alarm_map_;
};
}