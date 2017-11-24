#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include "nq.h"

namespace net {
typedef uint16_t NqSessionIndex;
typedef uint16_t NqStreamNameId;
typedef uint16_t NqStreamIndexPerNameId;
typedef uint32_t NqAlarmIndex;
static const NqStreamNameId CLIENT_INCOMING_STREAM_NAME_ID = static_cast<NqStreamNameId>(0);
template <class TYPE>
class NqIndexFactory {
 public:
  static constexpr TYPE kLimit = 
    (((TYPE)0x80) << (8 * (sizeof(TYPE) - 1))) + 
    ((((TYPE)0x80) << (8 * (sizeof(TYPE) - 1))) - 100);
  static inline TYPE Create(TYPE &seed) {
    auto id = ++seed;
    if (seed >= kLimit) {
      seed = 1;
    }
    return id;
  }
};
class NqAlarmSerialCodec {
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
    return alarm_index;
  }
  static inline NqAlarmIndex ClientAlarmIndex(uint64_t serial) {
    return (NqAlarmIndex)((serial & 0x00000000FFFFFFFF));
  } 
};
class NqStreamSerialCodec {
 public:
  static inline uint64_t ServerEncode(NqSessionIndex session_index, 
                                      QuicConnectionId conn_id, 
                                      QuicStreamId stream_id, int n_worker) {
    ASSERT(n_worker <= 0x0000FFFF);
    return (((uint64_t)session_index) << 48) | 
            ((conn_id % n_worker) << 32) | (stream_id);
  }
  static inline NqSessionIndex ServerSessionIndex(uint64_t serial) {
    return (NqSessionIndex)((serial & 0xFFFF000000000000) >> 48);
  }
  static inline int ServerWorkerIndex(uint64_t serial) {
    return (int)((serial & 0x0000FFFF00000000) >> 32);
  }
  static inline QuicStreamId ServerStreamId(uint64_t serial) {
    return (QuicStreamId)((serial & 0x00000000FFFFFFFF));
  } 

  static inline uint64_t ClientEncode(NqSessionIndex session_index, 
                                      NqStreamNameId name_id,
                                      NqStreamIndexPerNameId name_index_per_name) {
    return (((uint64_t)session_index) << 32) | (name_id << 16) | (name_index_per_name);
  }
  static inline int ClientSessionIndex(uint64_t serial) {
    return (int)((serial & 0x0000FFFF00000000) >> 32);
  }
  static inline NqStreamNameId ClientStreamNameId(uint64_t serial) {
    return (NqStreamNameId)((serial & 0x00000000FFFF0000) >> 16);
  }
  static inline NqStreamIndexPerNameId ClientStreamIndexPerName(uint64_t serial) {
    return (NqStreamIndexPerNameId)((serial & 0x000000000000FFFF) >> 0);
  } 
};
class NqConnSerialCodec {
 public:
  static inline uint64_t ServerEncode(NqSessionIndex session_index, 
                                      QuicConnectionId conn_id, int n_worker) {
    ASSERT(n_worker <= 0x0000FFFF);
    return (((uint64_t)session_index) << 16) | ((conn_id % n_worker) << 0);
  }
  static inline NqSessionIndex ServerSessionIndex(uint64_t serial) {
    return (NqSessionIndex)((serial & 0x00000000FFFF0000) >> 16);
  }
  static inline int ServerWorkerIndex(uint64_t serial) {
    return (int)(serial & 0x000000000000FFFF);
  }
  static inline uint64_t FromServerStreamSerial(uint64_t serial) {
    auto session_index = NqStreamSerialCodec::ServerSessionIndex(serial);
    auto worker_index = NqStreamSerialCodec::ServerWorkerIndex(serial);
    return (((uint64_t)session_index) << 16) | (worker_index);
  }

  static inline uint64_t ClientEncode(NqSessionIndex session_index) {
    return session_index;
  }
  static inline int ClientSessionIndex(uint64_t serial) {
    return (int)serial;
  }
  static inline uint64_t FromClientStreamSerial(uint64_t serial) {
    auto session_index = NqStreamSerialCodec::ClientSessionIndex(serial);
    return session_index;
  }
};
template <class S, typename INDEX>
class NqObjectExistenceMap : public std::map<INDEX, S*> {
  INDEX index_seed_;
 public:
  typedef std::map<INDEX, S*> container;
  NqObjectExistenceMap() : container(), index_seed_(0) {}
  inline INDEX NewIndex() { 
    return NqIndexFactory<INDEX>::Create(index_seed_); 
  }
  inline void Clear() {
    for (auto &kv : *this) {
      delete kv.second;
    }
    container::clear();    
  }
  inline void Add(INDEX idx, S *s) { (*this)[idx] = s; }
  inline void Remove(INDEX idx) {
    auto it = container::find(idx);
    if (it != container::end()) {
      container::erase(it);
    }
  }
};
template <class S, typename INDEX>
class NqObjectExistenceMapMT : public NqObjectExistenceMap<S, INDEX> {
  typedef NqObjectExistenceMap<S,INDEX> super;
  typedef typename super::container container;
  container read_map_;
  std::mutex read_map_mutex_;
 public:
  NqObjectExistenceMapMT() : read_map_(), read_map_mutex_() {}
  inline void Clear() {
    super::Clear();
    {
        std::unique_lock<std::mutex> lock(read_map_mutex_);
        read_map_.clear();
    }
  }
  inline void Activate(INDEX idx, S *s) {
    std::unique_lock<std::mutex> lock(read_map_mutex_);
    read_map_[idx] = s;
  }
  inline void Deactivate(INDEX idx) {
    std::unique_lock<std::mutex> lock(read_map_mutex_);
    read_map_.erase(idx);
  }
  inline const S *Active(INDEX idx) const {
      std::unique_lock<std::mutex> lock(const_cast<NqObjectExistenceMapMT<S,INDEX>*>(this)->read_map_mutex_);
      auto it = read_map_.find(idx);
      return it != read_map_.end() ? it->second : nullptr;
  }
};
}
