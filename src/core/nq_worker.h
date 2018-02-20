#pragma once

#include <map>
#include <vector>
#include <thread>

#include "MoodyCamel/concurrentqueue.h"

#include "core/nq_server_loop.h"
#include "core/nq_packet_reader.h"
#include "core/nq_boxer.h"

namespace net {
class NqServer;
class NqDispatcher;
class NqWorker {
  uint32_t index_;
  NqServer &server_;
  NqServerLoop loop_;
  NqPacketReader reader_;
  std::thread thread_;
  //TODO(iyatomi): measture this to confirm
  //almost case, should only have a few element. I think linear scan of vector faster
  std::vector<std::pair<int, NqDispatcher*>> dispatchers_;
  bool overflow_supported_;
 public:
  typedef moodycamel::ConcurrentQueue<NqPacket*> PacketQueue;
  typedef NqBoxer::Processor InvokeQueue;
  NqWorker(uint32_t index, NqServer &server) : 
    index_(index), server_(server), loop_(), reader_(), 
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
  inline NqPacketReader &reader() { return reader_; }
  inline NqServerLoop &loop() { return loop_; }
  inline uint32_t index() { return index_; }
  inline NqServer &server() { return server_; }
  inline std::thread::id thread_id() const { return thread_.get_id(); }

 protected:
  static bool ToSocketAddress(const nq_addr_t &addr, QuicSocketAddress &address);
  nq::Fd CreateUDPSocketAndBind(const QuicSocketAddress& address);
};
}