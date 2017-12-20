#include "core/nq_boxer.h"
#include "core/nq_unwrapper.h"

namespace net {
void NqBoxer::Processor::Poll(NqBoxer *p) {
  Op *op;
  while (try_dequeue(op)) {
    switch (op->target_) {
    case Conn: {
      auto c = reinterpret_cast<NqSession::Delegate *>(op->target_ptr_);
      auto m = NqUnwrapper::UnsafeUnwrapMutex(NqConnSerialCodec::IsClient(op->serial_), c);
      std::unique_lock<std::mutex> lock(*m);
      p->LockSession(NqConnSerialCodec::SessionIndex(op->serial_));
      switch (op->code_) {
      case CreateStream:
      case CreateRpc:
        p->InvokeConn(op->serial_, op->code_, c, op->stream_.name_, op->stream_.ctx_, true);
        break;
      default:
        p->InvokeConn(op->serial_, op->code_, c, true);
        break;
      }
      p->UnlockSession();
    } break;
    case Stream: {
      auto c = reinterpret_cast<NqSession::Delegate *>(op->target_ptr_);
      auto m = NqUnwrapper::UnsafeUnwrapMutex(NqStreamSerialCodec::IsClient(op->serial_), c);
      std::unique_lock<std::mutex> lock(*m);
      p->LockSession(NqConnSerialCodec::SessionIndex(op->serial_));
      auto s = p->FindStream(op->serial_, op->target_ptr_);
      switch (op->code_) {
      case Disconnect:
        p->InvokeStream(op->serial_, op->code_, s, nullptr);
        break;
#if defined(USE_WRITE_OP)
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
#endif
      default:
        ASSERT(false);
        break;
      }
      p->UnlockSession();
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
