#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include "nq.h"

namespace net {
typedef uint16_t NqSessionIndex;
typedef uint16_t NqStreamNameId;
typedef uint16_t NqStreamIndexPerNameId;
static const NqStreamNameId CLIENT_INCOMING_STREAM_NAME_ID = static_cast<NqStreamNameId>(0);
class NqSessionIndexFactory {
 public:
  static constexpr NqSessionIndex kLimit = 65535;
  static inline NqSessionIndex Create(NqSessionIndex &seed) {
    auto id = ++seed;
    if (seed >= kLimit) {
      seed = 1;
    }
    return id;
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
template <class S>
class NqSessionContainer : public std::map<NqSessionIndex, S*> {
  typedef std::map<NqSessionIndex, S*>  super;
  super read_map_;
  std::mutex read_map_mutex_;
  NqSessionIndex session_index_seed_;
 public:
  NqSessionContainer() : super(), read_map_(), read_map_mutex_(), session_index_seed_(0) {}
  inline NqSessionIndex NewIndex() { 
    return NqSessionIndexFactory::Create(session_index_seed_); 
  }
  inline void Clear() {
    for (auto &kv : *this) {
      delete kv.second;
    }
    super::clear();    
    {
        std::mutex read_map_mutex_;
        read_map_.clear();
    }
  }
  inline void Add(S *s) { (*this)[s->session_index()] = s; }
  inline void Remove(NqSessionIndex idx) {
    auto it = super::find(idx);
    if (it != super::end()) {
      super::erase(it);
    }
  }
  inline void Activate(S *s) {
    std::unique_lock<std::mutex> lock(read_map_mutex_);
    read_map_[s->session_index()] = s;
  }
  inline void Deactivate(NqSessionIndex idx) {
    std::unique_lock<std::mutex> lock(read_map_mutex_);
    read_map_.erase(idx);
  }
  inline const S *Active(NqSessionIndex idx) const {
      std::unique_lock<std::mutex> lock(const_cast<NqSessionContainer<S>*>(this)->read_map_mutex_);
      auto it = read_map_.find(idx);
      return it != read_map_.end() ? it->second : nullptr;
  }
};
}
