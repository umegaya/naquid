#include "core/nq_boxer.h"
#include "core/nq_unwrapper.h"

namespace net {
void NqBoxer::Processor::Poll(NqBoxer *p) {
  Op *op;
  while (try_dequeue(op)) {
    switch (op->target_) {
    case Conn: {
      auto c = reinterpret_cast<NqSession::Delegate *>(op->target_ptr_);
#if defined(USE_DIRECT_WRITE)
      auto m = NqUnwrapper::UnsafeUnwrapMutex(NqConnSerialCodec::IsClient(op->serial_), c);
      std::unique_lock<std::mutex> lock(*m);
      p->LockSession(NqConnSerialCodec::SessionIndex(op->serial_));
#endif
      switch (op->code_) {
      case OpenStream:
        p->InvokeConn(op->serial_, op->code_, c, op->stream_.name_, op->stream_.ctx_, true);
        break;
      default:
        p->InvokeConn(op->serial_, op->code_, c, true);
        break;
      }
#if defined(USE_DIRECT_WRITE)
      p->UnlockSession();
#endif
    } break;
    case Stream: {
#if defined(USE_DIRECT_WRITE)
      auto c = reinterpret_cast<NqSession::Delegate *>(op->target_ptr_);
      auto m = NqUnwrapper::UnsafeUnwrapMutex(NqStreamSerialCodec::IsClient(op->serial_), c);
      std::unique_lock<std::mutex> lock(*m);
      p->LockSession(NqConnSerialCodec::SessionIndex(op->serial_));
#endif
      auto s = p->FindStream(op->serial_, op->target_ptr_);
      switch (op->code_) {
      case Disconnect:
        p->InvokeStream(op->serial_, op->code_, s, nullptr);
        break;
      case Task: 
        p->InvokeStream(op->serial_, op->code_, s, op->task_.callback_, nullptr);
        break;
      case Send:
        p->InvokeStream(op->serial_, op->code_,
                       op->data_.ptr(), op->data_.length(), s, nullptr);
        break;
      case Call:
        p->InvokeStream(op->serial_, op->code_, op->call_.type_, 
                        op->data_.ptr(), op->data_.length(), op->call_.on_reply_, s, nullptr);
        break;
      case CallEx:
        p->InvokeStream(op->serial_, op->code_, op->call_ex_.type_, 
                        op->data_.ptr(), op->data_.length(), op->call_ex_.rpc_opt_, s, nullptr);
        break;
      case Notify:
        p->InvokeStream(op->serial_, op->code_, op->notify_.type_, 
                        op->data_.ptr(), op->data_.length(), s, nullptr);
        break;
      case Reply:
        p->InvokeStream(op->serial_, op->code_, op->reply_.result_, 
                        op->reply_.msgid_, op->data_.ptr(), op->data_.length(), s, nullptr);
        break;
      default:
        ASSERT(false);
        break;
      }
#if defined(USE_WRITE_OP)
      p->UnlockSession();
#endif
    } break;
    case Alarm: {
      auto a = reinterpret_cast<NqAlarm *>(op->target_ptr_);
      switch (op->code_) {
      case Start:
        p->InvokeAlarm(op->serial_, op->code_,
                      op->alarm_.invocation_ts_, op->alarm_.callback_, a);
        break;
      case Finalize:
        p->InvokeAlarm(op->serial_, op->code_, a, true);
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
}
