#pragma once

#include <map>
#include <thread>

#include "net/tools/quic/quic_dispatcher.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"

#include "core/nq_worker.h"
#include "core/nq_boxer.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqWorker;
class NqServerSession;
class NqServerConfig;
class NqDispatcher : public QuicDispatcher, 
                     public nq::IoProcessor,
                     public QuicCryptoServerStream::Helper,
                     public NqPacketReader::Delegate,
                     public NqBoxer {
  typedef NqWorker::InvokeQueue InvokeQueue;
  
  int port_; 
  uint32_t index_, n_worker_;
  const NqServer &server_;
  std::unique_ptr<QuicCryptoServerConfig> crypto_config_;
  InvokeQueue *invoke_queues_;
  NqServerLoop &loop_;
  NqPacketReader &reader_;
  QuicCompressedCertsCache cert_cache_;
  NqSessionContainer<NqServerSession> server_map_;
  std::thread::id thread_id_;
 public:
  //TODO(iyatomi): find proper cache size
  static const int kDefaultCertCacheSize = 16; 
  NqDispatcher(int port, const NqServerConfig& config, 
               std::unique_ptr<QuicCryptoServerConfig> crypto_config, 
               NqWorker &worker);
  void Process(NqPacket *p) {
    ProcessPacket(p->server_address(), p->client_address(), *p);
    reader_.Pool(const_cast<char *>(p->data()), p);
  }
  inline QuicCompressedCertsCache *cert_cache() { return &cert_cache_; }
  inline const QuicCryptoServerConfig *crypto_config() const { return crypto_config_.get(); }
  inline NqLoop *loop() { return &loop_; }
  inline InvokeQueue *invoke_queues() { return invoke_queues_; }
  inline NqSessionIndex new_session_index() { return server_map_.NewIndex(); }
  inline const NqSessionContainer<NqServerSession> &server_map() const { return server_map_; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }


  //implements nq::IoProcessor
  void OnEvent(nq::Fd fd, const Event &e) override;
  void OnClose(nq::Fd fd) override {}
	int OnOpen(nq::Fd fd) override;

  //implements NqPacketReader::Delegate
  void OnRecv(NqPacket *packet) override;

  //implements QuicCryptoServerStream::Helper
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const override {
    return loop_.GetRandomGenerator()->RandUint64();
  }
  // Returns true if |message|, which was received on |self_address| is
  // acceptable according to the visitor's policy. Otherwise, returns false
  // and populates |error_details|.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;

  //implements NqBoxer
  void Enqueue(Op *op) override;
  nq_conn_t Box(NqSession::Delegate *d) override;
  nq_stream_t Box(NqStream *s) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqSession::Delegate **unboxed) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqStream **unboxed) override;
  bool IsClient() const override { return false; }
  bool Valid(uint64_t serial, OpTarget target) const override {
    switch (target) {
    case Conn:
      return server_map().Active(NqConnSerialCodec::ServerSessionIndex(serial));
    case Stream:
      return server_map().Active(NqStreamSerialCodec::ServerSessionIndex(serial));
    default:
      ASSERT(false);
      return false;
    }
  }

 protected:
  //implements QuicDispatcher
  QuicSession* CreateQuicSession(
    QuicConnectionId connection_id,
    const QuicSocketAddress& client_address,
    QuicStringPiece alpn) override;
  void CleanUpSession(SessionMap::iterator it,
                      QuicConnection* connection,
                      bool should_close_statelessly) override;
};
}