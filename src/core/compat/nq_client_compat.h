#pragma once

#include "core/nq_config.h"
#include "core/nq_client_base.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "core/compat/chromium/nq_quic_client.h"

namespace nq {
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


  // implements NqSession::Delegate
  NqQuicConnectionId ConnectionId() override {
    return client_.connection_id();
  }
  void FlushWriteBuffer() override {
    NqQuicConnection::ScopedPacketBundler bundler(
      client_.nq_session()->connection(), NqQuicConnection::SEND_ACK_IF_QUEUED); 
  }
  int UnderlyingFd() override { 
    return static_cast<chromium::NqNetworkHelper *>(client_.network_helper())->fd();
  }

  // get/set
  chromium::NqQuicClient *chromium() { return &client_; }

 private:
  friend class NqClient;
  // This will create its own QuicClientEpollNetworkHelper.
  NqClientCompat(NqQuicSocketAddress server_address,
                 NqClientLoop &loop,
                 const NqQuicServerId &server_id,
                 const NqClientConfig &config);
  ~NqClientCompat() override {}

  chromium::NqQuicClient client_;
  DISALLOW_COPY_AND_ASSIGN(NqClientCompat);
};
} //net
#else
namespace nq {
class NqClientLoop;
class NqClientCompat : public NqClientBase {
 public:
  // implements NqClientBase
  bool Initialize() override { ASSERT(false); }
  void StartConnect() override;
  void StartDisconnect() override { ASSERT(false); }
  void ForceShutdown() override { ASSERT(false); }
  bool MigrateSocket() override { ASSERT(false); return false; }
  NqClientStream *NewStream() override { ASSERT(false); return nullptr; }

  // implements NqSession::Delegate
  NqQuicConnectionId ConnectionId() override { return conn_id_; }
  void FlushWriteBuffer() override { ASSERT(false); }
  int UnderlyingFd() override { ASSERT(false); }

 private:
  friend class NqClient;
  NqClientCompat(NqQuicSocketAddress server_address,
                 NqClientLoop &loop,
                 const NqQuicServerId &server_id,
                 const NqClientConfig &config) : 
    NqClientBase(loop, server_id, config), 
    server_address_(server_address), server_id_(server_id), conn_(nullptr) {
    config_ = config.NewQuicConfig();
  }
  ~NqClientCompat() override {
    if (conn_ != nullptr) { 
      quiche_conn_free(conn_);
      conn_ = nullptr;
    }
  }

  DISALLOW_COPY_AND_ASSIGN(NqClientCompat);

  NqQuicSocketAddress server_address_;
  NqQuicServerId server_id_;
  std::unique_ptr<NqQuicConfig> config_; 
  NqQuicConnection conn_;
  union  {
      uint8_t conn_id_as_bytes_[sizeof(NqQuicConnectionId)];
      NqQuicConnectionId conn_id_;
  };
};
} //net
#endif