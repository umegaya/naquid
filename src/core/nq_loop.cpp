#include "core/nq_loop.h"

#include <sys/time.h>

#include "core/nq_alarm.h"

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
uint64_t NqLoop::NowInUsec() const {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return ((uint64_t)tv.tv_usec) + (((uint64_t)tv.tv_sec) * 1000 * 1000);
}
/* static */
QuicTime NqLoop::ToQuicTime(uint64_t from_us) {
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(from_us);
}


void NqLoop::SetAlarm(NqAlarmInterface *a, uint64_t timeout_in_us) {
  AlarmMap().insert(std::make_pair(timeout_in_us, a));
}
void NqLoop::CancelAlarm(NqAlarmInterface *a, uint64_t timeout_in_us) {
    auto &alarm_map = AlarmMap();
    auto p = alarm_map.equal_range(timeout_in_us);
    auto it = p.first;
    for (; it != p.second; ++it) {
      if (it->second == a) {
        break;
      }
    }
    if (it != alarm_map.end()) {
      AlarmMap().erase(it);
    } else {
      ASSERT(false);
    }  
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

// polling
void NqLoop::Poll() {
  nq::Loop::Poll();
  approx_now_in_usec_ = NowInUsec();
  auto current = approx_now_in_usec_;
  for (auto it = alarm_map_.begin(); it != alarm_map_.end();) {
    //prevent infinite looping when OnAlarm keep on re-assigning alarm to the alarm_map_.
    if (it->first > current) { //multimap key should be ordered
      break;
    }
    NqAlarmInterface* cb = static_cast<NqAlarmInterface*>(it->second);
    auto it_prev = it;
    it++;
    if (cb->OnFire(this)) {
      alarm_map_.erase(it_prev);
    }
    //add small duration to avoid infinite loop 
    //(eg. OnAlarm adds new alarm that adds new alarm on OnAlarm again)
    approx_now_in_usec_++; 
  }
}
}  // namespace net
