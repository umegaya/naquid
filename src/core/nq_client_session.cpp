#include "core/nq_client_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_stream.h"
#include "core/nq_client_loop.h"

namespace net {
QuicStream* NqClientSession::CreateIncomingDynamicStream(QuicStreamId id) {
  auto c = static_cast<NqClient *>(delegate());
  auto s = new(c->client_loop()) NqClientStream(id, this, false);
  c->stream_manager().OnOpen("", s);
  s->InitSerial();
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NqClientSession::CreateOutgoingDynamicStream() {
  auto c = static_cast<NqClient *>(delegate());
  auto s = new(c->client_loop()) NqClientStream(GetNextOutgoingStreamId(), this, true);
  //InitSerial and StreamManager::OnOpen called in NqClient::NewStream.
  ActivateStream(QuicWrapUnique(s));
  return s;
}
}
