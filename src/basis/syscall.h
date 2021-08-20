#pragma once

#include <unistd.h>
#include <errno.h>
#ifdef OS_LINUX
#include <linux/net_tstamp.h>
#endif
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "basis/defs.h"

namespace nq {

#if defined(__ENABLE_EPOLL__) || defined(__ENABLE_KQUEUE__)
typedef int Fd;
constexpr Fd INVALID_FD = -1; 
#elif defined(__ENABLE_IOCP__)
//TODO(iyatomi): windows definition
#else
#endif

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
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
      logger::fatal({
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
      logger::fatal({
        {"msg", "unsupported address family"},
        {"address_family", address_family}
      });
      ASSERT(false);
      return 0;
    }
  }
  static bool SetNonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
      int saved_errno = errno;
      char buf[256];
      logger::fatal({
        {"msg", "fcntl() to get flags fails"},
        {"fd", fd},
        {"errno", saved_errno},
        {"strerror", strerror_r(saved_errno, buf, sizeof(buf))}
      });
      return false;
    }
    if (!(flags & O_NONBLOCK)) {
      int saved_flags = flags;
      flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
      if (flags == -1) {
        int saved_errno = errno;
        char buf[256];
        // bad.
        logger::fatal({
          {"msg", "fcntl() to set flags fails"},
          {"fd", fd},
          {"prev_flags", saved_flags},
          {"errno", saved_errno},
          {"strerror", strerror_r(saved_errno, buf, sizeof(buf))}
        });
        return false;
      }
    }
    return true;
  }
  static const size_t kDefaultSocketReceiveBuffer = 1024 * 1024;
  static int SetGetAddressInfo(int fd, int address_family) {
#if defined(OS_MACOSX)
    if (address_family == AF_INET6) {
      //for osx, IP_PKTINFO for ipv6 will not work. (at least at Sierra)
      return 0;
    }
#endif
    int get_local_ip = 1;
    int rc = setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                        sizeof(get_local_ip));
#if defined(IPV6_RECVPKTINFO)
    if (rc == 0 && address_family == AF_INET6) {
      rc = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip));
    }
#endif
    return rc;
  }

  static int SetGetSoftwareReceiveTimestamp(int fd) {
#if defined(SO_TIMESTAMPING) && defined(SOF_TIMESTAMPING_RX_SOFTWARE)
    int timestamping = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
    return setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &timestamping,
                      sizeof(timestamping));
#else
    return -1;
#endif
  }

  static bool SetSendBufferSize(int fd, size_t size) {
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) != 0) {
      logger::error({
        {"msg", "Failed to set socket send size"},
        {"size", size}
      });
      return false;
    }
    return true;
  }

  static bool SetReceiveBufferSize(int fd, size_t size) {
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
      logger::error({
        {"msg", "Failed to set socket recv size"},
        {"size", size}
      });
      return false;
    }
    return true;
  }
  static int CreateUDPSocket(int address_family, bool* overflow_supported) {
    int fd = socket(address_family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
      logger::error({
        {"msg", "socket() failed"},
        {"errno", Errno()}
      });
      return -1;
    }
    
    if (!SetNonblocking(fd)) {
      return -1;
    }

    int get_overflow = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &get_overflow,
                        sizeof(get_overflow));
    if (rc < 0) {
      logger::warn({
        {"msg", "Socket overflow detection not supported"}
      });
    } else {
      *overflow_supported = true;
    }

    if (!SetReceiveBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      return -1;
    }

    if (!SetSendBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      return -1;
    }

    rc = SetGetAddressInfo(fd, address_family);
    if (rc < 0) {
      logger::error({
        {"msg", "IP detection not supported"},
        {"errno", Errno()}
      });
      return -1;
    }

    rc = SetGetSoftwareReceiveTimestamp(fd);
    if (rc < 0) {
      logger::warn({
        {"msg", "SO_TIMESTAMPING not supported; using fallback"},
        {"errno", Errno()}
      });
    }

    return fd;
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
