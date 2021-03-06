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
#include "core/nq_serial_codec.h"

namespace net {
class NqAlarmInterface;
class NqLoop : public nq::Loop,
               public QuicConnectionHelperInterface,
               public QuicAlarmFactory,
               public QuicClock {
 public:
  NqLoop() : nq::Loop(), 
             approx_now_in_usec_(0),
             alarm_map_(), 
             alarm_process_us_ts_(0),
             current_locked_session_id(0) {}

  inline void LockSession(NqSessionIndex idx) { current_locked_session_id = idx + 1; }
  inline void UnlockSession() { current_locked_session_id = 0; }
  inline bool IsSessionLocked(NqSessionIndex idx) const { return current_locked_session_id == (1 + idx); }

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
  //inline std::multimap<uint64_t, NqAlarmInterface*> &AlarmMap() { return alarm_map_; }
  void Poll();
  uint64_t NowInUsec() const;

 protected:
  friend class NqQuicAlarm;
  friend class NqAlarmBase;
  void SetAlarm(NqAlarmInterface *a, uint64_t timeout_in_us);
  void CancelAlarm(NqAlarmInterface *a, uint64_t timeout_in_us);

 private:
  class AlarmEntry {
   public:
    NqAlarmInterface *ptr_;
    bool erased_;
   public:
    AlarmEntry(NqAlarmInterface *ptr) : ptr_(ptr), erased_(false) {}
  };
  uint64_t approx_now_in_usec_;
  SimpleBufferAllocator buffer_allocator_;
  std::multimap<uint64_t, AlarmEntry> alarm_map_;
  nq_time_t alarm_process_us_ts_;
  nq::atomic<NqSessionIndex> current_locked_session_id;
};
}