#pragma once

#include <map>
#include <thread>

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
  NaquidWorker(int index, const NaquidServer &server) : 
    index_(index), server_(server), loop_(), reader_(this), 
    thread_(), dispatchers_(), overflow_supported_(false) {}
  void Start(NaquidServer::PacketQueue &queue) {
    thread_ = std::thread([this, &queue]() { Run(queue); })
    return NQ_OK;
  }
  void Process(NaquidPacket *p);
  bool Listen();
  void Run(NaquidServer::PacketQueue &queue);
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  inline const NaquidServer &server() const { return server_; }
  inline NaquidPacketReader &reader() { return reader_; }
  inline NaquidServerLoop &loop() { return loop_; }

 protected:
  static bool ToSocketAddress(nq_addr_t &addr, QuicSocketAddress &address);
  nq::Fd CreateUDPSocketAndBind(const QuicSocketAddress& address);
}
}