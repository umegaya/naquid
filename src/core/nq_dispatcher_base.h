#pragma once

#include <map>
#include <thread>

#include "basis/allocator.h"
#include "core/nq_worker.h"
#include "core/nq_alarm.h"
#include "core/nq_boxer.h"
#include "core/nq_server_session.h"
#include "core/nq_static_section.h"
#include "core/nq_stream.h"
#include "core/nq_serial_codec.h"

namespace nq {
class NqWorker;
class NqServerConfig;
class NqDispatcherBase : public NqBoxer {
 protected:
  static const int kNumSessionsToCreatePerSocketEvent = 1024;
  static const int kDefaultCertCacheSize = 16; 
  typedef NqWorker::InvokeQueue InvokeQueue;
  typedef NqSessiontMap<NqServerSession, NqSessionIndex> ServerMap;
  typedef NqSessiontMap<NqAlarm, NqAlarmIndex> AlarmMap;
  typedef Allocator<NqServerSession, NqStaticSection> SessionAllocator;
  typedef Allocator<NqServerStream, NqStaticSection> StreamAllocator;
  typedef NqAlarm::Allocator AlarmAllocator;
  
  int port_, accept_per_loop_; 
  uint32_t index_, n_worker_, session_limit_;
  NqServer &server_;
  const NqServerConfig &config_;
  InvokeQueue *invoke_queues_; //only owns index_ th index. 
  NqServerLoop &loop_;
  std::thread::id thread_id_;
  ServerMap server_map_;
  AlarmMap alarm_map_;
  SessionAllocator session_allocator_;
  StreamAllocator stream_allocator_;
  AlarmAllocator alarm_allocator_;

 public:
  NqDispatcherBase(int port, const NqServerConfig& config, NqWorker &worker);
  virtual ~NqDispatcherBase() {}

  //interface NqDispatcherBase
  virtual void Shutdown() = 0;
  virtual bool ShutdownFinished(nq_time_t shutdown_start) const = 0;
  virtual void Accept() = 0;
  virtual void SetFromConfig(const NqServerConfig &conf) = 0;

  //get/set
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
  inline IdFactory<uint32_t> &stream_index_factory() { return server_.stream_index_factory(); }

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
  void AddAlarm(NqAlarm *a);
};
} // namespace nq