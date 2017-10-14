#pragma once

#include <map>
#include <thread>

#include "MoodyCamel/concurrentqueue.h"

#include "interop/naquid_server_loop.h"
#include "interop/naquid_packet_reader.h"

namespace net {
class NaquidServer;
class NaquidDispatcher;
class NaquidWorker {
  int index_;
  const NaquidServer &server_;
  NaquidServerLoop loop_;
  NaquidPacketReader reader_;
  std::thread thread_;
  //TODO(iyatomi): measture this to confirm
  //almost case, should only has a few element. I think linear scan of vector faster
  std::vector<std::pair<int, NaquidDispatcher*>> dispatchers_;
  bool overflow_supported_;
 public:
  typedef moodycamel::ConcurrentQueue<NaquidPacket*> PacketQueue;
  NaquidWorker(int index, const NaquidServer &server) : 
    index_(index), server_(server), loop_(), reader_(), 
    thread_(), dispatchers_(), overflow_supported_(false) {}
  void Start(PacketQueue &queue) {
    thread_ = std::thread([this, &queue]() { Run(queue); });
  }
  void Process(NaquidPacket *p);
  bool Listen();
  void Run(PacketQueue &queue);
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  //accessor
  inline const NaquidServer &server() const { return server_; }
  inline NaquidPacketReader &reader() { return reader_; }
  inline NaquidServerLoop &loop() { return loop_; }
  inline int index() { return index_; }

 protected:
  static bool ToSocketAddress(const nq_addr_t &addr, QuicSocketAddress &address);
  nq::Fd CreateUDPSocketAndBind(const QuicSocketAddress& address);
};
}