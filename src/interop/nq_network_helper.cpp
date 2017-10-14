#include "interop/naquid_network_helper.h"

#include "net/tools/quic/quic_default_packet_writer.h"

namespace net {

namespace {
const int kLoopFlags = NaquidLoop::EV_READ | NaquidLoop::EV_WRITE;
}  // namespace

NaquidNetworkHelper::NaquidNetworkHelper(
    NaquidLoop* loop,
    QuicClientBase* client)
    : loop_(loop),
      packets_dropped_(0),
      overflow_supported_(false),
      packet_reader_(new QuicPacketReader()),
      client_(client) {}

NaquidNetworkHelper::~NaquidNetworkHelper() {
  if (client_->connected()) {
    client_->session()->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Client being torn down",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  CleanUpAllUDPSockets();
}

bool NaquidNetworkHelper::CreateUDPSocketAndBind(
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
  int rc = bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
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

void NaquidNetworkHelper::CleanUpUDPSocket(Fd fd) {
  DCHECK_EQ(fd, fd_);
  CleanUpUDPSocketImpl(fd);
}

void NaquidNetworkHelper::CleanUpAllUDPSockets() {
  CleanUpUDPSocketImpl(fd_);
}

void NaquidNetworkHelper::CleanUpUDPSocketImpl(Fd fd) {
  DCHECK_EQ(fd, fd_);
  if (fd > -1) {
    loop_->Del(fd);
    int rc = nq::Syscall::Close(fd);
    DCHECK_EQ(0, rc);
  }
}

void NaquidNetworkHelper::RunEventLoop() {
  loop_->Poll();
}

void NaquidNetworkHelper::OnClose(Fd /*fd*/) {}
int NaquidNetworkHelper::OnOpen(Fd /*fd*/) {  return NQ_OK; }
void NaquidNetworkHelper::OnEvent(Fd fd, const Event& event) {
  if (NaquidLoop::Readable(event)) {
    bool more_to_read = true;
    while (client_->connected() && more_to_read) {
      more_to_read = packet_reader_->ReadAndDispatchPackets(
          fd, GetLatestClientAddress().port(),
          *client_->helper()->GetClock(), this,
          overflow_supported_ ? &packets_dropped_ : nullptr);
    }
  }
  if (client_->connected() && NaquidLoop::Writable(event)) {
    client_->writer()->SetWritable();
    client_->session()->connection()->OnCanWrite();
  }
  if (NaquidLoop::Closed(event)) {
    QUIC_DLOG(INFO) << "looperr";
  }
}

QuicPacketWriter* NaquidNetworkHelper::CreateQuicPacketWriter() {
  return new QuicDefaultPacketWriter(fd_);
}

QuicSocketAddress NaquidNetworkHelper::GetLatestClientAddress() const {
  return address_;
}

void NaquidNetworkHelper::ProcessPacket(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    const QuicReceivedPacket& packet) {
  client_->session()->ProcessUdpPacket(self_address, peer_address, packet);
}

}  // namespace net