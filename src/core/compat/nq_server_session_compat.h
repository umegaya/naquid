#pragma once

#include <map>
#include <mutex>

#include "core/compat/nq_session.h"
#include "core/nq_session_delegate.h"
#include "core/nq_server.h"
#include "core/nq_config.h"

#if defined(NQ_CHROMIUM_BACKEND)
namespace net {
class NqStream;
class NqServerSessionCompat : public NqSession,
                              public NqSessionDelegate {
 public:
  NqServerSessionCompat(NqQuicConnection *connection,
                  const NqServer::PortConfig &port_config);
  ~NqServerSessionCompat() override {}

  //get/set
  NqDispatcher *dispatcher();

  //operation
  NqQuicCryptoStream *NewCryptoStream();
  NqStream *NewStream();
  NqStreamIndex NewStreamIndex();

  //implements QuicSession
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicStream* CreateOutgoingDynamicStream() override;

  //per backend implementation body for NqSessionDelegate
  void DisconnectImpl();
  NqQuicConnectionId ConnectionIdImpl() { return connection()->connection_id(); }
  void FlushWriteBufferImpl() { 
    NqQuicConnection::ScopedPacketBundler bundler(connection(), NqQuicConnection::SEND_ACK_IF_QUEUED); 
  }
};
} // namespace net
#else
namespace net {
class NqStream;
class NqServerSessionCompat : public NqSession,
                              public NqSessionDelegate {
 public:
  NqServerSessionCompat(NqQuicConnection *connection,
                  const NqServer::PortConfig &port_config);
  ~NqServerSessionCompat() override {}

  //get/set
  NqDispatcher *dispatcher();

  //operation
  NqQuicCryptoStream *NewCryptoStream();
  NqStream *NewStream();
  NqStreamIndex NewStreamIndex();

  //implements QuicSession
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicStream* CreateOutgoingDynamicStream() override;

  //per backend implementation body for NqSessionDelegate
  void DisconnectImpl() { ASSERT(false); }
  NqQuicConnectionId ConnectionIdImpl() { ASSERT(false); return 0LL; }
  void FlushWriteBufferImpl() { ASSERT(false); }
};
} // namespace net
#endif
