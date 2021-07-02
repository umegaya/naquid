#include "backends/compats/nq_loop.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include <sys/time.h>

namespace net {
//implements QuicTime
QuicTime NqLoop::ApproximateNow() const {
  if (approx_now_in_usec_ == 0) {
    return Now(); //not yet initialized
  }
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(approx_now_in_usec_);
}

QuicTime NqLoop::Now() const {
  return QuicTime::Zero() +
         QuicTime::Delta::FromMicroseconds(NowInUsec());
}

QuicWallTime NqLoop::WallNow() const {
  if (approx_now_in_usec_ == 0) {
    return QuicWallTime::FromUNIXMicroseconds(NowInUsec()); //not yet initialized
  }
  return QuicWallTime::FromUNIXMicroseconds(approx_now_in_usec_);
}

QuicTime NqLoop::ConvertWallTimeToQuicTime(
    const QuicWallTime& walltime) const {
  return QuicTime::Zero() +
         QuicTime::Delta::FromMicroseconds(walltime.ToUNIXMicroseconds());
}
/* static */
QuicTime NqLoop::ToQuicTime(uint64_t from_us) {
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(from_us);
}

//implements QuicAlarmFactory
QuicAlarm* NqLoop::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new NqQuicAlarm(this, QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}
QuicArenaScopedPtr<QuicAlarm> NqLoop::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<NqQuicAlarm>(this, std::move(delegate));
  } else {
    return QuicArenaScopedPtr<NqQuicAlarm>(new NqQuicAlarm(this, std::move(delegate)));
  }
}
} // net
#endif
