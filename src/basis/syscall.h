#pragma once

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <algorithm>

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
};
}
