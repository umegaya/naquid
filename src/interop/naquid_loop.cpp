#include "interop/naquid_loop.h"

#include <sys/time.h>

namespace net {
//implements QuicTime
QuicTime NaquidLoop::ApproximateNow() const {
  if (approx_now_in_usec_ == 0) {
    return Now(); //not yet initialized
  }
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(approx_now_in_usec_);
}

QuicTime NaquidLoop::Now() const {
  return QuicTime::Zero() +
         QuicTime::Delta::FromMicroseconds(NowInUsec());
}

QuicWallTime NaquidLoop::WallNow() const {
  if (approx_now_in_usec_ == 0) {
    return QuicWallTime::FromUNIXMicroseconds(NowInUsec()); //not yet initialized
  }
  return QuicWallTime::FromUNIXMicroseconds(approx_now_in_usec_);
}

QuicTime NaquidLoop::ConvertWallTimeToQuicTime(
    const QuicWallTime& walltime) const {
  return QuicTime::Zero() +
         QuicTime::Delta::FromMicroseconds(walltime.ToUNIXMicroseconds());
}
uint64_t NaquidLoop::NowInUsec() const {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return ((uint64_t)tv.tv_usec) + (((uint64_t)tv.tv_sec) * 1000 * 1000);
}
/* static */
QuicTime NaquidLoop::ToQuicTime(uint64_t from_us) {
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(from_us);
}


//implements QuicAlarmFactory
class NaquidAlarm : public QuicAlarm {
 public:
  NaquidAlarm(NaquidLoop *loop, QuicArenaScopedPtr<Delegate> delegate)
      : QuicAlarm(std::move(delegate)), loop_(loop), timeout_in_us_(0) {}

  inline void OnAlarm() { Fire(); }
 protected:
  void SetImpl() override {
    DCHECK(deadline().IsInitialized());
    timeout_in_us_ = (deadline() - QuicTime::Zero()).ToMicroseconds();
    loop_->AlarmMap().insert(std::make_pair(timeout_in_us_, this));
  }

  void CancelImpl() override {
    DCHECK(!deadline().IsInitialized());
    if (timeout_in_us_ <= 0) {
      return; //not registered
    }
    auto &alarm_map = loop_->AlarmMap();
    auto p = alarm_map.equal_range(timeout_in_us_);
    std::multimap<uint64_t, QuicAlarm*>::iterator it = alarm_map.end();
    for (it = p.first; it != p.second; ++it) {
      if (it->second == this) {
        break;
      }
    }
    if (it != alarm_map.end()) {
      loop_->AlarmMap().erase(it);
    }
  }

  NaquidLoop* loop_;
  uint64_t timeout_in_us_;
};

QuicAlarm* NaquidLoop::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new NaquidAlarm(this, QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> NaquidLoop::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<NaquidAlarm>(this, std::move(delegate));
  } else {
    return QuicArenaScopedPtr<NaquidAlarm>(new NaquidAlarm(this, std::move(delegate)));
  }
}

// polling
void NaquidLoop::Poll() {
  nq::Loop::Poll();
  approx_now_in_usec_ = NowInUsec();  
  for (auto it = alarm_map_.begin(); it != alarm_map_.end();) {
    if (it->first > approx_now_in_usec_) {
      break;
    }
    NaquidAlarm* cb = static_cast<NaquidAlarm*>(it->second);
    cb->OnAlarm();
    auto it_prev = it;
    it++;
    alarm_map_.erase(it_prev);
    delete cb;
  }
}
}  // namespace net
