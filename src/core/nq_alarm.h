#pragma once

#include "net/quic/core/quic_alarm.h"

#include "nq.h"
#include "basis/timespec.h"
#include "core/nq_loop.h"
#include "core/nq_serial_codec.h"

namespace net {
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
  NqAlarmIndex alarm_index_;
 public:
  NqAlarm() : NqAlarmBase(), cb_(nq_closure_empty()), alarm_index_(0) {}

  inline void set_alarm_index(NqAlarmIndex idx) { alarm_index_ = idx; }
  inline NqAlarmIndex alarm_index() const { return alarm_index_; }
  inline void Start(NqLoop *loop, nq_time_t first_invocation_ts, nq_closure_t cb) {
    Stop(loop);
    cb_ = cb;
    NqAlarmBase::Start(loop, first_invocation_ts);
  }

  // implements NqAlarmInterface
  void OnFire(NqLoop *loop) override {
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
};
}
