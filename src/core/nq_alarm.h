#pragma once

#include "net/quic/core/quic_alarm.h"

#include "nq.h"
#include "basis/timespec.h"
#include "basis/allocator.h"
#include "core/nq_loop.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqBoxer;
class NqAlarmInterface {
 public:
  virtual ~NqAlarmInterface() {}
  //return false if it calls NqLoop::CancelAlarm internally, 
  //true otherwise, then system automatically remove this interface from NqLoop::alarm_map_
  virtual bool OnFire(NqLoop *) = 0;
};
class NqQuicAlarm : public QuicAlarm, 
                    public NqAlarmInterface {
 public:
  NqQuicAlarm(NqLoop *loop, QuicArenaScopedPtr<Delegate> delegate)
      : QuicAlarm(std::move(delegate)), loop_(loop), timeout_in_us_(0) {}
  ~NqQuicAlarm() override {}

  //implements NqAlarmInterface
  bool OnFire(NqLoop *) override { 
    Fire(); 
    return true;
  }

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
class NqAlarmBase : public NqAlarmInterface {
 protected:
  nq_time_t invocation_ts_;
 public:
  NqAlarmBase() : invocation_ts_(0) {}
  ~NqAlarmBase() override {}

  bool OnFire(NqLoop *) override = 0;

 public:
  void Start(NqLoop *loop, nq_time_t first_invocation_ts) {
    Stop(loop);
    ASSERT(invocation_ts_ == 0);
    invocation_ts_ = first_invocation_ts;
    loop->SetAlarm(this, nq::clock::to_us(invocation_ts_));
  }
  void Stop(NqLoop *loop) {
    if (invocation_ts_ != 0) {
      loop->CancelAlarm(this, nq::clock::to_us(invocation_ts_));
      invocation_ts_ = 0;
    }
  }
  void Destroy(NqLoop *loop) {
    Stop(loop);
    delete this;
  }
};
class NqAlarm : public NqAlarmBase {
  nq_closure_t cb_;
  NqBoxer *boxer_;
  NqSerial alarm_serial_;
 public:
  typedef nq::Allocator<NqAlarm> Allocator;

  NqAlarm() : NqAlarmBase(), cb_(nq_closure_empty()), alarm_serial_() {}
  ~NqAlarm() override {}

  inline void Start(NqLoop *loop, nq_time_t first_invocation_ts, nq_closure_t cb) {
    cb_ = cb;
    NqAlarmBase::Start(loop, first_invocation_ts);
  }
  inline NqBoxer *boxer() { return boxer_; }
  inline const NqSerial &alarm_serial() const { return alarm_serial_; }
  inline nq_alarm_t ToHandle() { return MakeHandle<nq_alarm_t, NqAlarm>(this, alarm_serial_); }
  NqAlarmIndex alarm_index() const;
  void InitSerial(const nq_serial_t &serial) { alarm_serial_ = serial; }

  // implements NqAlarmInterface
  bool OnFire(NqLoop *loop) override {
    nq_time_t next = invocation_ts_;
    nq_closure_call(cb_, on_alarm, &next);
    if (next > invocation_ts_) {
      NqAlarmBase::Start(loop, next);
      ASSERT(invocation_ts_ == next);
      return false;
    } else if (next == 0) {
      invocation_ts_ = 0;
    } else {
      delete this;
    }
    return true;
  }

  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, NqBoxer* l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, NqBoxer *l) noexcept;
};
}
