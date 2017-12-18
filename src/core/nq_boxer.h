#pragma once

#include <string>

#include "MoodyCamel/concurrentqueue.h"

#include "nq.h"
#include "basis/allocator.h"
#include "core/nq_session.h"
#include "core/nq_stream.h"
#include "core/nq_alarm.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqLoop;
class NqBoxer {
 public:
  enum UnboxResult {
    Ok = 0,
    NeedTransfer = -1, //target handle lives in other thread
    SerialExpire = -2,    
  };
  enum OpCode : uint8_t {
    Nop = 0,
    Disconnect,
    Reconnect,
    Flush,
    Finalize,
    Start,
    CreateStream,
    CreateRpc,
    StreamOpen,
  };
  enum OpTarget : uint8_t {
    Invalid = 0,
    Conn = 1,
    Stream = 2,
    Alarm = 3,
  };
  struct Op {
    uint64_t serial_;
    void *target_ptr_;
    OpCode code_;
    OpTarget target_; 
    uint8_t padd_[2];
    union {
      struct {
        nq_time_t invocation_ts_;
        nq_closure_t callback_;
      } alarm_;
      struct {
        char *name_;
        void *ctx_;
      } stream_;
    };
    Op(uint64_t serial, void *target_ptr, OpCode code, OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target) {}
    Op(uint64_t serial, void *target_ptr, OpCode code, nq_time_t ts, nq_closure_t cb, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target) {
      alarm_.invocation_ts_ = ts;
      alarm_.callback_ = cb;
    }
    Op(uint64_t serial, void *target_ptr, OpCode code, const char *name, void *ctx, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target) {
      stream_.name_ = strdup(name);
      stream_.ctx_ = ctx;
    }
    ~Op() {}
  };
  class Processor : public moodycamel::ConcurrentQueue<Op*> {
  public:
    void Poll(NqBoxer *p);
  };

  //interfaces
  virtual void Enqueue(Op *op) = 0;
  virtual bool MainThread() const = 0;
  virtual NqLoop *Loop() = 0;
  //TODO(iyatomi): remove this
  virtual NqSession::Delegate *FindConn(uint64_t serial, OpTarget target) = 0;
  virtual NqStream *FindStream(uint64_t serial, void *p) = 0;
  virtual NqAlarm *NewAlarm() = 0;
  virtual NqAlarm::Allocator *GetAlarmAllocator() = 0;
  virtual void RemoveAlarm(NqAlarmIndex index) = 0;
  virtual bool IsClient() const = 0;
  virtual bool IsSessionLocked(NqSessionIndex idx) const = 0;
  virtual void LockSession(NqSessionIndex idx) = 0;
  virtual void UnlockSession() = 0;

  //invoker
  inline void InvokeConn(uint64_t serial, OpCode code, NqSession::Delegate *unboxed, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        switch (code) {
        case Disconnect:
          unboxed->Disconnect();
          break;
        case Reconnect:
          unboxed->Reconnect();
          break;
        case Finalize:
          delete unboxed;
          break;
        case Flush: {
          QuicConnection::ScopedPacketBundler bundler(unboxed->Connection(), QuicConnection::SEND_ACK_IF_QUEUED);
        } break;
        default:
          ASSERT(false);
          return;
        }
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, OpTarget::Conn));      
    }
  }
  inline void InvokeConn(uint64_t serial, OpCode code, NqSession::Delegate *unboxed, const char *name, void *ctx, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->SessionSerial() == serial) {
        unboxed->NewStream(name, ctx);
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, name, ctx, OpTarget::Conn));      
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, NqStream *unboxed, bool from_queue = false) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (from_queue) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == StreamOpen);
        if (!unboxed->Handler<NqStreamHandler>()->OnOpen()) {
          unboxed->Disconnect();
        }
      } else {
        //already got invalid
        TRACE("InvokeStream stream already invalid %llx %llx", unboxed->stream_serial(), serial);
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, OpTarget::Stream));
    }
  }
  inline void InvokeAlarm(uint64_t serial, OpCode code, nq_time_t invocation_ts, nq_closure_t cb, NqAlarm *unboxed) {
    if (MainThread()) {
      if (unboxed->alarm_serial() == serial) {
        ASSERT(code == Start);
        unboxed->Start(Loop(), invocation_ts, cb);
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, invocation_ts, cb, OpTarget::Alarm));
    }    
  }
  inline void InvokeAlarm(uint64_t serial, OpCode code, NqAlarm *unboxed, bool from_queue = false) {
    if (from_queue) {
      if (unboxed->alarm_serial() == serial) {
        ASSERT(code == Finalize);
        RemoveAlarm(unboxed->alarm_index());
        unboxed->Destroy(Loop());
      } else {
        //already got invalid
      }
    } else {
      Enqueue(new Op(serial, unboxed, code, OpTarget::Alarm));
    }    
  }
  
  template <class H>
  struct unbox_result_trait {
    typedef nullptr_t value;
  };
  template <class H>
  static inline typename unbox_result_trait<H>::value *Unbox(H h) {
    typename unbox_result_trait<H>::value *r;
    NqBoxer *boxer = From(h);
    return boxer->Unbox(h.s, &r) == UnboxResult::Ok ? r : nullptr;
  }
};
template <>
struct NqBoxer::unbox_result_trait<nq_conn_t> {
  typedef NqSession::Delegate value;
};
template <>
struct NqBoxer::unbox_result_trait<nq_stream_t> {
  typedef NqStream value;
};
template <>
struct NqBoxer::unbox_result_trait<nq_rpc_t> {
  typedef NqStream value;
};
}
