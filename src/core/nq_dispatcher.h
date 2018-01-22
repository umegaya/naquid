#pragma once

#include <map>
#include <thread>

#include "net/tools/quic/quic_dispatcher.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"

#include "basis/allocator.h"
#include "core/nq_worker.h"
#include "core/nq_alarm.h"
#include "core/nq_boxer.h"
#include "core/nq_server_session.h"
#include "core/nq_stream.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqWorker;
class NqServerConfig;
class NqDispatcher : public QuicDispatcher, 
                     public nq::IoProcessor,
                     public QuicCryptoServerStream::Helper,
                     public NqPacketReader::Delegate,
                     public NqBoxer,
                     public QuicStreamAllocator, 
                     public QuicSessionAllocator {
  static const int kNumSessionsToCreatePerSocketEvent = 1024;
  static const int kDefaultCertCacheSize = 16; 
  typedef NqWorker::InvokeQueue InvokeQueue;
  typedef NqSessiontMap<NqServerSession, NqSessionIndex> ServerMap;
  typedef NqSessiontMap<NqAlarm, NqAlarmIndex> AlarmMap;
  typedef nq::Allocator<NqServerSession, NqStaticSection> SessionAllocator;
  typedef nq::Allocator<NqServerStream, NqStaticSection> StreamAllocator;
  typedef NqAlarm::Allocator AlarmAllocator;
  
  int port_, accept_per_loop_; 
  uint32_t index_, n_worker_;
  const NqServer &server_;
  std::unique_ptr<QuicCryptoServerConfig> crypto_config_;
  InvokeQueue *invoke_queues_; //only owns index_ th index. 
  NqServerLoop &loop_;
  NqPacketReader &reader_;
  QuicCompressedCertsCache cert_cache_;
  std::thread::id thread_id_;
  ServerMap server_map_;
  AlarmMap alarm_map_;
  SessionAllocator session_allocator_;
  StreamAllocator stream_allocator_;
  AlarmAllocator alarm_allocator_;
  nq::IdFactory<uint32_t> stream_index_factory_;

 public:
  NqDispatcher(int port, const NqServerConfig& config, 
               std::unique_ptr<QuicCryptoServerConfig> crypto_config, 
               NqWorker &worker);
  inline void Process(NqPacket *p) {
    {
      //get NqServerSession's mutex, which is corresponding to this packet's connection id
#if !defined(USE_DIRECT_WRITE)
      ProcessPacket(p->server_address(), p->client_address(), *p);        
#else
      auto cid = p->ConnectionId();
      auto s = FindByConnectionId(cid);
      if (s != nullptr) {
        std::unique_lock<std::mutex> session_lock(s->static_mutex());
        loop_.LockSession(s->session_index());
        ProcessPacket(p->server_address(), p->client_address(), *p);
        loop_.UnlockSession();
      } else {
        ProcessPacket(p->server_address(), p->client_address(), *p);        
      }
#endif
    }
    reader_.Pool(const_cast<char *>(p->data()), p);
  }
  inline void Accept() { ProcessBufferedChlos(accept_per_loop_); }
  inline QuicCompressedCertsCache *cert_cache() { return &cert_cache_; }
  inline const QuicCryptoServerConfig *crypto_config() const { return crypto_config_.get(); }
  inline NqLoop *loop() { return &loop_; }
  inline InvokeQueue *invoke_queues() { return invoke_queues_; }
  inline const ServerMap &server_map() const { return server_map_; }
  inline ServerMap &server_map() { return server_map_; }
  inline int worker_num() const { return n_worker_; }
  inline int worker_index() const { return index_; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }
  inline StreamAllocator &stream_allocator() { return stream_allocator_; }
  //avoid confusing with QuicSession::session_allocator
  inline SessionAllocator &session_allocator_body() { return session_allocator_; }
  inline nq::IdFactory<uint32_t> &stream_index_factory() { return stream_index_factory_; }

  //implements QuicStreamAllocator
  void *Alloc(size_t sz) override { return stream_allocator_.Alloc(sz); }
  void Free(void *p) override { return stream_allocator_.Free(p); }

  //implements QuicSessionAllocator
  void *AllocSession(size_t sz) override { return session_allocator_.Alloc(sz); }
  void FreeSession(void *p) override { return session_allocator_.Free(p); }  

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
  bool MainThread() const override { return main_thread(); }
  NqLoop *Loop() override { return &loop_; }
  NqAlarm *NewAlarm() override;
  AlarmAllocator *GetAlarmAllocator() override { return &alarm_allocator_; }
  bool IsClient() const override { return false; }
  bool IsSessionLocked(NqSessionIndex idx) const override { return loop_.IsSessionLocked(idx); }
  void LockSession(NqSessionIndex idx) override { loop_.LockSession(idx); }
  void UnlockSession() override { loop_.UnlockSession(); }
  void RemoveAlarm(NqAlarmIndex index) override;

 protected:
  void SetFromConfig(const NqServerConfig &conf);
  void AddAlarm(NqAlarm *a);

  inline NqServerSession *FindByConnectionId(QuicConnectionId cid) {
    auto it = session_map().find(cid);
    if (it != session_map().end()) {
      return static_cast<NqServerSession*>(const_cast<QuicSession *>(it->second.get()));
    }
    return nullptr;
  }
  
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