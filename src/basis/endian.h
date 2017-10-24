#pragma once

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <algorithm>

#include "basis/defs.h"

namespace nq {
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
  static inline int16_t NetbytesToHostS16(const char *b) {
    return (int16_t)NetbytesToHost16(b);
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
  static inline int32_t NetbytesToHostS32(const char *b) {
    return (int32_t)NetbytesToHost32(b);
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
  static inline int64_t NetbytesToHostS64(const char *b) {
    return (int64_t)NetbytesToHost64(b);
  }
  template <typename T>
  static inline T HostToNet(T value) noexcept {
    if (Endian::isLittleEndian()) {
  		char* ptr = reinterpret_cast<char*>(&value);
  	 	  std::reverse (ptr, ptr + sizeof(T));
      return value;
    } else if (Endian::isBigEndian()) {
  		return value;
    } else {
      //TODO(iyatomi): PDB?
      ASSERT(false);
      return value;
    }
  }
  template <typename T>
  static inline T NetToHost(T value) noexcept {
    return HostToNet(value);
  }
};
}
