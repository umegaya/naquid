#pragma once

#include <string>
#include <thread>

#include "net/quic/core/quic_server_id.h"
#include "net/quic/core/quic_version_manager.h"

#include "basis/allocator.h"
#include "core/nq_loop.h"
#include "core/nq_alarm.h"
#include "core/nq_config.h"
#include "core/nq_boxer.h"
#include "core/nq_client.h"
#include "core/nq_stream.h"

namespace net {
class NqClientLoop : public NqLoop,
                     public NqBoxer,
                     public QuicSession::Visitor,
                     public QuicStreamAllocator {
  typedef NqSessiontMap<NqClient, NqSessionIndex> ClientMap;
  typedef NqSessiontMap<NqAlarm, NqAlarmIndex> AlarmMap;
  typedef nq::AllocatorWithBSS<NqClient, NqStaticSection> ClientAllocator;
  typedef nq::Allocator<NqClientStream> StreamAllocator;
  typedef NqAlarm::Allocator AlarmAllocator;
  nq::HandlerMap handler_map_;
  ClientMap client_map_;
  AlarmMap alarm_map_;
  NqBoxer::Processor processor_;
  QuicVersionManager versions_;
  std::thread::id thread_id_;
  ClientAllocator client_allocator_;
  StreamAllocator stream_allocator_;
  AlarmAllocator alarm_allocator_;

 public:
  NqClientLoop(int max_client_hint, int max_stream_hint) : handler_map_(), client_map_(), alarm_map_(), 
    processor_(), versions_(net::AllSupportedVersions()),
    client_allocator_(max_client_hint), stream_allocator_(max_stream_hint), alarm_allocator_() {
    set_main_thread();
  }
  ~NqClientLoop() { Close(); }

  void Poll();
  void Close();
  void RemoveClient(NqClient *cl);
  NqClient *Create(const std::string &host, 
                   int port, 
                   NqClientConfig &config);

  inline nq::HandlerMap *mutable_handler_map() { return &handler_map_; }
  inline const nq::HandlerMap *handler_map() const { return &handler_map_; }
  inline nq_client_t ToHandle() { return (nq_client_t)this; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }
  inline void set_main_thread() { thread_id_ = std::this_thread::get_id(); }
  inline const ClientMap &client_map() const { return client_map_; }
  inline ClientMap &client_map() { return client_map_; }
  inline ClientAllocator &client_allocator() { return client_allocator_; }

  static inline NqClientLoop *FromHandle(nq_client_t cl) { return (NqClientLoop *)cl; }
  static bool ParseUrl(const std::string &host, 
                       int port, 
                       int address_family,
                       QuicServerId& server_id, 
                       QuicSocketAddress &address, 
                       QuicConfig &config);

  //implements QuicStreamAllocator
  void *Alloc(size_t sz) override { return stream_allocator_.Alloc(sz); }
  void Free(void *p) override { return stream_allocator_.Free(p); }

  //implements NqBoxer
  void Enqueue(NqBoxer::Op *op) override { processor_.enqueue(op); }
  bool MainThread() const override { return main_thread(); }
  NqLoop *Loop() override { return this; }
  NqAlarm *NewAlarm() override;
  AlarmAllocator *GetAlarmAllocator() override { return &alarm_allocator_; }
  bool IsClient() const override { return true; }
  bool IsSessionLocked(NqSessionIndex idx) const override { return NqLoop::IsSessionLocked(idx); };
  NqSession::Delegate *FindConn(uint64_t serial, NqBoxer::OpTarget target) override;
  NqStream *FindStream(uint64_t serial, void *p) override;
  void RemoveAlarm(NqAlarmIndex index) override;

  //implement QuicSession::Visitor
  void OnConnectionClosed(QuicConnectionId connection_id,
                          QuicErrorCode error,
                          const std::string& error_details) override {}
  // Called when the session has become write blocked.
  void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) override {}
  // Called when the session receives reset on a stream from the peer.
  void OnRstStreamReceived(const QuicRstStreamFrame& frame) override {}

 protected:
  void AddAlarm(NqAlarm *a);
};
}
