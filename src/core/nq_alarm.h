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

  //CAUTION: its generally unsafe that call NqLoop::CancelAlarm directly in OnFire callback,
  //because it may change ieterator state of NqLoop::alarm_map_ unpredictable way, as you can see at NqLoop::Poll
  //but operation to nq_alarm_t is safe because it does such operation by queueing it to NqBoxer::Processor. 
  //so if you want to operate alarm directly in OnFire, please add similar task to NqBoxer's Operation and InvokeXXXX function, then use it.
  virtual void OnFire(NqLoop *) = 0;
  virtual bool IsNonQuicAlarm() const = 0;
};
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
class NqAlarmBase : public NqAlarmInterface {
 protected:
  nq_time_t invocation_ts_;
 public:
  NqAlarmBase() : invocation_ts_(0) {}
  ~NqAlarmBase() override {}

  void OnFire(NqLoop *) override = 0;
  bool IsNonQuicAlarm() const override { return false; }

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
 protected:
  void ClearInvocationTS() {
    invocation_ts_ = 0;
  }
};
class NqAlarm : public NqAlarmBase {
  nq_closure_t cb_;
  NqBoxer *boxer_;
  NqSerial alarm_serial_;
 public:
  typedef nq::Allocator<NqAlarm> Allocator;

  NqAlarm() : NqAlarmBase(), cb_(nq_closure_empty()), alarm_serial_() {}
  ~NqAlarm() override { alarm_serial_.Clear(); }

  inline void Start(NqLoop *loop, nq_time_t first_invocation_ts, nq_closure_t cb) {
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
  void OnFire(NqLoop *loop) override;
  void Exec();

  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, NqBoxer* l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, NqBoxer *l) noexcept;
};
}
