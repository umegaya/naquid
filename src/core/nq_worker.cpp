#include "core/nq_worker.h"

#include "core/nq_client_loop.h"
#include "core/nq_dispatcher.h"
#include "core/nq_server_session.h"
#include "core/nq_server.h"

namespace net {
void NqWorker::Process(NqPacket *p) {
  for (size_t i = 0; i < dispatchers_.size(); i++) {
    if (dispatchers_[i].first == p->port()) {
      dispatchers_[i].second->Process(p);
      return;
    }
  }
  ASSERT(false);
}
void NqWorker::Run(PacketQueue &queue) {
  if (!Listen()) {
    return;
  }
  NqPacket *p;
  while (server_.alive()) {
    //consume queue
    while (queue.try_dequeue(p)) {
      //pass packet to corresponding session
      Process(p);
    }
    //wait incoming event
    loop_.Poll();
  }
  //last consume queue
  //TODO(iyatomi): packet from another worker may dropped.
  //somehow checking all the thread breaks main loop, before entering this last loop
  while (queue.try_dequeue(p)) {
    //pass packet to corresponding session
    Process(p);
  }
}
bool NqWorker::Listen() {
  if (loop_.Open(dispatchers_.size()) < 0) {
    ASSERT(false);
    return false;
  }
  for (auto &kv : server_.port_configs()) {
    //TODO(iyatomi): enable multiport server
    QuicSocketAddress address;
    if (!ToSocketAddress(kv.second.address_, address)) {
      ASSERT(false);
      return false;
    }
    auto listen_fd = CreateUDPSocketAndBind(address);
    if (listen_fd < 0) {
      ASSERT(false);
      return false;
    }
    auto d = new NqDispatcher(kv.first, kv.second, *this);
    if (loop_.Add(listen_fd, d, NqLoop::EV_READ | NqLoop::EV_WRITE) != NQ_OK) {
      nq::Syscall::Close(listen_fd);
      delete d;
      ASSERT(false);
      return false;
    }
    dispatchers_.push_back(std::pair<int, NqDispatcher*>(kv.first, d));
  }
  return true;
}
//helper
nq::Fd NqWorker::CreateUDPSocketAndBind(const QuicSocketAddress& address) {
  nq::Fd fd = QuicSocketUtils::CreateUDPSocket(address, &overflow_supported_);
  if (fd < 0) {
    QUIC_LOG(ERROR) << "CreateSocket() failed: " << strerror(errno);
    return -1;
  }

  //set socket resuable
  int flag = 1, rc = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
  if (rc < 0) {
    QUIC_LOG(ERROR) << "setsockopt(SO_REUSEPORT) failed: " << strerror(errno);
    nq::Syscall::Close(fd);
    return -1;    
  }

  sockaddr_storage addr = address.generic_address();
  socklen_t slen;
  switch(addr.ss_family) {
    case AF_INET:
      slen = sizeof(struct sockaddr_in);
      break;
    case AF_INET6:
      slen = sizeof(struct sockaddr_in6);
      break;
    default:
      ASSERT(false);
      QUIC_LOG(ERROR) << "unsupported address family: " << addr.ss_family;
      nq::Syscall::Close(fd);
      return -1;
  }
  rc = bind(fd, reinterpret_cast<sockaddr*>(&addr), slen);
  if (rc < 0) {
    QUIC_LOG(ERROR) << "Bind failed: " << strerror(errno);
    nq::Syscall::Close(fd);
    return -1;
  }
  QUIC_LOG(INFO) << "Listening on " << address.ToString();
  return fd; 
}
/* static */
bool NqWorker::ToSocketAddress(const nq_addr_t &addr, QuicSocketAddress &socket_address) {
  QuicServerId server_id;
  QuicConfig config;
  return NqClientLoop::ParseUrl(addr.host, addr.port, AF_INET, server_id, socket_address, config);
}
}
