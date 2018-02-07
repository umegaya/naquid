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
  alarm_map_.emplace(std::piecewise_construct, 
                    std::forward_as_tuple(timeout_in_us), 
                    std::forward_as_tuple(a));
}
void NqLoop::CancelAlarm(NqAlarmInterface *a, uint64_t timeout_in_us) {
  auto p = alarm_map_.equal_range(timeout_in_us);
  auto it = p.first;
  for (; it != p.second; ++it) {
    if (it->second.ptr_ == a) {
      if (alarm_process_us_ts_ == 0 || alarm_process_us_ts_ < timeout_in_us) {
        alarm_map_.erase(it);
      } else {
        TRACE("mark erased: %p %llu", it->second.ptr_, timeout_in_us);
        it->second.erased_ = true;
      }
      return;
    }
  }
  ASSERT(false);
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
  alarm_process_us_ts_ = approx_now_in_usec_;
  auto it = alarm_map_.begin();
  while (true) {
    //TRACE("try invoke alarm %p at %llu %llu %lld", it->second, it->first, current, current - it->first);
    //prevent infinite looping when OnAlarm keep on re-assigning alarm to the alarm_map_.
    if (it == alarm_map_.end()) {
      alarm_map_.clear();
      break;
    }
    if (it->first > alarm_process_us_ts_) { //multimap key should be ordered
      alarm_map_.erase(alarm_map_.begin(), it);
      break;
    }
    if (it->second.erased_) {
      TRACE("erased alarm: %p %llu", it->second.ptr_, it->first);
      it++;
      continue;
    }
    NqAlarmInterface* cb = static_cast<NqAlarmInterface*>(it->second.ptr_);
    //TRACE("invoke alarm %p(%s) at %llu", cb, cb->IsNonQuicAlarm() ? "reconn" : "",it->first);
    cb->OnFire(this);
    it++;
    //add small duration to avoid infinite loop 
    //(eg. OnAlarm adds new alarm that adds new alarm on OnAlarm again)
    approx_now_in_usec_++; 
  }
  alarm_process_us_ts_ = 0;
  //TRACE("------------ end -------------------");
}
}  // namespace net
