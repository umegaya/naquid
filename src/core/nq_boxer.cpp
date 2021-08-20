#include "core/nq_boxer.h"
#include "core/nq_unwrapper.h"

namespace nq {
void NqBoxer::Processor::Poll(NqBoxer *p) {
  Op *op;
  while (try_dequeue(op)) {
    switch (op->target_) {
    case Conn: {
      auto c = reinterpret_cast<NqSessionDelegate *>(op->target_ptr_);
#if defined(USE_DIRECT_WRITE)
      auto m = NqUnwrapper::UnsafeUnwrapMutex(NqConnSerialCodec::IsClient(op->serial_), c);
      std::unique_lock<std::mutex> lock(*m);
      p->LockSession(c->SessionIndex());
#endif
      switch (op->code_) {
      case OpenStream:
        p->InvokeConn(op->serial_, c, op->code_, op->stream_.name_, op->stream_.ctx_, true);
        break;
      case Reachability:
        p->InvokeConn(op->serial_, c, op->code_, op->reachability_.state_, true);
        break;
      case ModifyHandlerMap:
        p->InvokeConn(op->serial_, c, op->code_, op->task_.callback_, true);
        break;
      default:
        p->InvokeConn(op->serial_, c, op->code_, true);
        break;
      }
#if defined(USE_DIRECT_WRITE)
      p->UnlockSession();
#endif
    } break;
    case Stream: {
#if defined(USE_DIRECT_WRITE)
      auto c = reinterpret_cast<NqSessionDelegate *>(op->target_ptr_);
      auto m = NqUnwrapper::UnsafeUnwrapMutex(NqStreamSerialCodec::IsClient(op->serial_), c);
      std::unique_lock<std::mutex> lock(*m);
      p->LockSession(c->SessionIndex());
#endif
      auto s = reinterpret_cast<NqStream *>(op->target_ptr_);
      switch (op->code_) {
      case Disconnect:
        p->InvokeStream(op->serial_, s, op->code_, true);
        break;
      case Task: 
        p->InvokeStream(op->serial_, s, op->code_, op->task_.callback_, true);
        break;
      case Send:
        p->InvokeStream(op->serial_, s, op->code_,
                       op->data_.ptr(), op->data_.length(), true);
        break;
      case SendEx:
        p->InvokeStream(op->serial_, s, op->code_,
                       op->data_.ptr(), op->data_.length(), op->send_ex_.stream_opt_, true);
        break;
      case Call:
        p->InvokeStream(op->serial_, s, op->code_, op->call_.type_, 
                        op->data_.ptr(), op->data_.length(), op->call_.on_reply_, true);
        break;
      case CallEx:
        p->InvokeStream(op->serial_, s, op->code_, op->call_ex_.type_, 
                        op->data_.ptr(), op->data_.length(), op->call_ex_.rpc_opt_, true);
        break;
      case Notify:
        p->InvokeStream(op->serial_, s, op->code_, op->notify_.type_, 
                        op->data_.ptr(), op->data_.length(), true);
        break;
      case Reply:
        p->InvokeStream(op->serial_, s, op->code_, op->reply_.result_, 
                        op->reply_.msgid_, op->data_.ptr(), op->data_.length(), true);
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
        p->InvokeAlarm(op->serial_, a, op->code_,
                      op->alarm_.invocation_ts_, op->alarm_.callback_, true);
        break;
      case Finalize:
      case Exec:
        p->InvokeAlarm(op->serial_, a, op->code_, true);
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
