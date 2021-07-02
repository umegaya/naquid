#pragma once

#include "nq.h"
#include "basis/timespec.h"
#include "basis/allocator.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqBoxer;
class NqLoop;
class NqAlarmInterface {
 public:
  virtual ~NqAlarmInterface() {}
  virtual void OnFire() = 0;
  virtual bool IsNonQuicAlarm() const = 0;
};
class NqAlarmBase : public NqAlarmInterface {
 protected:
  nq_time_t invocation_ts_;
 public:
  NqAlarmBase() : invocation_ts_(0) {}
  ~NqAlarmBase() override {}

  void OnFire() override = 0;
  bool IsNonQuicAlarm() const override { return false; }

 public:
  void Start(NqLoop *loop, nq_time_t first_invocation_ts);
  void Stop(NqLoop *loop);
  void Destroy(NqLoop *loop) {
    Stop(loop);
    delete this;
  }
 protected:
  void ClearInvocationTS() {
    invocation_ts_ = 0;
  }
};
class NqAlarm : public NqAlarmBase {
  nq_on_alarm_t cb_;
  NqBoxer *boxer_;
  NqSerial alarm_serial_;
 public:
  typedef nq::Allocator<NqAlarm> Allocator;

  NqAlarm() : NqAlarmBase(), alarm_serial_() { cb_ = nq_closure_empty(); }
  ~NqAlarm() override { alarm_serial_.Clear(); }

  inline void Start(NqLoop *loop, nq_time_t first_invocation_ts, nq_on_alarm_t cb) {
    TRACE("NqAlarm Start:%p", this);
    cb_ = cb;
    NqAlarmBase::Start(loop, first_invocation_ts);
  }
  inline NqBoxer *boxer() { return boxer_; }
  inline const NqSerial &alarm_serial() const { return alarm_serial_; }
  inline nq_alarm_t ToHandle() { return MakeHandle<nq_alarm_t, NqAlarm>(this, alarm_serial_); }
  NqAlarmIndex alarm_index() const;
  void InitSerial(const nq_serial_t &serial) { alarm_serial_ = serial; }

  // implements NqAlarmInterface
  void OnFire() override;
  void Exec();

  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, NqBoxer* l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, NqBoxer *l) noexcept;
};
}
