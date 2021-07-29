#include "core/nq_dispatcher.h"

#include "core/nq_server_session.h"
#include "core/nq_server.h"

namespace net {
NqDispatcherBase::NqDispatcherBase(int port, const NqServerConfig& config, NqWorker &worker)
  : port_(port), 
  accept_per_loop_(config.server().accept_per_loop <= 0 ? kNumSessionsToCreatePerSocketEvent : config.server().accept_per_loop),
  index_(worker.index()), n_worker_(worker.server().n_worker()), 
  session_limit_(config.server().use_max_session_hint_as_limit ? config.server().max_session_hint : 0), 
  server_(worker.server()), config_(config), loop_(worker.loop()),
  thread_id_(worker.thread_id()), server_map_(), alarm_map_(), 
  session_allocator_(config.server().max_session_hint), stream_allocator_(config.server().max_stream_hint),
  alarm_allocator_(config.server().max_session_hint) {
  invoke_queues_ = server_.InvokeQueuesFromPort(port);
  ASSERT(invoke_queues_ != nullptr);
}


//implements NqBoxer
void NqDispatcherBase::Enqueue(Op *op) {
  //TODO(iyatomi): NqDispatcherBase owns invoke_queue
  invoke_queues_[index_].enqueue(op);
}
NqAlarm *NqDispatcherBase::NewAlarm() {
  auto a = new(this) NqAlarm();
  auto idx = alarm_map_.Add(a);
  nq_serial_t s;
  NqAlarmSerialCodec::ServerEncode(s, idx);
  a->InitSerial(s);
  return a;
}
void NqDispatcherBase::RemoveAlarm(NqAlarmIndex index) {
  alarm_map_.Remove(index);
}
}

