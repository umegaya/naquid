#include "core/compat/nq_quic_alarm.h"
#include "core/compat/nq_loop.h"

#if defined(NQ_CHROMIUM_BACKEND)
namespace net {
//implements QuicAlarm
void NqQuicAlarm::SetImpl() {
  DCHECK(deadline().IsInitialized());
  timeout_in_us_ = (deadline() - QuicTime::Zero()).ToMicroseconds();
  loop_->SetAlarm(this, timeout_in_us_);
}

void NqQuicAlarm::CancelImpl() {
  DCHECK(!deadline().IsInitialized());
  if (timeout_in_us_ <= 0) {
    return; //not registered
  }
  loop_->CancelAlarm(this, timeout_in_us_);
}
} // net
#endif
