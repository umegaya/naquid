#include "core/nq_client_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_stream.h"
#include "core/nq_client.h"

namespace net {
QuicStream* NqClientSession::CreateIncomingDynamicStream(QuicStreamId id) {
  auto s = new NqClientStream(id, this, false);
  auto c = static_cast<NqClient *>(delegate());
  c->stream_manager().OnOpen("", s);
  s->InitHandle();
  ActivateStream(QuicWrapUnique(s));
  return s;
}
QuicStream* NqClientSession::CreateOutgoingDynamicStream() {
  auto s = new NqClientStream(GetNextOutgoingStreamId(), this, true);
  //InitHandle called in NqClient::NewStream, after name_id and index_per_name_id decided.
  ActivateStream(QuicWrapUnique(s));
  return s;
}
}
