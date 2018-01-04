#include "core/nq_worker.h"

#include "basis/syscall.h"
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
void NqWorker::Run(PacketQueue &pq) {
  int n_dispatcher = server_.port_configs().size();
  InvokeQueue *iq[n_dispatcher];
  NqDispatcher *ds[n_dispatcher];
  if (!Listen(iq, ds)) {
    exit(1);
    return;
  }
  NqPacket *p;
  nq_time_t next_try_accept = 0;
  while (server_.alive()) {
    //TODO(iyatomi): better way to handle this (eg. with timer system)
    nq_time_t now = nq_time_now();
    bool try_accept = false;
    if ((next_try_accept + nq_time_msec(10)) < now) {
      try_accept = true;
      next_try_accept = now;
    }
    //consume queue
    while (pq.try_dequeue(p)) {
      //pass packet to corresponding session
      TRACE("process packet at %d", index_);
      Process(p);
    }
    //wait and process incoming event
    for (int i = 0; i < n_dispatcher; i++) {
      iq[i]->Poll(ds[i]);
      if (try_accept) {
        ds[i]->Accept();
      }
    }
    loop_.Poll();
  }
  //last consume queue
  //TODO(iyatomi): packet from another worker may dropped.
  //somehow checking all the thread breaks main loop, before entering this last loop
  while (pq.try_dequeue(p)) {
    //pass packet to corresponding session
    Process(p);
  }
}
bool NqWorker::Listen(InvokeQueue **iq, NqDispatcher **ds) {
  if (loop_.Open(server_.port_configs().size()) < 0) {
    ASSERT(false);
    return false;
  }
  int port_index = 0;
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
    auto cc = kv.second.NewCryptoConfig(&loop_);
    if (cc == nullptr) {
      ASSERT(false);
      return false;      
    }
    nq::logger::info({
      {"msg", "listen"},
      {"thread_index", index_}, 
      {"fd", listen_fd},
    });
    auto d = new NqDispatcher(kv.first, kv.second, std::move(cc), *this);
    if (loop_.Add(listen_fd, d, NqLoop::EV_READ | NqLoop::EV_WRITE) != NQ_OK) {
      nq::Syscall::Close(listen_fd);
      delete d;
      ASSERT(false);
      return false;
    }
    ds[port_index] = d;
    iq[port_index] = d->invoke_queues() + index_;
    port_index++;
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
  socklen_t slen = nq::Syscall::GetSockAddrLen(addr.ss_family);
  if (slen == 0) {
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
  return NqClientLoop::ParseUrl(addr.host, addr.port, 0, server_id, socket_address, config);
}
}
