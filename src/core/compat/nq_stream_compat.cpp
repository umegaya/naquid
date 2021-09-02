#include "core/nq_stream.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "core/compat/nq_session.h"
#include "core/nq_client_loop.h"
#include "core/nq_dispather.h"

namespace nq {
using namespace net;

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
NqDispatcher *NqStreamCompat::dispatcher() {
  return static_cast<NqDispatcher *>(stream_allocator());
}
NqClientLoop *NqStreamCompat::client_loop() {
  return static_cast<NqClientLoop *>(stream_allocator())
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

} //namespace nq
#else
#include "core/nq_client_loop.h"
#include "core/nq_dispatcher.h"

namespace nq {
void* NqStreamCompat::operator new(std::size_t sz, NqClientLoop *a) {
  volatile auto r = static_cast<NqClientStream *>(a->stream_allocator().Alloc(sz));
  r->client_loop_ = a;
  r->allocator_type_ = ClientLoop;
  return r;
}
void NqStreamCompat::operator delete(void *p, NqClientLoop *a) noexcept {
  a->stream_allocator().Free(p);
}
void* NqStreamCompat::operator new(std::size_t sz, NqDispatcher *a) {
  volatile auto r = static_cast<NqServerStream *>(a->stream_allocator().Alloc(sz));
  r->dispatcher_ = a;
  r->allocator_type_ = Dispatcher;
  return r;
}
void NqStreamCompat::operator delete(void *p, NqDispatcher *a) noexcept {
  a->stream_allocator().Free(p);
}
void NqStreamCompat::operator delete(void *p) noexcept {
  auto r = reinterpret_cast<NqStreamCompat *>(p);
  if (r->allocator_ptr_ == nullptr) {
    std::free(r);
  } else {
    switch (r->allocator_type_) {
      case ClientLoop:
        r->client_loop_->stream_allocator().Free(p);
        break;
      case Dispatcher:
        r->dispatcher_->stream_allocator().Free(p);
        break;
      default:
        ASSERT(false);
        std::free(r);
        break;
    }
  }
}
void NqStreamCompat::Send(const char *p, nq_size_t len, bool fin, const nq_stream_opt_t *p_opt) {
  ASSERT(false);
}
}
#endif
