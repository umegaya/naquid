#pragma once

#include "core/nq_config.h"
#include "core/nq_client_base.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "core/compat/chromium/nq_quic_client.h"

namespace net {
class NqClientLoop;
class NqClientCompat : public NqClientBase {
 public:
  // implements NqClientBase
  bool Initialize() override { return client_.Initialize(); }
  void StartConnect() override { client_.StartConnect(); }
  void StartDisconnect() override { client_.Disconnect(); }
  void ForceShutdown() override { client_.ForceShutdown(); }
  bool MigrateSocket() override { return client_.MigrateSocket(client_.bind_to_address()); }
  NqClientStream *NewStream() override {
    return static_cast<NqClientStream *>(client_.nq_session()->CreateOutgoingDynamicStream());
  }
  int UnderlyingFd() override { 
    return static_cast<NqNetworkHelper *>(client_.network_helper())->fd();
  }


  // implements NqSession::Delegate
  NqQuicConnectionId ConnectionId() override {
    return client_.connection_id();
  }
  void FlushWriteBuffer() override {
    QuicConnection::ScopedPacketBundler bundler(
      client_.nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED); 
  }

  // get/set
  NqQuicClient *quic_client() { return &client_; }

 private:
  friend class NqClient;
  // This will create its own QuicClientEpollNetworkHelper.
  NqClientCompat(NqQuicSocketAddress server_address,
                 NqClientLoop &loop,
                 const NqQuicServerId &server_id,
                 const NqClientConfig &config);
  ~NqClientCompat() override {}

  NqQuicClient client_;
  DISALLOW_COPY_AND_ASSIGN(NqClientCompat);
};
} //net
#else
namespace net {
class NqClientLoop;
class NqClientCompat : public NqClientBase {
 public:
  // implements NqClientBase
  bool Initialize() override { ASSERT(false); }
  void StartConnect() override { ASSERT(false); }
  void StartDisconnect() override { ASSERT(false); }
  void ForceShutdown() override { ASSERT(false); }
  bool MigrateSocket() override { ASSERT(false); return false; }
  NqClientStream *NewStream() override { ASSERT(false); return nullptr; }
  int UnderlyingFd() override { ASSERT(false); return -1; }

  // implements NqSession::Delegate
  NqQuicConnectionId ConnectionId() override {
    ASSERT(false);
    return 0LL;
  }
  void FlushWriteBuffer() override {
    ASSERT(false);
  }

  // operation
  void StartConnect();

 private:
  friend class NqClient;
  NqClientCompat(NqQuicSocketAddress server_address,
                 NqClientLoop &loop,
                 const NqQuicServerId &server_id,
                 const NqClientConfig &config) 
    : NqClientBase(server_address, loop, server_id, config) {}
  ~NqClientCompat() override {}

  DISALLOW_COPY_AND_ASSIGN(NqClientCompat);
};
} //net
#endif