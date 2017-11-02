#include "core/nq_client_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_stream.h"

namespace net {
QuicStream* NqClientSession::CreateIncomingDynamicStream(QuicStreamId id) {
  auto s = new NqClientStream(id, this, false);
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
