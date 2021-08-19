#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "net/tools/quic/quic_client_base.h"
#include "net/tools/quic/quic_process_packet_interface.h"
//#include "net/tools/quic/quic_packet_reader.h"

#include "basis/io_processor.h"
#include "core/nq_loop.h"
#include "core/compat/chromium/nq_packet_reader.h"

namespace net {
// An implementation of the QuicClientBase::NetworkHelper based off
// the epoll server.
class NqClientCompat;
class NqQuicClient;
class NqNetworkHelper : public QuicClientBase::NetworkHelper,
                        public nq::IoProcessor,
                        public NqPacketReader::Delegate {
 public:
  // Create a quic client, which will have events managed by an externally owned
  // EpollServer.
  NqNetworkHelper(NqLoop* loop, NqClientCompat* client);
  ~NqNetworkHelper() override;

  // implements nq::IoProcessor
  typedef nq::Fd Fd;
  typedef nq::IoProcessor::Event Event;
  void OnEvent(Fd fd, const Event &e) override;
  void OnClose(Fd fd) override;
  int OnOpen(Fd fd) override;

  // implements ProcessPacketInterface. 
  // This will be called for each received packet.
  void OnRecv(NqPacket *p) override;

  // implements NetworkHelper.
  void RunEventLoop() override;
  bool CreateUDPSocketAndBind(QuicSocketAddress server_address,
                              QuicIpAddress bind_to_address,
                              int bind_to_port) override;
  void CleanUpAllUDPSockets() override;
  QuicSocketAddress GetLatestClientAddress() const override;
  QuicPacketWriter* CreateQuicPacketWriter() override;


  // Accessors provided for convenience, not part of any interface.
  NqClientCompat* client() { return client_; }  

  Fd fd() { return fd_; }

 private:
  // If |fd| is an open UDP socket, unregister and close it. Otherwise, do
  // nothing.
  void CleanUpUDPSocket(Fd fd);

  // Actually clean up |fd|.
  void CleanUpUDPSocketImpl(Fd fd);

  // Listens for events on the client socket.
  NqLoop* loop_;

  // single file descriptor for client connection
  Fd fd_;

  // socket address
  QuicSocketAddress address_;

  // If overflow_supported_ is true, this will be the number of packets dropped
  // during the lifetime of the server.
  QuicPacketCount packets_dropped_;

  // True if the kernel supports SO_RXQ_OVFL, the number of packets dropped
  // because the socket would otherwise overflow.
  bool overflow_supported_;

  // Point to a QuicPacketReader object on the heap. The reader allocates more
  // space than allowed on the stack.
  std::unique_ptr<NqPacketReader> packet_reader_;

  NqClientCompat* client_;

  DISALLOW_COPY_AND_ASSIGN(NqNetworkHelper);
};

}  // namespace net
