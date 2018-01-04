#pragma once

#include <mutex>

#include "nq.h"
#include "basis/allocator.h"
#include "core/nq_client.h"
#include "core/nq_server_session.h"
#include "core/nq_stream.h"
#include "core/nq_alarm.h"
#include "core/nq_serial_codec.h"

namespace net {
//these code does valid check of nq_conn_t, nq_stream_t, nq_rpc_t, nq_alarm_t even if related pointer (member p)
//already freed, and return casted NqSession::Delegate* or NqStream* if valid. its really wild part of the codes, be careful to make change.
//but as a result, following restriction caused.
//  1. member p can be cast to NqSession::Delegate* or NqStream* (and its derived classes, NqServerStream, NqClientStream, NqClient, NqServerSession)
//     but none of its virtual function can be called. because its vtbl may become invalid already.
//  2. validity should be check serial stored in pointer of casted object and handle (nq_conn_t.s, nq_rpc_t.s, nq_stream_t.s, nq_alarm_t.s),
//     with locking mutex which is returned by StaticMutex(nq_conn_t/nq_rpc_t/nq_stream_t/nq_alarm_t)
//
//caller should be use following functions with following step.
//  1. lock mutex which is returned by NqUnwrapper::UnwrapMutex. 
//  2. get storead serial
//  3. get NqSession::Delegate* or NqStream* or NqAlarm* by getting corresponding UnwrapXXX method
//  4. if non-null pointer returned, you can call any method as its synchronized. otherwise, handle(nq_conn_t/nq_rpc_t/nq_stream_t/nq_alarm_t) is 
//     already invalid and do nothing 
class NqUnwrapper {
 protected:
  static inline std::mutex *UnwrapMutex(uint64_t stream_serial, void *p) {
    auto b = UnwrapBoxer(NqStreamSerialCodec::IsClient(stream_serial), p);
    if (b->MainThread() &&
        b->IsSessionLocked(NqStreamSerialCodec::ClientSessionIndex(stream_serial))) {
      return nullptr; //avoid deadlock
    }
    return UnsafeUnwrapMutex(NqStreamSerialCodec::IsClient(stream_serial), p);
  }
  static inline NqSession::Delegate *UnwrapConn(uint64_t stream_serial, void *p) { 
    if (stream_serial != 0) {
      auto s = UnwrapStoredSerialFromStream(stream_serial, p);
      if (s == NqStreamSerialCodec::ToConnSerial(stream_serial)) {
        return reinterpret_cast<NqSession::Delegate *>(p);
      }
    }
    return nullptr;
  }


 public:
  //unwrap boxer
  static inline NqBoxer *UnwrapBoxer(bool is_client, void *p) { 
    if (is_client) {
      auto cli = static_cast<NqClient *>(reinterpret_cast<NqSession::Delegate *>(p));
      return cli->boxer();
    } else {
      auto sv = static_cast<NqServerSession *>(reinterpret_cast<NqSession::Delegate *>(p));
      return sv->boxer();
    }
  }

  //unwrap stored serial
  static inline uint64_t UnwrapStoredSerialCommon(bool is_client, void *delegate_ptr) {
    if (is_client) {
      auto cli = static_cast<NqClient *>(reinterpret_cast<NqSession::Delegate *>(delegate_ptr));
      return cli->session_serial();
    } else {
      auto sv = static_cast<NqServerSession *>(reinterpret_cast<NqSession::Delegate *>(delegate_ptr));
      return sv->session_serial();
    }    
  }
  static inline uint64_t UnwrapStoredSerial(uint64_t conn_serial, void *p) {
    return UnwrapStoredSerialCommon(NqConnSerialCodec::IsClient(conn_serial), p);
  }
  static inline uint64_t UnwrapStoredSerialFromStream(uint64_t stream_serial, void *p) {
    return UnwrapStoredSerialCommon(NqStreamSerialCodec::IsClient(stream_serial), p);
  }
  
  //unwrap mutex
  static inline std::mutex *UnsafeUnwrapMutex(bool is_client, void *delegate_ptr) {
    if (is_client) {
      auto cli = static_cast<NqClient *>(reinterpret_cast<NqSession::Delegate *>(delegate_ptr));
      return &(cli->static_mutex());
    } else {
      auto sv = static_cast<NqServerSession *>(reinterpret_cast<NqSession::Delegate *>(delegate_ptr));
      return &(sv->static_mutex());
    }
  }
  static inline std::mutex *UnwrapMutex(nq_conn_t conn) {
    auto b = UnwrapBoxer(conn);
    if (b->MainThread() &&  
        b->IsSessionLocked(NqStreamSerialCodec::ClientSessionIndex(conn.s))) {
      return nullptr; //avoid deadlock
    }
    return UnsafeUnwrapMutex(NqConnSerialCodec::IsClient(conn.s), conn.p);
  }
  static inline std::mutex *UnwrapMutex(nq_stream_t s) {
    return UnwrapMutex(s.s, s.p);
  }
  static inline std::mutex *UnwrapMutex(nq_rpc_t rpc) {
    return UnwrapMutex(rpc.s, rpc.p);
  }
  /*static inline std::mutex &UnwrapMutex(nq_alarm_t a) {
    auto b = UnwrapBoxer(a);
    return b->GetAlarmAllocator().BSS(a.p)->mutex();    
  }*/


