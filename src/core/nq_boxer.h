#pragma once

#include <string>

#include "MoodyCamel/concurrentqueue.h"

#include "nq.h"
#include "core/nq_session.h"
#include "core/nq_stream.h"
#include "core/nq_serial_codec.h"

namespace net {
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
    Send,
    Call,
    Reply,
    Notify,
    Flush,
    Finalize,
  };
  enum OpTarget : uint8_t {
    Invalid = 0,
    Conn = 1,
    Stream = 2,
  };
  struct Op {
    uint64_t serial_;
    OpCode code_;
    OpTarget target_; 
    uint8_t padd_[2];
    struct Data {
      const void *p_;
      nq_size_t len_;
      Data() {}
      Data(const void *p, nq_size_t len) {
        if (len <= 0) {
          p_ = p; len_ = len; //if p is not byte array, assume memory is managed by caller
        } else {
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
        uint16_t type_;
      } notify_;
      struct {
        nq_result_t result_;
        nq_msgid_t msgid_;
      } reply_;
    };
    Op(uint64_t serial, OpCode code, OpTarget target = OpTarget::Stream) : 
      serial_(serial), code_(code), target_(target) { data_.len_ = 0; }
    Op(uint64_t serial, OpCode code, const void *data, nq_size_t datalen, 
       OpTarget target = OpTarget::Stream) : 
      serial_(serial), code_(code), target_(target), data_(data, datalen) {}
    Op(uint64_t serial, OpCode code, uint16_t type, const void *data, 
       nq_size_t datalen, nq_closure_t on_reply, 
       OpTarget target = OpTarget::Stream) :
      serial_(serial), code_(code), target_(target), data_(data, datalen) {
      call_.type_ = type;
      call_.on_reply_ = on_reply;
    }
    Op(uint64_t serial, OpCode code, uint16_t type, 
       const void *data, nq_size_t datalen, OpTarget target = OpTarget::Stream) : 
      serial_(serial), code_(code), target_(target), data_(data, datalen) {
      notify_.type_ = type;
    }
    Op(uint64_t serial, OpCode code, 
       nq_result_t result, nq_msgid_t msgid, 
       const void *data, nq_size_t datalen, OpTarget target = OpTarget::Stream) :
      serial_(serial), code_(code), target_(target), data_(data, datalen) {
      reply_.result_ = result;
      reply_.msgid_ = msgid;
    }
    ~Op() {}
  };
  class Processor : public moodycamel::ConcurrentQueue<Op*> {
  public:
    inline void Poll(NqBoxer *p) {
      Op *op;
      while (try_dequeue(op)) {
        switch (op->target_) {
        case Conn: {
          NqSession::Delegate *d;
          if (p->Unbox(op->serial_, &d) == UnboxResult::Ok) {
            p->InvokeConn(op->serial_, op->code_, d);
          }
        } break;
        case Stream: {
          NqStream *s;
          if (p->Unbox(op->serial_, &s) != UnboxResult::Ok) {
            break;
          }
          switch (op->code_) {
          case Disconnect:
            p->InvokeStream(op->serial_, op->code_, s);
            break;
          case Call:
            p->InvokeStream(op->serial_, op->code_, op->call_.type_, 
                            op->data_.ptr(), op->data_.length(), op->call_.on_reply_);
            break;
          case Notify:
            p->InvokeStream(op->serial_, op->code_, op->notify_.type_, 
                            op->data_.ptr(), op->data_.length());
            break;
          case Reply:
            p->InvokeStream(op->serial_, op->code_, op->reply_.result_, 
                            op->reply_.msgid_, op->data_.ptr(), op->data_.length());
            break;
          default:
            ASSERT(false);
            break;
          }
        } break;
        default:
          ASSERT(false);
          break;
        }
        delete op;
      }
    }
  };

  //interfaces
  virtual void Enqueue(Op *op) = 0;
  virtual nq_conn_t Box(NqSession::Delegate *d) = 0;
  virtual nq_stream_t Box(NqStream *s) = 0;
  virtual UnboxResult Unbox(uint64_t serial, NqSession::Delegate **unboxed) = 0;
  virtual UnboxResult Unbox(uint64_t serial, NqStream **unboxed) = 0;
  virtual const NqSession::Delegate *FindConn(uint64_t serial, OpTarget target) const = 0;
  virtual const NqStream *FindStream(uint64_t serial) const = 0;
  virtual bool IsClient() const = 0;

  //invoker
  inline void InvokeConn(uint64_t serial, OpCode code, NqSession::Delegate *unboxed = nullptr) {
    UnboxResult r = UnboxResult::Ok;
    if (unboxed != nullptr || (r = Unbox(serial, &unboxed)) == UnboxResult::Ok) {
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
      Enqueue(new Op(serial, code, OpTarget::Conn));
    }
  }
  //TODO(iyatomi): use direct pointer of NqStream to achieve faster operation
  //1. allocate NqStream by the way that reserved it to pool on destroy. 
  //2. then heap for NqStream never reused by other objects, boxer remain same (and we can set invalid value for serial)
  //3. nq_stream/rpc_t has NqStream pointer as value of member p. this value also passed to InvokeStream by storing it into Op
  //4. so we can check validity of NqStream just compare given serial value from nq_stream/rpc_t with stored one, instead of double hash table lookup in Unbox
  //current blocker of this fix is: NqStream wrapped unique_ptr<T> inside of QuicSession, so impossible to reserve it to pool.
  inline void InvokeStream(uint64_t serial, OpCode code, NqStream *unboxed = nullptr) {
    UnboxResult r = UnboxResult::Ok;
    if (unboxed != nullptr || (r = Unbox(serial, &unboxed)) == UnboxResult::Ok) {
      ASSERT(code == Disconnect);
      unboxed->Disconnect();
    } else if (r == UnboxResult::NeedTransfer) {
      Enqueue(new Op(serial, code));
    } else {
      ASSERT(r == UnboxResult::SerialExpire);
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           const void *data, nq_size_t datalen, NqStream *unboxed = nullptr) {
    UnboxResult r = UnboxResult::Ok;
    if (unboxed != nullptr || (r = Unbox(serial, &unboxed)) == UnboxResult::Ok) {
      ASSERT(code == Send);
      unboxed->Handler<NqStreamHandler>()->Send(data, datalen);
    } else if (r == UnboxResult::NeedTransfer) {
      Enqueue(new Op(serial, code, data, datalen));
    } else {
      ASSERT(r == UnboxResult::SerialExpire);
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           uint16_t type, const void *data, 
                           nq_size_t datalen, nq_closure_t on_reply, NqStream *unboxed = nullptr) {
    UnboxResult r = UnboxResult::Ok;
    if (unboxed != nullptr || (r = Unbox(serial, &unboxed)) == UnboxResult::Ok) {
      ASSERT(code == Call);
      unboxed->Handler<NqStreamHandler>()->Send(type, data, datalen, on_reply);
    } else if (r == UnboxResult::NeedTransfer) {
      Enqueue(new Op(serial, code, type, data, datalen, on_reply));
    } else {
      ASSERT(r == UnboxResult::SerialExpire);
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           uint16_t type, const void *data, nq_size_t datalen, NqStream *unboxed = nullptr) {    
    UnboxResult r = UnboxResult::Ok;
    if (unboxed != nullptr || (r = Unbox(serial, &unboxed)) == UnboxResult::Ok) {
      ASSERT(code == Notify);
      unboxed->Handler<NqSimpleRPCStreamHandler>()->Notify(type, data, datalen);
    } else if (r == UnboxResult::NeedTransfer) {
      Enqueue(new Op(serial, code, type, data, datalen));
    } else {
      ASSERT(r == UnboxResult::SerialExpire);
    }
  }
  inline void InvokeStream(uint64_t serial, OpCode code, 
                           nq_result_t result, nq_msgid_t msgid, 
                           const void *data, nq_size_t datalen, NqStream *unboxed = nullptr) {
    UnboxResult r = UnboxResult::Ok;
    if (unboxed != nullptr || (r = Unbox(serial, &unboxed)) == UnboxResult::Ok) {
      ASSERT(code == Reply);
      unboxed->Handler<NqSimpleRPCStreamHandler>()->Reply(result, msgid, data, datalen);
    } else if (r == UnboxResult::NeedTransfer) {
      Enqueue(new Op(serial, code, result, msgid, data, datalen));
    } else {
      ASSERT(r == UnboxResult::SerialExpire);
    }
  }
  
  //helper
  static inline NqBoxer *From(nq_conn_t c) { return (NqBoxer *)c.p; }
  static inline NqBoxer *From(nq_stream_t s) { return (NqBoxer *)s.p; }
  static inline NqBoxer *From(nq_rpc_t rpc) { return (NqBoxer *)rpc.p; }

  inline const NqSession::Delegate *Find(nq_conn_t c) const { return c.s != 0 ? FindConn(c.s, Conn) : nullptr; }
  inline const NqStream *Find(nq_stream_t s) const { return s.s != 0 ? FindStream(s.s) : nullptr; }
  inline const NqStream *Find(nq_rpc_t rpc) const { return rpc.s != 0 ? FindStream(rpc.s) : nullptr; }

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
