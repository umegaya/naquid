#include "core/nq_async_resolver.h"

#include "core/nq_loop.h"

namespace net {
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
          if (it->second->current_flags() != flags) {
            if (l->Mod(fd, flags) < 0) {
              ASSERT(false);
              l->Del(fd);
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
          auto req = new IoRequest(channel_);
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
      io_requests_.erase(it);
      delete req;
    }
  }
  //TODO(iyatomi): if bits == 0, pause executing this Polling?
  //then activate again if any Resolve call happens.
}
}