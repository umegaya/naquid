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
  void OnFire() override { Fire(); }
  bool IsNonQuicAlarm() const override { return false; }

 protected:
  //implements QuicAlarm
  void SetImpl() override;
  void CancelImpl() override;
  //TODO(iyatomi): there is a way to optimize update?
  //void UpdateImpl() override;

  NqLoop* loop_;
  uint64_t timeout_in_us_;
};
}
#else
namespace net {
class NqQuicAlarm : public NqAlarmBase {
};
}
#endif
