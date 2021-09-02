#pragma once

#include "core/nq_dispatcher_base.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"

#include "core/compat/chromium/nq_packet_reader.h"
#include "core/compat/chromium/nq_quic_dispatcher.h"

namespace nq {
using namespace net;
class NqDispatcherCompat : public NqDispatcherBase,
                           public IoProcessor,
                           public chromium::NqPacketReader::Delegate,
                           public QuicStreamAllocator, 
                           public QuicSessionAllocator {
 public:
  NqDispatcherCompat(int port, const NqServerConfig& config, NqWorker &worker);
  ~NqDispatcherCompat() override {}

  // operation
  inline void Process(NqPacket *packet) { dispatcher_.Process(packet); }

  // get/set
  inline QuicCompressedCertsCache *cert_cache() { return &cert_cache_; }
  inline const QuicCryptoServerConfig *crypto_config() const { return crypto_config_.get(); }
  inline chromium::NqQuicDispatcher *chromium() { return &dispatcher_; }

  //implements NqDispatcherBase
  void Accept() override { dispatcher_.ProcessBufferedChlos(accept_per_loop_); }
  void Shutdown() override;
  bool ShutdownFinished(nq_time_t shutdown_start) const override;
  void SetFromConfig(const NqServerConfig &config) override;

  //implements IoProcessor
  void OnEvent(Fd fd, const Event &e) override;
  void OnClose(Fd fd) override {}
	int OnOpen(Fd fd) override;

  //implements NqPacketReader::Delegate
  void OnRecv(NqPacket *packet) override;

  //implements QuicStreamAllocator
  void *Alloc(size_t sz) override { return stream_allocator_.Alloc(sz); }
  void Free(void *p) override { return stream_allocator_.Free(p); }

  //implements QuicSessionAllocator
  void *AllocSession(size_t sz) override { return session_allocator_.Alloc(sz); }
  void FreeSession(void *p) override { return session_allocator_.Free(p); }  

  //chromium::NqQuicDispatcher delegates
  QuicSession* CreateQuicSession(
    QuicConnectionId connection_id,
    const QuicSocketAddress& client_address,
    QuicStringPiece alpn);
  void OnSessionClosed(NqServerSession *session);

  //delegate QuicCryptoServerStream::Helper (called from chromium::NqQuicDispatcher)
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const {
    return loop_.GetRandomGenerator()->RandUint64();
  }
  // Returns true if |message|, which was received on |self_address| is
  // acceptable according to the visitor's policy. Otherwise, returns false
  // and populates |error_details|.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const;

 private:
  std::unique_ptr<QuicCryptoServerConfig> crypto_config_;
  QuicCompressedCertsCache cert_cache_;
  chromium::NqQuicDispatcher dispatcher_;
};
} // namespace nq
#else
namespace nq {
class NqDispatcherCompat : public NqDispatcherBase,                           
                           public IoProcessor {
 public:
  NqDispatcherCompat(int port, const NqServerConfig& config, NqWorker &worker) :
    NqDispatcherBase(port, config, worker) {}
  ~NqDispatcherCompat() override {}

  // operation
  inline void Process(NqPacket *packet) { ASSERT(false); }

  //implements IoProcessor
  void OnEvent(Fd fd, const Event &e) override;
  void OnClose(Fd fd) override {}
	int OnOpen(Fd fd) override;
  
  //implements NqDispatcherBase
  void Accept() override { ASSERT(false); }
  void Shutdown() override { ASSERT(false); }
  bool ShutdownFinished(nq_time_t shutdown_start) const override { ASSERT(false); return false; }
  void SetFromConfig(const NqServerConfig &config) override { ASSERT(false); }
};
} //namespace nq
#endif