#pragma once

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "basis/defs.h"

namespace nq {
class Syscall {
public:
	static inline int Close(Fd fd) { return ::close(fd); }
	static inline int Errno() { return errno; }
	static inline bool EAgain() {
		int eno = Errno();
		return (EINTR == eno || EAGAIN == eno || EWOULDBLOCK == eno);
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
      ASSERT(false);
      QUIC_LOG(ERROR) << "unsupported address family: " << address_family;
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
};
}
