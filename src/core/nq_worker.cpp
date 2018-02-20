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
  ALLOCA(iq, InvokeQueue *, n_dispatcher);
  ALLOCA(ds, NqDispatcher *, n_dispatcher);
  if (!Listen(iq, ds)) {
    nq::logger::fatal("fail to listen");
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
  //shutdown proc
  ALLOCA(per_worker_shutdown_state, bool, n_dispatcher);
  for (int i = 0; i < n_dispatcher; i++) {
    per_worker_shutdown_state[i] = false;
    ds[i]->Shutdown(); //send connection close for all sessions handled by this worker
  }
  //last consume queue with checking all sessions are gone
  bool need_wait_shutdown = true;
  auto shutdown_start = nq_time_now();
  while (need_wait_shutdown) {
    while (pq.try_dequeue(p)) {
      //pass packet to corresponding session
      Process(p);
    }
    //wait and process incoming event
    need_wait_shutdown = false;
    for (int i = 0; i < n_dispatcher; i++) {
      iq[i]->Poll(ds[i]);
      if (per_worker_shutdown_state[i]) {
      } else if (!ds[i]->ShutdownFinished(shutdown_start)) {
        need_wait_shutdown = true;
      } else {
        per_worker_shutdown_state[i] = true;
      }
    }
    loop_.Poll();
  }
}
bool NqWorker::Listen(InvokeQueue **iq, NqDispatcher **ds) {
  if (loop_.Open(server_.port_configs().size()) < 0) {
    ASSERT(false);
    return false;
  }
  int port_index = 0;
  for (auto &kv : server_.port_configs()) {
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
#if defined(WIN32)
  constexpr int opt_reuseport = SO_REUSEADDR;
#else
  constexpr int opt_reuseport = SO_REUSEPORT;
#endif
  int flag = 1, rc = setsockopt(fd, SOL_SOCKET, opt_reuseport, reinterpret_cast<const char *>(&flag), sizeof(flag));
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
  char buffer[sizeof(struct sockaddr_storage)];
  int len, af;
  if ((len = NqAsyncResolver::PtoN(addr.host, &af, &buffer)) < 0) {
    return false;
  }
  QuicIpAddress ip;
  if (!ip.FromPackedString(buffer, len)) {
    return false;
  }
  socket_address = QuicSocketAddress(ip, addr.port);
  return true;
}
}
