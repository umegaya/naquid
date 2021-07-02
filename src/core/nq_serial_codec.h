#pragma once

#include <map>
#include <mutex>
#include <algorithm>

#include <inttypes.h>

#include "nq.h"
#include "backends/compats/nq_quic_types.h"
#include "basis/id_factory.h"

namespace net {
typedef uint32_t NqSessionIndex; //logical serial for session
typedef uint32_t NqStreamIndex;  //stream id
typedef uint32_t NqAlarmIndex;   //logical serial for alarm

/*
conn serial: 64 bit
  client: [session index 31bit][client bit 1bit][timestamp 32bit]
  server: [session index 31bit][client bit 1bit][timestamp 32bit]

stream serial: 128 bit
  client: [stream index 31bit][client bit 1bit][timestamp 32bit]
  server: [stream index 31bit][client bit 1bit][timestamp 32bit]

alarm serial: 128 bit
  client: [alarm index 31bit][client bit 1bit][timestamp 32bit]
  server: [alarm index 31bit][client bit 1bit][timestamp 32bit]

equality:
  128 bit serial is same

validity:
  stored serial and provided serial is same
*/

class NqSerial : public nq_serial_t {
 protected:
  constexpr static uint64_t CLIENT_BIT = 0x0000000080000000;

 public:
  static inline bool IsSame(const nq_serial_t &s1, const nq_serial_t &s2) {
    return s1.data[0] == s2.data[0];
  }
  static inline bool IsEmpty(const nq_serial_t &serial) {
    return serial.data[0] == 0;
  }
  static inline void Clear(nq_serial_t &serial) {
    serial.data[0] = 0;
  }
  static inline uint64_t InfoBits(const nq_serial_t &serial) {
    return serial.data[0];
  }
  template <typename INDEX>
  static inline void Encode(nq_serial_t &out_serial, INDEX object_index, bool is_client) {
    STATIC_ASSERT(sizeof(INDEX) <= sizeof(uint32_t), "INDEX template type should be with in word");
    ASSERT(object_index <= 0x7FFFFFFF);
    out_serial.data[0] = (uint64_t)(nq_time_unix() << 32) | ((uint64_t)object_index) | (is_client ? CLIENT_BIT : 0);
  }
  template <typename INDEX>
  static inline INDEX ObjectIndex(const nq_serial_t &serial) {
    STATIC_ASSERT(sizeof(INDEX) <= sizeof(uint32_t), "INDEX template type should be with in word");
    return (INDEX)((NqSerial::InfoBits(serial) & 0x000000007FFFFFFF));
  } 
  static inline uint32_t Timestamp(const nq_serial_t &serial) {
    return (uint32_t)((serial.data[0] & 0xFFFFFFFF00000000) >> 32);
  }
  static inline bool IsClient(const nq_serial_t &serial) {
    return ((NqSerial::InfoBits(serial) & CLIENT_BIT) != 0);
  }
  static inline const std::string Dump(const nq_serial_t &serial) {
    char buff[256];
    auto sz = sprintf(buff, "%" PRIx64, serial.data[0]);
    return std::string(buff, sz);
  }
  static inline bool Compare(const nq_serial_t &s1, const nq_serial_t &s2) {
    return s1.data[0] < s2.data[0];    
  }
  struct Comparer {
    inline bool operator() (const nq_serial_t& lhs, const nq_serial_t& rhs) const {
      return Compare(lhs, rhs);
    }
  };
 public:
  inline NqSerial() { Clear(); }
  inline const NqSerial &operator = (const nq_serial_t &s) {
    data[0] = s.data[0];
    return *this;
  }
  inline bool operator == (const nq_serial_t &s) const {
    return NqSerial::IsSame(*this, s);
  }
  inline bool operator != (const nq_serial_t &s) const {
    return !(operator == (s));
  }
  inline bool operator < (const NqSerial& src) const {
    return NqSerial::Compare(*this, src);
  }
  inline bool IsEmpty() const {
    return NqSerial::IsEmpty(*this);
  }
  inline void Clear() {
    NqSerial::Clear(*this);
  }
  inline const std::string Dump() const {
    return NqSerial::Dump(*this);
  }
};

class NqAlarmSerialCodec {
 public:
  static inline void ServerEncode(nq_serial_t &out_serial, NqAlarmIndex alarm_index) {
    NqSerial::Encode(out_serial, alarm_index, false);
  }

  static inline void ClientEncode(nq_serial_t &out_serial, NqAlarmIndex alarm_index) {
    NqSerial::Encode(out_serial, alarm_index, true);
  }

  static inline NqAlarmIndex ClientAlarmIndex(const nq_serial_t &s) { return NqSerial::ObjectIndex<NqAlarmIndex>(s); } 
  static inline NqAlarmIndex ServerAlarmIndex(const nq_serial_t &s) { return NqSerial::ObjectIndex<NqAlarmIndex>(s); } 
};
class NqConnSerialCodec {
 public:
  static inline void ServerEncode(nq_serial_t &out_serial, NqSessionIndex session_index) {
    NqSerial::Encode(out_serial, session_index, false);
  }

  static inline void ClientEncode(nq_serial_t &out_serial, NqSessionIndex session_index) {
    NqSerial::Encode(out_serial, session_index, true);
  }

  static inline NqSessionIndex ClientSessionIndex(const nq_serial_t &s) { return NqSerial::ObjectIndex<NqSessionIndex>(s); }
  static inline NqSessionIndex ServerSessionIndex(const nq_serial_t &s) { return NqSerial::ObjectIndex<NqSessionIndex>(s); }
};
class NqStreamSerialCodec {
 public:
  static inline void ServerEncode(nq_serial_t &out_serial, NqStreamIndex stream_index) {
    NqSerial::Encode(out_serial, stream_index, false);
  }

  static inline void ClientEncode(nq_serial_t &out_serial, NqStreamIndex stream_index) {
    NqSerial::Encode(out_serial, stream_index, true);
  }

  static inline NqQuicStreamId ClientStreamIndex(const nq_serial_t &s) { return NqSerial::ObjectIndex<NqQuicStreamId>(s); }
  static inline NqQuicStreamId ServerStreamIndex(const nq_serial_t &s) { return NqSerial::ObjectIndex<NqQuicStreamId>(s); }
};
template <class S, typename INDEX>
class NqSessiontMap : protected std::map<INDEX, S*> {
  nq::IdFactory<INDEX> idgen_;
 public:
  typedef std::map<INDEX, S*> container;
  NqSessiontMap() : container(), idgen_() {}
  ~NqSessiontMap() { Clear(); }
  inline INDEX NewId() { 
    return idgen_.New(); 
  }
  inline void Clear() {
    for (auto &kv : *this) {
      delete kv.second;
    }
    container::clear();
  }
  inline void Iter(std::function<void (INDEX, S*)> cb) {
    for (auto it = container::begin(); it != container::end(); ) {
      typename container::iterator kv = it;
      ++it;
      cb(kv->first, kv->second);
    }
  }
  inline INDEX Add(S *s) { 
    auto idx = NewId();
    (*this)[idx] = s; 
    return idx;
  }
  inline void Remove(INDEX idx) {
    auto it = container::find(idx);
    if (it != container::end()) {
      container::erase(it);
    }
  }
  inline S *Find(INDEX idx) {
    auto it = container::find(idx);
    return it != container::end() ? it->second : nullptr;
  }
};

template <class H, class P>
inline H MakeHandle(P *p, const NqSerial &s) {
  H h;
  h.p = p;
  h.s = s;
  return h;
}
}
