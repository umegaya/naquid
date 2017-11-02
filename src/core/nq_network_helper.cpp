#include "core/nq_network_helper.h"

#include "net/tools/quic/quic_default_packet_writer.h"

#include "basis/syscall.h"
#include "core/nq_client_loop.h"

namespace net {

namespace {
const int kLoopFlags = NqLoop::EV_READ | NqLoop::EV_WRITE;
}  // namespace

NqNetworkHelper::NqNetworkHelper(
    NqLoop* loop,
    QuicClientBase* client)
    : loop_(loop),
      fd_(-1),
      packets_dropped_(0),
      overflow_supported_(false),
      packet_reader_(new QuicPacketReader()),
      client_(client) {}

NqNetworkHelper::~NqNetworkHelper() {
  if (client_->connected()) {
    client_->session()->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Client being torn down",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  CleanUpAllUDPSockets();
}

bool NqNetworkHelper::CreateUDPSocketAndBind(
    QuicSocketAddress server_address,
    QuicIpAddress bind_to_address,
    int bind_to_port) {
  fd_ = QuicSocketUtils::CreateUDPSocket(server_address, &overflow_supported_);
  if (fd_ < 0) {
    return false;
  }

  if (bind_to_address.IsInitialized()) {
    address_ = QuicSocketAddress(bind_to_address, client_->local_port());
  } else if (server_address.host().address_family() == IpAddressFamily::IP_V4) {
    address_ = QuicSocketAddress(QuicIpAddress::Any4(), bind_to_port);
  } else {
    address_ = QuicSocketAddress(QuicIpAddress::Any6(), bind_to_port);
  }

  sockaddr_storage addr = address_.generic_address();
  socklen_t slen = nq::Syscall::GetSockAddrLen(addr.ss_family);
  if (slen == 0) {
    return false;
  }
  int rc = bind(fd_, reinterpret_cast<sockaddr*>(&addr), slen);
  if (rc < 0) {
    QUIC_LOG(ERROR) << "Bind failed: " << strerror(errno);
    return false;
  }

  if (address_.FromSocket(fd_) != 0) {
    QUIC_LOG(ERROR) << "Unable to get self address.  Error: "
                    << strerror(errno);
  }

  loop_->Add(fd_, this, kLoopFlags);
  return true;
}

void NqNetworkHelper::CleanUpUDPSocket(Fd fd) {
  DCHECK_EQ(fd, fd_);
  CleanUpUDPSocketImpl(fd);
}

void NqNetworkHelper::CleanUpAllUDPSockets() {
  CleanUpUDPSocketImpl(fd_);
}

void NqNetworkHelper::CleanUpUDPSocketImpl(Fd fd) {
  DCHECK_EQ(fd, fd_);
  if (fd > -1) {
    loop_->Del(fd);
    int rc = nq::Syscall::Close(fd);
    DCHECK_EQ(0, rc);
  }
}

void NqNetworkHelper::RunEventLoop() {
  loop_->Poll();
}

void NqNetworkHelper::OnClose(Fd /*fd*/) {}
int NqNetworkHelper::OnOpen(Fd /*fd*/) {  return NQ_OK; }
void NqNetworkHelper::OnEvent(Fd fd, const Event& event) {
  if (NqLoop::Readable(event)) {
    TRACE("readable %d\n", fd);
    bool more_to_read = true;
    while (client_->connected() && more_to_read) {
      more_to_read = packet_reader_->ReadAndDispatchPackets(
          fd, GetLatestClientAddress().port(),
          *client_->helper()->GetClock(), this,
          overflow_supported_ ? &packets_dropped_ : nullptr);
    }
  }
  if (client_->connected() && NqLoop::Writable(event)) {
    TRACE("writable %d\n", fd);
    client_->writer()->SetWritable();
    client_->session()->connection()->OnCanWrite();
  }
  if (NqLoop::Closed(event)) {
    TRACE("closed %d\n", fd);
    QUIC_DLOG(INFO) << "looperr";
  }
}

QuicPacketWriter* NqNetworkHelper::CreateQuicPacketWriter() {
  return new QuicDefaultPacketWriter(fd_);
}

QuicSocketAddress NqNetworkHelper::GetLatestClientAddress() const {
  return address_;
}

void NqNetworkHelper::ProcessPacket(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    const QuicReceivedPacket& packet) {
  client_->session()->ProcessUdpPacket(self_address, peer_address, packet);
}

}  // namespace net