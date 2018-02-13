#include "core/nq_async_resolver.h"

#include "core/nq_loop.h"

namespace net {
NqAsyncResolver::Config::Config() : optmask(0), server_list(nullptr) {
  flags = 0;
}
NqAsyncResolver::Config::~Config() {
  while (server_list != nullptr) {
    auto tmp = server_list;
    server_list = server_list->next;
    delete tmp;
  }
  server_list = nullptr;
}
void NqAsyncResolver::Config::SetTimeout(nq_time_t t_o) {
  flags |= ARES_OPT_TIMEOUTMS;
  timeout = (int)nq_time_to_msec(t_o);
}
bool NqAsyncResolver::Config::SetServerHostPort(const std::string &host, int port) {
  auto tmp = new struct ares_addr_port_node;
  int af;
  if (NqAsyncResolver::PtoN(host.c_str(), &af, &(tmp->addr)) > 0) {
    tmp->family = af;
    tmp->udp_port = tmp->tcp_port = port;
  } else {
    nq::Syscall::MemFree(tmp);
    return false;
  }
  tmp->next = server_list;
  server_list = tmp;
  return true;
}
void NqAsyncResolver::Config::SetRotateDns() {
  optmask |= ARES_OPT_ROTATE;
}
void NqAsyncResolver::Config::SetStayOpen() {
  optmask |= ARES_FLAG_STAYOPEN;
}
void NqAsyncResolver::Config::SetLookup(bool use_hosts, bool use_dns) {
  if (use_hosts) {
    if (use_dns) {
      lookups = const_cast<char *>("bf");
    } else {
      lookups = const_cast<char *>("b");
    }
  } else {
    lookups = const_cast<char *>("f");
  }
}

void NqAsyncResolver::IoRequest::OnEvent(Fd fd, const Event &e) {
  if (NqLoop::Readable(e)) {
    if (NqLoop::Writable(e)) {
      ares_process_fd(channel_, fd, fd);
    } else {
      ares_process_fd(channel_, fd, ARES_SOCKET_BAD);
    }
  } else {
    ares_process_fd(channel_, ARES_SOCKET_BAD, fd);
  }
}
bool NqAsyncResolver::Initialize(const Config &config) {
  int status = ares_init_options(&channel_, 
    const_cast<ares_options *>(config.options()), config.optmask);
  if(status != ARES_SUCCESS) {
    nq::logger::error({
      {"msg", "fail ares_init_options"},
      {"error", ares_strerror(status)}
    });
    return false;
  }
  ares_set_servers_ports(channel_, config.server_list);
  return true;
}
void NqAsyncResolver::Resolve(const char *host, int family, ares_host_callback cb, void *arg) {
  ares_gethostbyname(channel_, host, family, cb, arg);
}
void NqAsyncResolver::Poll(NqLoop *l) {
  Fd fds[ARES_GETSOCK_MAXNUM];
  auto bits = ares_getsock(channel_, fds, ARES_GETSOCK_MAXNUM);
  //synchronize socket i/o request with event loop registration state
  for (auto &kv : io_requests_) {
    kv.second->set_alive(false);
  }
  if (bits != 0) {
    for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      uint32_t flags = 0;
      if(ARES_GETSOCK_READABLE(bits, i)) {
        flags |= NqLoop::EV_READ;
      }
      if(ARES_GETSOCK_WRITABLE(bits, i)) {
        flags |= NqLoop::EV_WRITE;
      }
      if (flags != 0) {
        Fd fd = fds[i];
        auto it = io_requests_.find(fd);
        if (it != io_requests_.end()) {
          ASSERT(fd == it->second->fd());
          if (it->second->current_flags() != flags) {
            TRACE("ares: fd mod: %d %x => %x", fd, it->second->current_flags(), flags);
            if (l->Mod(fd, flags) < 0) {
              ASSERT(false);
              //will remove below
            } else {
              it->second->set_current_flags(flags);
              it->second->set_alive(true);
            }
          } else {
            //no need to change
            it->second->set_alive(true);
          }
        } else {
          TRACE("ares: fd add: %d %x", fd, flags);
          auto req = new IoRequest(channel_, fd, flags);
          if (l->Add(fd, req, flags) < 0) {
            ASSERT(false);
            delete req;
          } else {
            io_requests_[fd] = req;
          }
        }
      }
    }
  }
  for (auto it = io_requests_.begin(); it != io_requests_.end(); ) {
    auto it_prev = it;
    it++;
    if (!it_prev->second->alive()) {
      auto req = it_prev->second;
      io_requests_.erase(it_prev);
      TRACE("ares: fd del: %d", req->fd());
      //fd already closed in ares internal and reused by another object (eg. NqClient)
      //also, fd seems closed in ares library when control path comes here.
      l->ForceDelWithCheck(req->fd(), req);
      delete req;
    }
  }
  if (queries_.size() > 0) {
    for (auto q : queries_) {
      switch (q->family_) {
        case AF_INET6:
          //AF_UNSPEC automatically search AF_INET6 => AF_INET addresses
          Resolve(q->host_.c_str(), AF_UNSPEC, Query::OnComplete, q);
          break;
        default:
          //if AF_INET and not found, then Query::OnComplete re-invoke with AF_INET6
          Resolve(q->host_.c_str(), q->family_, Query::OnComplete, q);
          break;
      }
    }
    queries_.clear();
  }
  //TODO(iyatomi): if bits == 0, pause executing this Polling?
  //then activate again if any Resolve call happens.
}
}