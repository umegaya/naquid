#pragma once

#include <map>
#include <thread>

#include "MoodyCamel/concurrentqueue.h"

#include "core/nq_server_loop.h"
#include "core/nq_packet_reader.h"

namespace net {
class NqServer;
class NqDispatcher;
class NqWorker {
  uint32_t index_;
  const NqServer &server_;
  NqServerLoop loop_;
  NqPacketReader reader_;
  std::thread thread_;
  //TODO(iyatomi): measture this to confirm
  //almost case, should only has a few element. I think linear scan of vector faster
  std::vector<std::pair<int, NqDispatcher*>> dispatchers_;
  bool overflow_supported_;
 public:
  typedef moodycamel::ConcurrentQueue<NqPacket*> PacketQueue;
  NqWorker(uint32_t index, const NqServer &server) : 
    index_(index), server_(server), loop_(), reader_(), 
    thread_(), dispatchers_(), overflow_supported_(false) {}
  void Start(PacketQueue &queue) {
    thread_ = std::thread([this, &queue]() { Run(queue); });
  }
  void Process(NqPacket *p);
  bool Listen();
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

 protected:
  static bool ToSocketAddress(const nq_addr_t &addr, QuicSocketAddress &address);
  nq::Fd CreateUDPSocketAndBind(const QuicSocketAddress& address);
};
}