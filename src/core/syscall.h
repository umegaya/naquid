#pragma once

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <algorithm>

namespace nq {
class Syscall {
public:
	static inline int Close(Fd fd) { return ::close(fd); }
	static inline int Errno() { return errno; }
	static inline bool EAgain() {
		int eno = Errno();
		return (EINTR == eno || EAGAIN == eno || EWOULDBLOCK == eno);
	}

  struct Endian {
    static constexpr uint32_t checker_ = 0x00000001;
    static inline bool isLittleEndian() {
        return ((const char *)checker_)[0] == 1;
    }
    static inline bool isBigEndian() {
        return ((const char *)checker_)[3] == 1;
    }
    static inline bool isPDBEndian() {
        return ((const char *)checker_)[1] == 1;
    }
  };

	static inline uint16_t NetbytesToHost16(const char *b) {
    if (Endian::isLittleEndian()) {
  		return ((uint16_t)b[0] << 8) |
  			     ((uint16_t)b[1]);
    } else if (Endian::isBigEndian()) {
  		return *(uint16_t *)b;
    } else {
      ASSERT(false);
      return *(uint16_t *)b;
    }
	}
	static inline uint32_t NetbytesToHost32(const char *b) {
    if (Endian::isLittleEndian()) {
      return ((uint32_t)b[0] << 24) |
             ((uint32_t)b[1] << 16) |
             ((uint32_t)b[2] << 8)  |
             ((uint32_t)b[3]);
    } else if (Endian::isBigEndian()) {
  		return *(uint32_t *)b;
    } else {
      ASSERT(false);
      return *(uint32_t *)b;
    }
	}
	static inline uint64_t NetbytesToHost64(const char *b) {
    if (Endian::isLittleEndian()) {
      return ((uint64_t)b[0] << 56) |
             ((uint64_t)b[1] << 48) |
             ((uint64_t)b[2] << 40) |
             ((uint64_t)b[3] << 32) |
             ((uint64_t)b[4] << 24) |
             ((uint64_t)b[5] << 16) |
             ((uint64_t)b[6] << 8)  |
             ((uint64_t)b[7]);
    } else if (Endian::isBigEndian()) {
      return *(uint64_t *)b;
    } else {
      ASSERT(false);
      return *(uint64_t *)b;
    }
	}
	template <typename T>
	static T HostToNet(T value) noexcept {
    if (Endian::isLittleEndian()) {
  		char* ptr = reinterpret_cast<char*>(&value);
 	 	  std::reverse (ptr, ptr + sizeof(T));
      return value;
    } else if (Endian::isBigEndian()) {
  		return value;
    } else {
      ASSERT(false);
      return value;
    }
	}
};
}
