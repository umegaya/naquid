#pragma once

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "basis/defs.h"

namespace nq {

#if defined(__ENABLE_EPOLL__) || defined(__ENABLE_KQUEUE__)
typedef int Fd;
constexpr Fd INVALID_FD = -1; 
#elif defined(__ENABLE_IOCP__)
//TODO(iyatomi): windows definition
#else
#endif

class Syscall {
public:
  static inline int Close(Fd fd) { return ::close(fd); }
  static inline int Errno() { return errno; }
  static inline bool EAgain() {
    int eno = Errno();
    return (EINTR == eno || EAGAIN == eno || EWOULDBLOCK == eno);
  }
  /* note that we treat EADDRNOTAVAIL and ENETUNREACH as blocked error, when reachability_tracked. 
  because it typically happens during network link change (eg. wifi-cellular handover)
  and make QUIC connections to close. but soon link change finished and 
  QUIC actually can continue connection after that. add above errnos will increase
  possibility to migrate QUIC connection on another socket.
  
  TODO(iyatomi): more errno, say, ENETDOWN should be treated as blocked error? 
  basically we add errno to WriteBlocked list with evidence based policy. 
  that is, you actually need to see the new errno on write error caused by link change, 
  to add it to this list.
  */
  static inline bool WriteMayBlocked(int eno, bool reachability_tracked) {
    if (eno == EAGAIN || eno == EWOULDBLOCK) {
      return true;
    } else if (reachability_tracked && (eno == EADDRNOTAVAIL || eno == ENETUNREACH)) {
      return true;
    } else {
      return false;
    }
  }
  static socklen_t GetSockAddrLen(int address_family) {
    switch(address_family) {
    case AF_INET:
      return sizeof(struct sockaddr_in);
      break;
    case AF_INET6:
      return sizeof(struct sockaddr_in6);
      break;
    default:
      nq::logger::fatal({
        {"msg", "unsupported address family"},
        {"address_family", address_family}
      });
      ASSERT(false);
      return 0;
    }
  }
  static socklen_t GetIpAddrLen(int address_family) {
    switch(address_family) {
    case AF_INET:
      return sizeof(struct in_addr);
      break;
    case AF_INET6:
      return sizeof(struct in6_addr);
      break;
    default:
      nq::logger::fatal({
        {"msg", "unsupported address family"},
        {"address_family", address_family}
      });
      ASSERT(false);
      return 0;
    }
  }
  static void *Memdup(const void *p, nq_size_t sz) {
    void *r = malloc(sz);
    memcpy(r, p, sz);
    return r;
  }
  static void MemFree(void *p) {
    free(p);
  }
  static void *MemAlloc(nq_size_t sz) {
    return malloc(sz);
  }
};
}
