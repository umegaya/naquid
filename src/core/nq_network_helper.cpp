#include "core/nq_network_helper.h"

#include "net/tools/quic/quic_default_packet_writer.h"

#include "basis/syscall.h"
#include "core/nq_client_loop.h"
#include "core/nq_packet_writer.h"

namespace net {

namespace {
const int kLoopFlags = NqLoop::EV_READ | NqLoop::EV_WRITE;
}  // namespace

NqNetworkHelper::NqNetworkHelper(
    NqLoop* loop,
    NqClient* client)
    : loop_(loop),
      fd_(-1),
      packets_dropped_(0),
      overflow_supported_(false),
      packet_reader_(new NqPacketReader()),
      client_(client) {}

NqNetworkHelper::~NqNetworkHelper() {
  CleanUpAllUDPSockets();
}

bool NqNetworkHelper::CreateUDPSocketAndBind(
    QuicSocketAddress server_address,
    QuicIpAddress bind_to_address,
    int bind_to_port) {
  //close fd on reconnection.
  if (fd_ > -1) {
    CleanUpAllUDPSockets();
    ASSERT(fd_ == -1);
  }
  auto fd = QuicSocketUtils::CreateUDPSocket(server_address, &overflow_supported_);
  if (fd < 0) {
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
    nq::Syscall::Close(fd);
    return false;
  }
  int rc = bind(fd, reinterpret_cast<sockaddr*>(&addr), slen);
  if (rc < 0) {
    QUIC_LOG(ERROR) << "Bind failed: " << strerror(errno);
    nq::Syscall::Close(fd);
    return false;
  }

  if (address_.FromSocket(fd) != 0) {
    QUIC_LOG(ERROR) << "Unable to get self address.  Error: "
                    << strerror(errno);
  }

  fd_ = fd;
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
    TRACE("close fd: %d", fd);
    int rc = nq::Syscall::Close(fd);
    DCHECK_EQ(0, rc);
    fd_ = -1;
  }
}

void NqNetworkHelper::RunEventLoop() {
  loop_->Poll();
}

void NqNetworkHelper::OnClose(Fd /*fd*/) {}
int NqNetworkHelper::OnOpen(Fd /*fd*/) {  return NQ_OK; }
void NqNetworkHelper::OnEvent(Fd fd, const Event& event) {
  if (NqLoop::Readable(event)) {
    bool more_to_read = true;
    while (client_->connected() && more_to_read) {
      more_to_read = packet_reader_->Read(
          fd, GetLatestClientAddress().port(),
          *client_->helper()->GetClock(), this,
          overflow_supported_ ? &packets_dropped_ : nullptr);
    }
  }
  if (client_->connected() && NqLoop::Writable(event)) {
    client_->writer()->SetWritable();
    client_->session()->connection()->OnCanWrite();
  }
  if (NqLoop::Closed(event)) {
    TRACE("closed %d", fd);
    QUIC_DLOG(INFO) << "looperr";
  }
}

QuicPacketWriter* NqNetworkHelper::CreateQuicPacketWriter() {
  auto w = new NqPacketWriter(fd_);
  if (client_->IsReachabilityTracked()) {
    w->SetReachabilityTracked(true);
  }
  return w;
}

QuicSocketAddress NqNetworkHelper::GetLatestClientAddress() const {
  return address_;
}

void NqNetworkHelper::OnRecv(NqPacketReader::Packet *p) {
  //self == server, peer == client
#if !defined(USE_DIRECT_WRITE)
  client_->session()->ProcessUdpPacket(p->server_address(), p->client_address(), *p);
#else
  auto m = &(client_->static_mutex());
  std::unique_lock<std::mutex> session_lock(*m);
  loop_->LockSession(client_->session_index());
  //TRACE("NqNetworkHelper try get static mutex success %p", m);
  client_->session()->ProcessUdpPacket(p->server_address(), p->client_address(), *p);
  loop_->UnlockSession();
#endif
}

}  // namespace net
