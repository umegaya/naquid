#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include "nq.h"
#include "basis/id_factory.h"

namespace net {
typedef uint32_t NqSessionIndex;
typedef uint16_t NqStreamIndex;
typedef uint32_t NqAlarmIndex;
class NqAlarmSerialCodec {
  constexpr static uint64_t CLIENT_BIT = 0x0000000080000000;
 public:
  static inline uint64_t ServerEncode(NqAlarmIndex alarm_index, int worker_index) {
    return ((uint64_t)alarm_index << 0) | ((uint64_t)worker_index << 32);
  }
  static inline int ServerWorkerIndex(uint64_t serial) {
    return (int)((serial & 0x0000FFFF00000000) >> 32);
  }
  static inline NqAlarmIndex ServerAlarmIndex(uint64_t serial) {
    return (NqAlarmIndex)((serial & 0x00000000FFFFFFFF));
  } 
  static inline uint64_t ClientEncode(NqAlarmIndex alarm_index) {
    return ((uint64_t)alarm_index) | CLIENT_BIT;
  }
  static inline NqAlarmIndex ClientAlarmIndex(uint64_t serial) {
    return (NqAlarmIndex)((serial & 0x00000000FFFFFFFF));
  } 
  static inline bool IsClient(uint64_t serial) {
    return ((serial & CLIENT_BIT) != 0);
  }
};
class NqConnSerialCodec {
  constexpr static uint64_t CLIENT_BIT = 0x0000000080000000;
 public:
  static inline uint64_t ServerEncode(NqSessionIndex session_index, QuicConnectionId cid, int n_worker) {
    ASSERT(session_index <= 0x7FFFFFFF);
    return ((uint64_t)session_index & 0x000000007FFFFFFF) | ((cid % n_worker) << 48);
  }
  static inline NqSessionIndex ServerSessionIndex(uint64_t serial) {
    return (NqSessionIndex)(serial & 0x000000007FFFFFFF);
  }
  static inline int ServerWorkerIndex(uint64_t serial) {
    return (int)((serial & 0xFFFF000000000000) >> 48);
  }

  static inline uint64_t ClientEncode(NqSessionIndex session_index) {
    ASSERT(session_index <= 0x7FFFFFFF);
    return ((uint64_t)session_index & 0x000000007FFFFFFF) | CLIENT_BIT;
  }
  static inline NqSessionIndex ClientSessionIndex(uint64_t serial) {
    return ServerSessionIndex(serial);
  }

  static inline bool IsClient(uint64_t serial) {
    return ((serial & CLIENT_BIT) != 0);
  }
};
class NqStreamSerialCodec {
  constexpr static uint64_t CLIENT_BIT = 0x0000000080000000;
 public:
  static inline uint64_t ServerEncode(NqSessionIndex session_index, NqStreamIndex stream_index, int worker_index) {
    ASSERT(session_index <= 0x7FFFFFFF);
    return (((uint64_t)session_index) & 0x000000007FFFFFFF) | ((uint64_t)stream_index << 32) | ((uint64_t)worker_index << 48);
  }
  static inline NqSessionIndex ServerSessionIndex(uint64_t serial) {
    return (NqSessionIndex)(serial & 0x000000007FFFFFFF);
  }
  static inline NqStreamIndex ServerStreamIndex(uint64_t serial) {
    return (int)((serial & 0x0000FFFF00000000) >> 32);
  }
  static inline int ServerWorkerIndex(uint64_t serial) {
    return (int)((serial & 0xFFFF000000000000) >> 48);
  }

  static inline uint64_t ClientEncode(NqSessionIndex session_index, NqStreamIndex stream_index) {
    return (((uint64_t)session_index) & 0x000000007FFFFFFF) | ((uint64_t)stream_index << 32) | CLIENT_BIT;
  }
  static inline NqSessionIndex ClientSessionIndex(uint64_t serial) {
    return ServerSessionIndex(serial);
  }
  static inline NqStreamIndex ClientStreamIndex(uint64_t serial) {
    return ServerStreamIndex(serial);
  }

  static inline bool IsClient(uint64_t serial) {
    return ((serial & CLIENT_BIT) != 0);
  }
  static inline uint64_t ToConnSerial(uint64_t stream_serial) {
    return (stream_serial & 0xFFFF0000FFFFFFFF);
  }
};
template <class S, typename INDEX>
class NqSessiontMap : protected std::map<INDEX, S*> {
  nq::IdFactory<INDEX> idgen_;
  std::mutex mtx_;
 public:
  typedef std::map<INDEX, S*> container;
  NqSessiontMap() : container(), idgen_(), mtx_() {}
  ~NqSessiontMap() { Clear(); }
  inline INDEX NewId() { 
    return idgen_.New(); 
  }
  inline void Clear() {
    std::unique_lock<std::mutex> lock(mtx_);
    for (auto &kv : *this) {
      delete kv.second;
    }
    container::clear();
  }
  inline INDEX Add(S *s) { 
    auto idx = NewId();
    std::unique_lock<std::mutex> lock(mtx_);
    (*this)[idx] = s; 
    return idx;
  }
  inline void Remove(INDEX idx) {
    std::unique_lock<std::mutex> lock(mtx_);
    auto it = container::find(idx);
    if (it != container::end()) {
      container::erase(it);
    }
  }
  inline S *Find(INDEX idx) {
    std::unique_lock<std::mutex> lock(mtx_);
    auto it = container::find(idx);
    return it != container::end() ? it->second : nullptr;
  }
};
}
