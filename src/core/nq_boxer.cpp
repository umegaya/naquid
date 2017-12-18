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
      auto st = reinterpret_cast<NqStream *>(op->target_ptr_);
      p->InvokeStream(op->serial_, op->code_, st, true);
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
