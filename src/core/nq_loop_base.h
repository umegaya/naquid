#pragma once

#include <map>

#include "basis/atomic.h"
#include "basis/loop.h"
#include "basis/handler_map.h"
#include "core/nq_serial_codec.h"

namespace nq {
class NqAlarmInterface;
class NqLoopBase : public Loop {
 public:
  NqLoopBase() : Loop(), 
             approx_now_in_usec_(0),
             alarm_map_(), 
             alarm_process_us_ts_(0),
             current_locked_session_id_(0) {}

  inline void LockSession(NqSessionIndex idx) { current_locked_session_id_ = idx + 1; }
  inline void UnlockSession() { current_locked_session_id_ = 0; }
  inline bool IsSessionLocked(NqSessionIndex idx) const { return current_locked_session_id_ == (1 + idx); }

 public:
  //inline std::multimap<uint64_t, NqAlarmInterface*> &AlarmMap() { return alarm_map_; }
  void Poll();
  uint64_t NowInUsec() const;

 protected:
  friend class NqQuicAlarm;
  friend class NqAlarmBase;
  void SetAlarm(NqAlarmInterface *a, uint64_t timeout_in_us);
  void CancelAlarm(NqAlarmInterface *a, uint64_t timeout_in_us);

 protected:
  class AlarmEntry {
   public:
    NqAlarmInterface *ptr_;
    bool erased_;
   public:
    AlarmEntry(NqAlarmInterface *ptr) : ptr_(ptr), erased_(false) {}
  };
  uint64_t approx_now_in_usec_;
  std::multimap<uint64_t, AlarmEntry> alarm_map_;
  nq_time_t alarm_process_us_ts_;
  atomic<NqSessionIndex> current_locked_session_id_;
};
} // net
