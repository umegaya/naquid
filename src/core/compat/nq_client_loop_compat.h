#pragma once

#include "core/nq_client_loop_base.h"

#if defined(NQ_CHROMIUM_BACKEND)
namespace net {
class NqClientLoopCompat : public NqClientLoopBase,
                           public QuicSession::Visitor,
                           public QuicStreamAllocator {
 protected:
  NqClientLoopCompat(int max_client_hint, int max_stream_hint) : 
    NqClientLoopBase(max_client_hint, max_stream_hint) {}
 public:
  //implements QuicStreamAllocator
  void *Alloc(size_t sz) override { return stream_allocator_.Alloc(sz); }
  void Free(void *p) override { return stream_allocator_.Free(p); }

  //implement QuicSession::Visitor
  void OnConnectionClosed(QuicConnectionId connection_id,
                          QuicErrorCode error,
                          const std::string& error_details) override {}
  // Called when the session has become write blocked.
  void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) override {}
  // Called when the session receives reset on a stream from the peer.
  void OnRstStreamReceived(const QuicRstStreamFrame& frame) override {}
};
} // net
#else
namespace net {
typedef NqClientLoopBase NqClientLoopCompat;
} // net
#endif
