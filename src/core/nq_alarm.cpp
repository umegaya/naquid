#include "core/nq_alarm.h"

#include "core/nq_boxer.h"

namespace net {
NqAlarmIndex NqAlarm::alarm_index() const {
  if (NqSerial::IsClient(alarm_serial_)) {
    return NqAlarmSerialCodec::ClientAlarmIndex(alarm_serial_);
  } else {
    return NqAlarmSerialCodec::ServerAlarmIndex(alarm_serial_);    
  }
}
void* NqAlarm::operator new(std::size_t sz) {
  ASSERT(false);
  auto r = reinterpret_cast<NqAlarm *>(std::malloc(sz));
  r->boxer_ = nullptr;
  return r;
}
void* NqAlarm::operator new(std::size_t sz, NqBoxer* b) {
  auto r = reinterpret_cast<NqAlarm *>(b->GetAlarmAllocator()->Alloc(sz));
  r->boxer_ = b;
  return r;
}
void NqAlarm::operator delete(void *p) noexcept {
  auto r = reinterpret_cast<NqAlarm *>(p);
  if (r->boxer_ == nullptr) {
    ASSERT(false);
    std::free(r);
  } else {
    r->boxer_->GetAlarmAllocator()->Free(p);
  }
}
void NqAlarm::operator delete(void *p, NqBoxer *b) noexcept {
  b->GetAlarmAllocator()->Free(p);
}
void NqAlarm::OnFire(NqLoop *loop) {
  boxer_->InvokeAlarm(alarm_serial_, this, NqBoxer::OpCode::Exec);
  //ClearInvocationTS();
}
void NqAlarm::Exec() {
  //here, alarm is already unregistered from NqLoop::alarm_map_
  NqLoop *loop = boxer_->Loop();
  nq_time_t invoke = invocation_ts_;
  nq_time_t next = invoke;
  nq_closure_call(cb_, &next);
  ClearInvocationTS();
  if (next > invoke) {
    NqAlarmBase::Start(loop, next);
    ASSERT(invocation_ts_ == next);
  } else if (next == 0) {
    //stopped but not freed. you can resume by using nq_alarm_set
  } else {
    delete this;
  }    
}
}
