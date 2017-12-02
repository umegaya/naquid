#pragma once

#include <map>
#include <thread>

#include "net/tools/quic/quic_dispatcher.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"

#include "basis/allocator.h"
#include "core/nq_worker.h"
#include "core/nq_boxer.h"
#include "core/nq_server_session.h"
#include "core/nq_stream.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqWorker;
class NqServerConfig;
class NqAlarmBase;
class NqDispatcher : public QuicDispatcher, 
                     public nq::IoProcessor,
                     public QuicCryptoServerStream::Helper,
                     public NqPacketReader::Delegate,
                     public NqBoxer,
                     public QuicStreamAllocator {
  static const int kNumSessionsToCreatePerSocketEvent = 1024;
  static const int kDefaultCertCacheSize = 16; 
  typedef NqWorker::InvokeQueue InvokeQueue;
  
  int port_, accept_per_loop_; 
  uint32_t index_, n_worker_;
  const NqServer &server_;
  std::unique_ptr<QuicCryptoServerConfig> crypto_config_;
  InvokeQueue *invoke_queues_;
  NqServerLoop &loop_;
  NqPacketReader &reader_;
  QuicCompressedCertsCache cert_cache_;
  typedef NqObjectExistenceMapMT<NqServerSession, NqSessionIndex> ServerMap;
  ServerMap server_map_;
  NqObjectExistenceMap<NqAlarm, NqAlarmIndex> alarm_map_;
  std::thread::id thread_id_;
  nq::Allocator<NqServerSession> session_allocator_;
  nq::Allocator<NqServerStream> stream_allocator_;

 public:
  NqDispatcher(int port, const NqServerConfig& config, 
               std::unique_ptr<QuicCryptoServerConfig> crypto_config, 
               NqWorker &worker);
  inline void Process(NqPacket *p) {
    ProcessPacket(p->server_address(), p->client_address(), *p);
    reader_.Pool(const_cast<char *>(p->data()), p);
  }
  inline void Accept() {
    ProcessBufferedChlos(accept_per_loop_);
  }
  inline QuicCompressedCertsCache *cert_cache() { return &cert_cache_; }
  inline const QuicCryptoServerConfig *crypto_config() const { return crypto_config_.get(); }
  inline NqLoop *loop() { return &loop_; }
  inline InvokeQueue *invoke_queues() { return invoke_queues_; }
  inline NqSessionIndex new_session_index() { return server_map_.NewIndex(); }
  inline NqAlarmIndex new_alarm_index() { return alarm_map_.NewIndex(); }
  inline const ServerMap &server_map() const { return server_map_; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }
  inline nq::Allocator<NqServerSession> &session_allocator() { return session_allocator_; }
  inline nq::Allocator<NqServerStream> &stream_allocator() { return stream_allocator_; }

  //implements QuicStreamAllocator
  void *Alloc(size_t sz) override { return stream_allocator_.Alloc(sz); }
  void Free(void *p) override { return stream_allocator_.Free(p); }

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
  NqLoop *Loop() override { return &loop_; }
  nq_conn_t Box(NqSession::Delegate *d) override;
  nq_stream_t Box(NqStream *s) override;
  nq_alarm_t Box(NqAlarm *a) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqSession::Delegate **unboxed) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqStream **unboxed) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqAlarm **unboxed) override;
  bool IsClient() const override { return false; }
  const NqSession::Delegate *FindConn(uint64_t serial, OpTarget target) const override;
  const NqStream *FindStream(uint64_t serial) const override;
  void RemoveAlarm(NqAlarmIndex index) override;

 protected:
  void SetFromConfig(const NqServerConfig &conf);
  void AddAlarm(NqAlarm *a);
  
  //implements QuicDispatcher
  QuicSession* CreateQuicSession(
    QuicConnectionId connection_id,
    const QuicSocketAddress& client_address,
    QuicStringPiece alpn) override;
  void OnConnectionClosed(QuicConnectionId connection_id,
                          QuicErrorCode error,
                          const std::string& error_details) override;
};
}