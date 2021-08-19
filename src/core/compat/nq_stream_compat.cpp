#include "core/nq_stream.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "core/compat/nq_session.h"

namespace net {

NqStreamCompat::NqStreamCompat(QuicStreamId id, NqSession* nq_session, SpdyPriority priority) : 
  QuicStream(id, nq_session) {
  nq_session->RegisterStreamPriority(id, priority);
}
NqSession *NqStreamCompat::nq_session() { 
  return static_cast<NqSession *>(session()); 
}
const NqSession *NqStreamCompat::nq_session() const { 
  return static_cast<const NqSession *>(session()); 
}
void NqStreamCompat::OnDataAvailable() {
  NqQuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), NqQuicConnection::SEND_ACK_IF_QUEUED);
  //greedy read and called back
  struct iovec v[256];
  int n_blocks = sequencer()->GetReadableRegions(v, 256);
  int i = 0;
  size_t consumed = 0;
  for (;i < n_blocks; i++) {
    if (OnRecv(NqStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len)) {
      return;        
    }
    consumed += v[i].iov_len;
  }
  sequencer()->MarkConsumed(consumed);
}
void NqStreamCompat::Send(
  const char *p, nq_size_t len, 
  bool fin, const nq_stream_opt_t *p_opt
) {
  WriteOrBufferData(QuicStringPiece(p, len), fin, 
    p_opt == nullptr ? 
      nullptr : 
      QuicReferenceCountedPointer<QuicAckListenerInterface>(new AckHandler(*p_opt))
  );
}

} //namespace net
#else
#endif
