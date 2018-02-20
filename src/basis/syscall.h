#pragma once

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SSIZE_T ssize_t;
#else
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/time.h>
#endif

#include "net/quic/platform/api/quic_logging.h"

#include "basis/defs.h"

namespace nq {
#if defined(WIN32)
typedef SOCKET Fd;
constexpr Fd INVALID_FD = -1;
#else
typedef int Fd;
constexpr Fd INVALID_FD = -1;
#endif

#if defined(WIN32)
#define ALLOCA(varname, ptr_type, n_elem) ptr_type *varname = reinterpret_cast<ptr_type *>(alloca(sizeof(ptr_type) * n_elem))
#else
#define ALLOCA(varname, ptr_type, n_elem) ptr_type varname[n_elem]
#endif

class Syscall {
public:
#if defined(WIN32)
	static inline int Close(Fd fd) { return ::closesocket(fd); }
#else
	static inline int Close(Fd fd) { return ::close(fd); }
#endif
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
	static inline socklen_t GetSockAddrLen(int address_family) {
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
	static inline socklen_t GetIpAddrLen(int address_family) {
		switch(address_family) {
		case AF_INET:
			return sizeof(struct in_addr);
			break;
		case AF_INET6:
			return sizeof(struct in6_addr);
			break;
		default:
			ASSERT(false);
			QUIC_LOG(ERROR) << "unsupported address family: " << address_family;
			return 0;
		}
	}
	static inline void *Memdup(const void *p, nq_size_t sz) {
		void *r = malloc(sz);
		memcpy(r, p, sz);
		return r;
	}
	static inline void MemFree(void *p) {
		free(p);
	}
	static inline void *MemAlloc(nq_size_t sz) {
		return malloc(sz);
	}
	static inline char *StrDup(const char *src) {
#if defined(WIN32)
		return _strdup(src);
#else
		return strdup(src);
#endif
	}
};
}
