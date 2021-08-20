#pragma once

#include <map>
#include <thread>

#include "MoodyCamel/concurrentqueue.h"

#include "core/compat/nq_worker_compat.h"
#include "core/nq_server_loop.h"
#include "core/nq_boxer.h"

namespace nq {
class NqServer;
class NqDispatcher;
class NqWorker : public NqWorkerCompat {
  uint32_t index_;
  NqServer &server_;
  NqServerLoop loop_;
  std::thread thread_;
  //TODO(iyatomi): measture this to confirm
  //almost case, should only have a few element. I think linear scan of vector faster
  std::vector<std::pair<int, NqDispatcher*>> dispatchers_;
  bool overflow_supported_;
 public:
  typedef moodycamel::ConcurrentQueue<NqPacket*> PacketQueue;
  typedef NqBoxer::Processor InvokeQueue;
  NqWorker(uint32_t index, NqServer &server) : 
    NqWorkerCompat(), index_(index), server_(server), loop_(),
    thread_(), dispatchers_(), overflow_supported_(false) {}
  void Start(PacketQueue &pq) {
    thread_ = std::thread([this, &pq]() { Run(pq); });
  }
  void Process(NqPacket *p);
  bool Listen(InvokeQueue **iq, NqDispatcher **ds);
  void Run(PacketQueue &queue);
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  //accessor
  inline const NqServer &server() const { return server_; }
  inline NqServerLoop &loop() { return loop_; }
  inline uint32_t index() { return index_; }
  inline NqServer &server() { return server_; }
  inline std::thread::id thread_id() const { return thread_.get_id(); }

 protected:
  static bool ToSocketAddress(const nq_addr_t &addr, NqQuicSocketAddress &address);
  Fd CreateUDPSocketAndBind(const NqQuicSocketAddress& address);

 private:
  DISALLOW_COPY_AND_ASSIGN(NqWorker);
};
}