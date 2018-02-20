#pragma once

#include <algorithm>

#include "basis/defs.h"
#include "basis/syscall.h"

namespace nq {
struct Endian {
  static uint32_t checker_;
  static inline bool isLittleEndian() {
      return ((const char *)(&checker_))[0] == 1;
  }
  static inline bool isBigEndian() {
      return ((const char *)(&checker_))[3] == 1;
  }
  static inline bool isPDBEndian() {
      return ((const char *)(&checker_))[1] == 1;
  }

  /* geneeric netbytes => host integer */
  template <typename T, typename B>
  struct NetbytesToHostImpl {
    static inline T Exec(const B *p) {
      return *(T *)p;
    }
  };
  template <typename B>
  struct NetbytesToHostImpl<uint16_t, B> {
    static inline uint16_t Exec(const B *p) {
      auto b = reinterpret_cast<const uint8_t *>(p);
      if (Endian::isLittleEndian()) {
    		return ((uint16_t)b[0] << 8) |
    			     ((uint16_t)b[1] << 0);
      } else if (Endian::isBigEndian()) {
    		return *(uint16_t *)b;
      } else {
        ASSERT(false);
        return *(uint16_t *)b;
      }
    }
  };
  template <typename B>
  struct NetbytesToHostImpl<int16_t, B> {
    static inline int16_t Exec(const B *p) {
      return (int16_t)NetbytesToHostImpl<uint16_t, B>::Exec(p);
    }
  };
  template <typename B>
  struct NetbytesToHostImpl<uint32_t, B> {
    static inline uint32_t Exec(const B *p) {
      auto b = reinterpret_cast<const uint8_t *>(p);
      if (Endian::isLittleEndian()) {
        return ((uint32_t)b[0] << 24) |
               ((uint32_t)b[1] << 16) |
               ((uint32_t)b[2] << 8)  |
               ((uint32_t)b[3] << 0);
      } else if (Endian::isBigEndian()) {
    		return *(uint32_t *)b;
      } else {
        ASSERT(false);
        return *(uint32_t *)b;
      }
    }
  };
  template <typename B>
  struct NetbytesToHostImpl<int32_t, B> {
    static inline int32_t Exec(const B *p) {
      return (int32_t)NetbytesToHostImpl<uint32_t, B>::Exec(p);
    }
  };
  template <typename B>
  struct NetbytesToHostImpl<uint64_t, B> {
    static inline uint64_t Exec(const B *p) {
      auto b = reinterpret_cast<const uint8_t *>(p);
      if (Endian::isLittleEndian()) {
        return ((uint64_t)b[0] << 56) |
               ((uint64_t)b[1] << 48) |
               ((uint64_t)b[2] << 40) |
               ((uint64_t)b[3] << 32) |
               ((uint64_t)b[4] << 24) |
               ((uint64_t)b[5] << 16) |
               ((uint64_t)b[6] << 8)  |
               ((uint64_t)b[7] << 0);
      } else if (Endian::isBigEndian()) {
        return *(uint64_t *)b;
      } else {
        ASSERT(false);
        return *(uint64_t *)b;
      }
    }
  };
  template <typename B>
  struct NetbytesToHostImpl<int64_t, B> {
    static inline int64_t Exec(const B *p) {
      return (int64_t)NetbytesToHostImpl<uint32_t, B>::Exec(p);
    }
  };
  template <typename T, typename B>
  static inline T NetbytesToHost(const B *p) {
    return NetbytesToHostImpl<T,B>::Exec(p);
  }




  /* geneeric host integer => net bytes */
  template <typename T, typename B>
  struct HostToNetbytesImpl {
    static inline void Exec(T value, B *p) {
      *((T *)p) = value;
    }
  };
  template <typename B>
  struct HostToNetbytesImpl<uint16_t, B> {
    static inline void Exec(uint16_t value, B *p) {
      if (Endian::isLittleEndian()) {
        auto b = reinterpret_cast<uint8_t *>(p);
        auto v = reinterpret_cast<const uint8_t *>(&value); 
        b[0] = v[1];
        b[1] = v[0];
      } else if (Endian::isBigEndian()) {
        *((uint16_t *)p) = value;
      } else {
        ASSERT(false);
        *((uint16_t *)p) = value;
      }
    }
  };
  template <typename B>
  struct HostToNetbytesImpl<int16_t, B> {
    static inline void Exec(int16_t value, B *p) {
      HostToNetbytesImpl<uint16_t, B>::Exec((uint16_t)value, p);
    }
  };
  template <typename B>
  struct HostToNetbytesImpl<uint32_t, B> {
    static inline void Exec(uint32_t value, B *p) {
      if (Endian::isLittleEndian()) {
        auto b = reinterpret_cast<uint8_t *>(p);
        auto v = reinterpret_cast<const uint8_t *>(&value); 
        b[0] = v[3];
        b[1] = v[2];
        b[2] = v[1];
        b[3] = v[0];
      } else if (Endian::isBigEndian()) {
        *((uint32_t *)p) = value;
      } else {
        ASSERT(false);
        *((uint32_t *)p) = value;
      }
    }
  };
  template <typename B>
  struct HostToNetbytesImpl<int32_t, B> {
    static inline void Exec(int32_t value, B *p) {
      HostToNetbytesImpl<uint32_t, B>::Exec((uint32_t)value, p);
    }
  };
  template <typename B>
  struct HostToNetbytesImpl<uint64_t, B> {
    static inline void Exec(uint64_t value, B *p) {
      if (Endian::isLittleEndian()) {
        auto b = reinterpret_cast<uint8_t *>(p);
        auto v = reinterpret_cast<const uint8_t *>(&value); 
        b[0] = v[7];
        b[1] = v[6];
        b[2] = v[5];
        b[3] = v[4];
        b[4] = v[3];
        b[5] = v[2];
        b[6] = v[1];
        b[7] = v[0];
      } else if (Endian::isBigEndian()) {
        *((uint64_t *)p) = value;
      } else {
        ASSERT(false);
        *((uint64_t *)p) = value;
      }
    }
  };
  template <typename B>
  struct HostToNetbytesImpl<int64_t, B> {
    static inline void Exec(int64_t value, B *p) {
      HostToNetbytesImpl<uint64_t, B>::Exec((uint64_t)value, p);
    }
  };
  template <typename T, typename B>
  static inline void HostToNetbytes(T value, B *p) {
    HostToNetbytesImpl<T,B>::Exec(value, p);
  }




  /* generic host integer => net integer */
  template <typename T>
  static inline T HostToNet(T value) noexcept {
    if (Endian::isLittleEndian()) {
  		uint8_t* ptr = reinterpret_cast<uint8_t*>(&value);
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



  /* generic net integer => host integer */
  template <typename T>
  static inline T NetToHost(T value) noexcept {
    return HostToNet(value);
  }
};
}
