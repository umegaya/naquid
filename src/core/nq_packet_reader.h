#pragma once

#include <netinet/in.h>
// Include here to guarantee this header gets included (for MSG_WAITFORONE)
// regardless of how the below transitive header include set may change.
#include <sys/socket.h>

#include <stack>

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"

#include "basis/endian.h"

#if defined(__linux__)
#define MMSG_MORE 1
#else
#define MMSG_MORE 0
#endif

namespace net {

#if MMSG_MORE
// Read in larger batches to minimize recvmmsg overhead.
const int kNumPacketsPerReadMmsgCall = 16;
#endif

class NqPacketReader {
 public:
  class Packet : public QuicReceivedPacket {
    QuicSocketAddress client_address_, server_address_;
    int port_;
   public:
    Packet(const char* buffer,
           size_t length,
           QuicTime receipt_time,
           int ttl,
           bool ttl_valid, 
           struct sockaddr_storage client_sockaddr, 
           QuicIpAddress &server_ip, int server_port);
    inline QuicSocketAddress &server_address() { return server_address_; }
    inline QuicSocketAddress &client_address() { return client_address_; }
    inline void set_port(int port) { port_ = port; }
    inline int port() const { return port_; }
    inline uint64_t ConnectionId() const {
      switch (data()[0] & 0x08) {
        case 0x08:
          return nq::Endian::NetbytesToHost<uint64_t>(data() + 1);
        default:
          //TODO(iyatomi): can we detect connection_id from packet->client_address()? but chromium server sample itself seems to ignore...
          return 0;
      }
    } 
  };
  class Delegate {
   public:
    virtual void OnRecv(Packet *p) = 0;
  };
 public:
  NqPacketReader();

  ~NqPacketReader();

  // Reads a number of packets from the given fd, and then passes them off to
  // the PacketProcessInterface.  Returns true if there may be additional
  // packets available on the socket.
  // Populates |packets_dropped| if it is non-null and the socket is configured
  // to track dropped packets and some packets are read.
  // If the socket has timestamping enabled, the per packet timestamps will be
  // passed to the processor. Otherwise, |clock| will be used.
  bool Read(int fd, int port, const QuicClock& clock, 
            Delegate *delegate, QuicPacketCount* packets_dropped);

  // memory pool
  inline void Pool(char *buffer, Packet *packet) { 
    buffer_pool_.push(buffer);
    packet_pool_.push(reinterpret_cast<char*>(packet));
  }
  inline char *NewBuffer() { 
    if (buffer_pool_.size() > 0) {
      auto p = buffer_pool_.top();
      buffer_pool_.pop();
      return p;
    } else {
      return new char[kMaxPacketSize];
    }
  }
  inline Packet *NewPacket(const char* buffer,
           size_t length,
           QuicTime receipt_time,
           int ttl,
           bool ttl_valid, 
           struct sockaddr_storage client_sockaddr, 
           QuicIpAddress &server_ip, int server_port) { 
    char *p;
    if (packet_pool_.size() > 0) {
      p = packet_pool_.top();
      packet_pool_.pop();
    } else {
      p =new char[sizeof(Packet)];
    }
    return new(p) Packet(buffer, length, receipt_time, ttl, ttl_valid, client_sockaddr, server_ip, server_port);
  }

 private:
  // Initialize the internal state of the reader.
  void Initialize();

  // Reads and dispatches many packets using recvmmsg.
  bool ReadPacketsMulti(int fd,
                        int port,
                        const QuicClock& clock,
                        Delegate *delegate,
                        QuicPacketCount* packets_dropped);

  // Reads and dispatches a single packet using recvmsg.
  bool ReadPackets(int fd,
                          int port,
                          const QuicClock& clock,
                          Delegate *delegate,
                          QuicPacketCount* packets_dropped);
 private:
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
    char *buf;
  };
  // TODO(danzh): change it to be a pointer to avoid the allocation on the stack
  // from exceeding maximum allowed frame size.
  // packets_ and mmsg_hdr_ are used to supply cbuf and buf to the recvmmsg
  // call.
  PacketData packets_[kNumPacketsPerReadMmsgCall];
  mmsghdr mmsg_hdr_[kNumPacketsPerReadMmsgCall];
  int last_packets_read_;
#endif

  DISALLOW_COPY_AND_ASSIGN(NqPacketReader);
};

typedef NqPacketReader::Packet NqPacket;

}  // namespace net