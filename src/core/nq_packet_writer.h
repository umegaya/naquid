#pragma once

#include "net/tools/quic/quic_default_packet_writer.h"
#include "net/quic/platform/api/quic_logging.h"

#include "basis/defs.h"
#include "basis/syscall.h"

namespace net {
class NqPacketWriter : public QuicDefaultPacketWriter {
 protected:
  bool reachability_tracked_;
  static WriteResult WritePacket(int fd,
                                 const char* buffer,
                                 size_t buf_len,
                                 const QuicIpAddress& self_address,
                                 const QuicSocketAddress& peer_address, 
                                 bool reachability_tracked);
 public:
  NqPacketWriter(nq::Fd fd) : QuicDefaultPacketWriter(fd), reachability_tracked_(false) {}
  ~NqPacketWriter() override { reachability_tracked_ = false; }
  void SetReachabilityTracked(bool on) { reachability_tracked_ = on; }
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;
};
}
