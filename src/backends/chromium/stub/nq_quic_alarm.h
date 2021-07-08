#pragma once

#include "core/nq_alarm.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_alarm.h"

namespace net {
class NqQuicAlarm : public QuicAlarm, 
                    public NqAlarmInterface {
 public:
  NqQuicAlarm(NqLoop *loop, QuicArenaScopedPtr<Delegate> delegate)
      : QuicAlarm(std::move(delegate)), loop_(loop), timeout_in_us_(0) {}
  ~NqQuicAlarm() override {}

  //implements NqAlarmInterface
  void OnFire(NqLoop *) override { Fire(); }
  bool IsNonQuicAlarm() const override { return false; }

 protected:
  //implements QuicAlarm
  void SetImpl() override {
    DCHECK(deadline().IsInitialized());
    timeout_in_us_ = (deadline() - QuicTime::Zero()).ToMicroseconds();
    loop_->SetAlarm(this, timeout_in_us_);
  }

  void CancelImpl() override {
    DCHECK(!deadline().IsInitialized());
    if (timeout_in_us_ <= 0) {
      return; //not registered
    }
    loop_->CancelAlarm(this, timeout_in_us_);
  }
  //TODO(iyatomi): there is a way to optimize update?
  //void UpdateImpl() override;

  NqLoop* loop_;
  uint64_t timeout_in_us_;
};
#else
namespace net {
class NqAuicAlarm : public NqAlarmBase {
}
}
#endif
