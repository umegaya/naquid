#pragma once

#include <netinet/in.h>
// Include here to guarantee this header gets included (for MSG_WAITFORONE)
// regardless of how the below transitive header include set may change.
#include <sys/socket.h>

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"

#define MMSG_MORE 0

namespace net {

#if MMSG_MORE
// Read in larger batches to minimize recvmmsg overhead.
const int kNumPacketsPerReadMmsgCall = 16;
#endif

class NaquidPacketReader {
 public:
  class Packet : public QuicReceivedPacket {
    QuicSocketAddress server_address_, client_address_;
    int port_;
   public:
    Packet(const char* buffer,
           size_t length,
           QuicTime receipt_time,
           int ttl,
           bool ttl_valid, 
           struct sockaddr_storage client_sockaddr, 
           QuicIpAddress &server_ip, int server_port);
    QuicSocketAddress &server_address() { return server_address_; }
    QuicSocketAddress &client_address() { return client_address_; }
  };
  class Delegate {
   public:
    virtual void OnRecv(Packet *p) = 0;
  }
 public:
  NaquidPacketReader(Delegate *d);

  ~NaquidPacketReader();

  // Reads a number of packets from the given fd, and then passes them off to
  // the PacketProcessInterface.  Returns true if there may be additional
  // packets available on the socket.
  // Populates |packets_dropped| if it is non-null and the socket is configured
  // to track dropped packets and some packets are read.
  // If the socket has timestamping enabled, the per packet timestamps will be
  // passed to the processor. Otherwise, |clock| will be used.
  bool Read(int fd, int port, const QuicClock& clock, QuicPacketCount* packets_dropped);

  // memory pool
  inline void Pool(char *buffer, Packet *packet) { 
    buffer_pool_.push(buffer);
    packet_pool_.push(reinterpret_cast<char*>(packet));
  }
  inline char *NewBuffer() { 
    return buffer_pool_.size() > 0 ? buffer_pool_.pop() : new char[kMaxPacketSize];
  }
  inline Packet *NewPacket(const char* buffer,
           size_t length,
           QuicTime receipt_time,
           int ttl,
           bool ttl_valid, 
           struct sockaddr_storage client_sockaddr, 
           QuicIpAddress &server_ip, int server_port) { 
    auto p = packet_pool_.size() > 0 ? packet_pool_.pop() : new char[sizeof(Packet)];
    return new(p) Packet(buffer, length, receipt_time, ttl, ttl_valid, client_sockaddr, server_ip, server_port);
  }

 private:
  // Initialize the internal state of the reader.
  void Initialize();

  // Reads and dispatches many packets using recvmmsg.
  bool ReadPacketsMulti(int fd,
                        int port,
                        const QuicClock& clock,
                        QuicPacketCount* packets_dropped);

  // Reads and dispatches a single packet using recvmsg.
  static bool ReadPackets(int fd,
                          int port,
                          const QuicClock& clock,
                          Delegate *delegate,
                          QuicPacketCount* packets_dropped);
 private:
  Delegate *delegate_;
  std::stack<char *> buffer_pool_;
  std::stack<char *> packet_pool_;
  // Storage only used when recvmmsg is available.
#if MMSG_MORE
  struct PacketData {
    iovec iov;
    // raw_address is used for address information provided by the recvmmsg
    // call on the packets.
    struct sockaddr_storage raw_address;
    // cbuf is used for ancillary data from the kernel on recvmmsg.
    char cbuf[QuicSocketUtils::kSpaceForCmsg];
    // buf is used for the data read from the kernel on recvmmsg.
    std::unique_ptr<char> buf;
  };
  // TODO(danzh): change it to be a pointer to avoid the allocation on the stack
  // from exceeding maximum allowed frame size.
  // packets_ and mmsg_hdr_ are used to supply cbuf and buf to the recvmmsg
  // call.
  PacketData packets_[kNumPacketsPerReadMmsgCall];
  mmsghdr mmsg_hdr_[kNumPacketsPerReadMmsgCall];
#endif

  DISALLOW_COPY_AND_ASSIGN(NaquidPacketReader);
};

typedef NaquidPacketReader::Packet NaquidPacket;

}  // namespace net