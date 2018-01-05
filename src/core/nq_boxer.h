#pragma once

#include <string>

#include "MoodyCamel/concurrentqueue.h"

#include "nq.h"
#include "basis/allocator.h"
#include "core/nq_session.h"
#include "core/nq_stream.h"
#include "core/nq_alarm.h"
#include "core/nq_serial_codec.h"

//#define USE_WRITE_OP (1)

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
#if defined(USE_WRITE_OP)
    Send,
    Call,
    CallEx,
    Reply,
    Notify,
#endif
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
    struct Data {
      const void *p_;
      nq_size_t len_;
      Data() { len_ = 0; }
      Data(const void *p, nq_size_t len) {
        if (len <= 0) {
          p_ = p; len_ = len; //if p is not byte array, assume memory is managed by caller
        } else {
          ASSERT(len <= 10000);
          //TODO(iyatomi): allocation from some pool
          p_ = nq::Syscall::Memdup(p, len); len_ = len;
        }
      }
      ~Data() { if (len_ > 0) { nq::Syscall::MemFree(const_cast<void *>(p_)); } }
      inline const void *ptr() const { return p_; }
      inline nq_size_t length() const { return len_; } 
    } data_;
    union {
      struct {
        nq_closure_t on_reply_;
        uint16_t type_;
      } call_;
      struct {
        nq_rpc_opt_t rpc_opt_;
        uint16_t type_;
      } call_ex_;
      struct {
        uint16_t type_;
      } notify_;
      struct {
        nq_error_t result_;
        nq_msgid_t msgid_;
      } reply_;
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
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {}
    Op(uint64_t serial, void *target_ptr, OpCode code, nq_time_t ts, nq_closure_t cb, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {
      alarm_.invocation_ts_ = ts;
      alarm_.callback_ = cb;
    }
    Op(uint64_t serial, void *target_ptr, OpCode code, const char *name, void *ctx, 
      OpTarget target) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_() {
      stream_.name_ = strdup(name);
      stream_.ctx_ = ctx;
    }
#if defined(USE_WRITE_OP)
    Op(uint64_t serial, void *target_ptr, OpCode code, const void *data, nq_size_t datalen, 
       OpTarget target = OpTarget::Stream) : 
      serial_(serial), code_(code), target_(target), data_(data, datalen) {}
    Op(uint64_t serial, void *target_ptr, OpCode code, uint16_t type, const void *data, 
       nq_size_t datalen, nq_closure_t on_reply, 
       OpTarget target = OpTarget::Stream) :
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      call_.type_ = type;
      call_.on_reply_ = on_reply;
    }
    Op(uint64_t serial, void *target_ptr, OpCode code, uint16_t type, const void *data, 
       nq_size_t datalen, nq_rpc_opt_t rpc_opt, 
       OpTarget target = OpTarget::Stream) :
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      call_ex_.type_ = type;
      call_ex_.rpc_opt_ = rpc_opt;
    }
    Op(uint64_t serial, void *target_ptr, OpCode code, uint16_t type, 
       const void *data, nq_size_t datalen, OpTarget target = OpTarget::Stream) : 
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      notify_.type_ = type;
    }
    Op(uint64_t serial, void *target_ptr, OpCode code, 
       nq_error_t result, nq_msgid_t msgid, 
       const void *data, nq_size_t datalen, OpTarget target = OpTarget::Stream) :
      serial_(serial), target_ptr_(target_ptr), code_(code), target_(target), data_(data, datalen) {
      reply_.result_ = result;
      reply_.msgid_ = msgid;
    }
#endif
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
  inline void InvokeStream(uint64_t serial, OpCode code, NqStream *unboxed, NqSession::Delegate *d) {
    //UnboxResult r = UnboxResult::Ok;
    //always enter queue to be safe when this call inside protocol handler
    if (unboxed != nullptr) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Disconnect);
        unboxed->Disconnect();
      }
    } else {
      ASSERT(d != nullptr);
      Enqueue(new Op(serial, d, code, OpTarget::Stream));
    }
  }
#if defined(USE_WRITE_OP)
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           const void *data, nq_size_t datalen, NqStream *unboxed, NqSession::Delegate *d) {
    if (unboxed != nullptr) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Send);
        unboxed->Handler<NqStreamHandler>()->Send(data, datalen);
      }
    } else {
      Enqueue(new Op(serial, d, code, data, datalen));
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           uint16_t type, const void *data, 
                           nq_size_t datalen, nq_closure_t on_reply, NqStream *unboxed, NqSession::Delegate *d) {
    if (unboxed != nullptr) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Call);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->Call(type, data, datalen, on_reply);
      }
    } else {
      Enqueue(new Op(serial, d, code, type, data, datalen, on_reply));
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           uint16_t type, const void *data, 
                           nq_size_t datalen, nq_rpc_opt_t &rpc_opt, NqStream *unboxed, NqSession::Delegate *d) {
    if (unboxed != nullptr) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == CallEx);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->CallEx(type, data, datalen, rpc_opt);
      }
    } else {
      Enqueue(new Op(serial, d, code, type, data, datalen, rpc_opt));
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           uint16_t type, const void *data, nq_size_t datalen, NqStream *unboxed, NqSession::Delegate *d) {    
    if (unboxed != nullptr) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Notify);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->Notify(type, data, datalen);
      }
    } else {
      Enqueue(new Op(serial, d, code, type, data, datalen));
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           nq_error_t result, nq_msgid_t msgid, 
                           const void *data, nq_size_t datalen, NqStream *unboxed, NqSession::Delegate *d) {
    if (unboxed != nullptr) {
      if (unboxed->stream_serial() == serial) {
        ASSERT(code == Reply);
        unboxed->Handler<NqSimpleRPCStreamHandler>()->Reply(result, msgid, data, datalen);
      }
    } else {
      Enqueue(new Op(serial, d, code, result, msgid, data, datalen));
    }
  }
#endif

  
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