  //unwrap delegate
  static inline NqSession::Delegate *UnwrapConn(nq_conn_t conn) { 
    if (conn.s != 0) {
      auto s = UnwrapStoredSerial(conn.s, conn.p);
      if (s == conn.s) {
        return reinterpret_cast<NqSession::Delegate *>(conn.p);
      }
    }
    return nullptr;
  }
  static inline NqSession::Delegate *UnwrapConn(nq_stream_t s) { 
    return UnwrapConn(s.s, s.p); 
  }
  static inline NqSession::Delegate *UnwrapConn(nq_rpc_t rpc) { 
    return UnwrapConn(rpc.s, rpc.p); 
  }


  //unwrap boxer
  static inline NqBoxer *UnwrapBoxer(nq_conn_t conn) {
    return UnwrapBoxer(NqConnSerialCodec::IsClient(conn.s), conn.p);   
  }
  static inline NqBoxer *UnwrapBoxer(nq_stream_t s) { 
    return UnwrapBoxer(NqStreamSerialCodec::IsClient(s.s), s.p); 
  }
  static inline NqBoxer *UnwrapBoxer(nq_rpc_t rpc) { 
    return UnwrapBoxer(NqStreamSerialCodec::IsClient(rpc.s), rpc.p); 
  }
  static inline NqBoxer *UnwrapBoxer(nq_alarm_t a) { 
    return reinterpret_cast<NqAlarm *>(a.p)->GetBoxer();
  }


  //unwrap stream
  static inline NqStream *UnwrapStream(nq_stream_t s) { 
    return UnwrapBoxer(NqStreamSerialCodec::IsClient(s.s), s.p)->FindStream(s.s, s.p); 
  }
  static inline NqStream *UnwrapStream(nq_rpc_t rpc) { 
    return UnwrapBoxer(NqStreamSerialCodec::IsClient(rpc.s), rpc.p)->FindStream(rpc.s, rpc.p); 
  }


  //unwrap alarm
  /*static inline NqAlarm *UnwrapAlarm(nq_alarm_t a) {
    if (a.s != 0) {
      auto p = reinterpret_cast<NqAlarm *>(a.p);
      if (p->alarm_index() == a.s) {
        return p;
      }
    }
    return nullptr;
  }*/
};
}

//note that following macro and function executed even if pointer already freed. 
//so cannot call virtual function correctly inside of this func.
#define UNWRAP_CONN(__handle, __d, __code, __purpose) { \
  if (__handle.s == 0) { \
    TRACE("UNWRAP_CONN(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
  } else { \
    auto m = NqUnwrapper::UnwrapMutex(__handle); \
    if (m != nullptr) { \
      std::unique_lock<std::mutex> lock(*m); \
      __d = NqUnwrapper::UnwrapConn(__handle); \
      if (__d != nullptr) { \
        __code; \
      } else { \
        TRACE("UNWRAP_CONN(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
      } \
    } else { \
      __d = NqUnwrapper::UnwrapConn(__handle); \
      if (__d != nullptr) { \
        __code; \
      } else { \
        TRACE("UNWRAP_CONN(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
      } \
    } \
  } \
}
#define UNWRAP_STREAM(__handle, __s, __code, __purpose) { \
  if (__handle.s == 0) { \
    TRACE("UNWRAP_STREAM(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
  } else { \
    auto m = NqUnwrapper::UnwrapMutex(__handle); \
    if (m != nullptr) { \
      /*TRACE("UNWRAP_STREAM(%s): try lock %p", __purpose, m);//*/ \
      std::unique_lock<std::mutex> lock(*m); \
      /*TRACE("UNWRAP_STREAM(%s): try lock success %p", __purpose, m);//*/ \
      __s = NqUnwrapper::UnwrapStream(__handle); \
      if (__s != nullptr) { \
        __code; \
      } else { \
        TRACE("UNWRAP_STREAM(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
      } \
    } else { \
      __s = NqUnwrapper::UnwrapStream(__handle); \
      if (__s != nullptr) { \
        __code; \
      } else { \
        TRACE("UNWRAP_STREAM(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
      } \
    } \
  } \
}
/*#define UNWRAP_ALARM(__handle, __a, __code) { \
  std::unique_lock<std::mutex> lock(NqUnwrapper::UnwrapMutex(__handle)); \
  __a = NqUnwrapper::UnwrapAlarm(__handle); \
  if (__a != nullptr) { \
    __code; \
  } \
}*/


