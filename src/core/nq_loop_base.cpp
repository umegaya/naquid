#include "core/nq_loop_base.h"

#include <sys/time.h>

#include "core/nq_alarm.h"

namespace nq {
// handling alarm
void NqLoopBase::SetAlarm(NqAlarmInterface *a, uint64_t timeout_in_us) {
  alarm_map_.emplace(std::piecewise_construct, 
                    std::forward_as_tuple(timeout_in_us), 
                    std::forward_as_tuple(a));
}
void NqLoopBase::CancelAlarm(NqAlarmInterface *a, uint64_t timeout_in_us) {
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



// polling
uint64_t NqLoopBase::NowInUsec() const {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return ((uint64_t)tv.tv_usec) + (((uint64_t)tv.tv_sec) * 1000 * 1000);
}
void NqLoopBase::Poll() {
  Loop::Poll();
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
    cb->OnFire();
    it++;
    //add small duration to avoid infinite loop 
    //(eg. OnAlarm adds new alarm that adds new alarm on OnAlarm again)
    approx_now_in_usec_++; 
  }
  alarm_process_us_ts_ = 0;
  //TRACE("------------ end -------------------");
}
}  // namespace nq
