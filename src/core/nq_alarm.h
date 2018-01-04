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
  virtual void OnFire(NqLoop *) = 0;
};
class NqQuicAlarm : public QuicAlarm, 
                    public NqAlarmInterface {
 public:
  NqQuicAlarm(NqLoop *loop, QuicArenaScopedPtr<Delegate> delegate)
      : QuicAlarm(std::move(delegate)), loop_(loop), timeout_in_us_(0) {}
  ~NqQuicAlarm() override {}

  //implements NqAlarmInterface
  void OnFire(NqLoop *) override { Fire(); }

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

  void OnFire(NqLoop *) override = 0;

 public:
  void Start(NqLoop *loop, nq_time_t first_invocation_ts) {
    Stop(loop);
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
  uint64_t alarm_serial_;
 public:
  typedef nq::Allocator<NqAlarm> Allocator;

  NqAlarm() : NqAlarmBase(), cb_(nq_closure_empty()), alarm_serial_(0) {}
  ~NqAlarm() override {}

  inline void Start(NqLoop *loop, nq_time_t first_invocation_ts, nq_closure_t cb) {
    Stop(loop);
    cb_ = cb;
    NqAlarmBase::Start(loop, first_invocation_ts);
  }
  inline NqBoxer *GetBoxer() { return boxer_; }
  NqAlarmIndex alarm_index() const;
  uint64_t alarm_serial() { return alarm_serial_; }
  void InitSerial(uint64_t serial) { alarm_serial_ = serial; }

  // implements NqAlarmInterface
  void OnFire(NqLoop *loop) override {
    TRACE("OnFire %p", this);
    nq_time_t next = invocation_ts_;
    nq_closure_call(cb_, on_alarm, &next);
    if (next > invocation_ts_) {
      NqAlarmBase::Start(loop, next);
      ASSERT(invocation_ts_ == next);
    } else if (next == 0) {
      invocation_ts_ = 0;
    } else {
      delete this;
    }
  }

  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, NqBoxer* l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, NqBoxer *l) noexcept;
};
}
