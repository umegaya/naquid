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
//especially, keep following restriction in mind.
//  1. member p can be cast to NqSession::Delegate* or NqStream* (and its derived classes, NqServerStream, NqClientStream, NqClient, NqServerSession)
//     but none of its virtual function can be called. because its vtbl may become invalid already.
//  2. validity should be check serial stored in pointer of casted object and handle (nq_conn_t.s, nq_rpc_t.s, nq_stream_t.s, nq_alarm_t.s),
//     with locking mutex which is returned by StaticMutex(nq_conn_t/nq_rpc_t/nq_stream_t/nq_alarm_t)
//
//caller should be use following functions with following step. for convenience, UNWRAP_CONN and UNWRAP_STREAM is provided.
//  1. lock mutex which is returned by NqUnwrapper::UnwrapMutex. 
//  2. get storead serial
//  3. get NqSession::Delegate* or NqStream* or NqAlarm* by getting corresponding UnwrapXXX method
//  4. if non-null pointer returned, you can call any method as its synchronized. otherwise, handle(nq_conn_t/nq_rpc_t/nq_stream_t/nq_alarm_t) is 
//     already invalid and do nothing 
class NqUnwrapper {
 public:
  //unwrap boxer
  static inline NqBoxer *UnwrapBoxer(bool is_client, NqSession::Delegate *p) { 
    if (is_client) {
      auto cli = static_cast<NqClient *>(p);
      return cli->boxer();
    } else {
      auto sv = static_cast<NqServerSession *>(p);
      return sv->boxer();
    }
  }
  static inline NqBoxer *UnwrapBoxer(bool is_client, NqStream *p) { 
    if (is_client) {
      auto cli = static_cast<NqClientStream *>(p);
      return cli->boxer();
    } else {
      auto sv = static_cast<NqServerStream *>(p);
      return sv->boxer();
    }
  }
  static inline NqBoxer *UnwrapBoxer(nq_conn_t conn) {
    return UnwrapBoxer(NqSerial::IsClient(conn.s), reinterpret_cast<NqSession::Delegate *>(conn.p));   
  }
  static inline NqBoxer *UnwrapBoxer(nq_stream_t s) { 
    return UnwrapBoxer(NqSerial::IsClient(s.s), reinterpret_cast<NqStream *>(s.p)); 
  }
  static inline NqBoxer *UnwrapBoxer(nq_rpc_t rpc) { 
    return UnwrapBoxer(NqSerial::IsClient(rpc.s), reinterpret_cast<NqStream *>(rpc.p)); 
  }
  static inline NqBoxer *UnwrapBoxer(nq_alarm_t a) { 
    return reinterpret_cast<NqAlarm *>(a.p)->boxer();
  }


  //unwrap stored serial
  static inline const nq_serial_t &UnwrapStoredSerial(bool is_client, NqSession::Delegate *delegate_ptr) {
    if (is_client) {
      auto cli = static_cast<NqClient *>(delegate_ptr);
      return cli->session_serial();
    } else {
      auto sv = static_cast<NqServerSession *>(delegate_ptr);
      return sv->session_serial();
    }        
  }
  static inline const nq_serial_t &UnwrapStoredSerialFromStream(bool is_client, NqStream *delegate_ptr) {
    return delegate_ptr->stream_serial();
  }

  
  //unwrap mutex
  static inline std::mutex *UnsafeUnwrapMutex(bool is_client, NqSession::Delegate *delegate_ptr) {
    if (is_client) {
      auto cli = static_cast<NqClient *>(delegate_ptr);
      return &(cli->static_mutex());
    } else {
      auto sv = static_cast<NqServerSession *>(delegate_ptr);
      return &(sv->static_mutex());
    }
  }
  static inline std::mutex *UnwrapMutex(const nq_serial_t &stream_serial, NqStream *delegate_ptr) {
    auto b = UnwrapBoxer(NqSerial::IsClient(stream_serial), delegate_ptr);
    if (b->MainThread()) {
      return nullptr; //avoid deadlock
    }
    if (NqSerial::IsClient(stream_serial)) {
      return &(static_cast<NqClientStream *>(delegate_ptr)->static_mutex());
    } else {
      return &(static_cast<NqServerStream *>(delegate_ptr)->static_mutex());
    }
  }
  static inline std::mutex *UnwrapMutex(nq_conn_t conn) {
    auto b = UnwrapBoxer(conn);
    if (b->MainThread()) {
      return nullptr; //avoid deadlock
    }
    return UnsafeUnwrapMutex(NqSerial::IsClient(conn.s), reinterpret_cast<NqSession::Delegate *>(conn.p));
  }


  //stream => conn converter
  static inline nq_conn_t Stream2Conn(const nq_serial_t &stream_serial, NqStream *stream_ptr) {
    auto d = stream_ptr->nq_session()->delegate();
    nq_conn_t c;
    c.p = d;
    c.s = UnwrapStoredSerial(NqSerial::IsClient(stream_serial), d);
    return c;
  }
};
}

//note that following macro and function executed even if pointer already freed. 
//so cannot call virtual function correctly inside of this func.
#define UNWRAP_CONN(__handle, __d, __code, __purpose) { \
  if (NqSerial::IsEmpty(__handle.s)) { \
    TRACE("UNWRAP_CONN(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
  } else { \
    __d = reinterpret_cast<NqSession::Delegate *>(__handle.p); \
    auto m = NqUnwrapper::UnwrapMutex(__handle); \
    if (m != nullptr) { \
      std::unique_lock<std::mutex> lk(*m); \
      if (__d->SessionSerial() == __handle.s) { \
        __code; \
      } \
    } else if (__d->SessionSerial() == __handle.s) { \
      __code; \
    } \
  } \
}
#define UNWRAP_CONN_OR_ENQUEUE(__handle, __d, __boxer, __code, __rescue, __purpose) { \
  if (NqSerial::IsEmpty(__handle.s)) { \
    TRACE("UNWRAP_STREAM_OR_RESCUE(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
  } else { \
    __boxer = NqUnwrapper::UnwrapBoxer(__handle); \
    if (__boxer->MainThread()) { \
      __d = reinterpret_cast<NqSession::Delegate *>(__handle.p); \
      if (__d->SessionSerial() == __handle.s) { \
        __code; \
      } \
    } else { \
      __rescue; \
    } \
  } \
}
#define UNSAFE_UNWRAP_CONN(__handle, __d, __code, __purpose) { \
  __d = reinterpret_cast<NqSession::Delegate *>(__handle.p); \
  __code; \
}
#define UNWRAP_STREAM(__handle, __s, __code, __purpose) { \
  if (NqSerial::IsEmpty(__handle.s)) { \
    TRACE("UNWRAP_STREAM(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
  } else { \
    __s = reinterpret_cast<NqStream *>(__handle.p); \
    auto m = NqUnwrapper::UnwrapMutex(__handle.s, reinterpret_cast<NqStream *>(__handle.p)); \
    if (m != nullptr) { \
      std::unique_lock<std::mutex> lk(*m); \
      if (__s->stream_serial() == __handle.s) { \
        __code; \
      } \
    } else if (__s->stream_serial() == __handle.s) { \
      __code; \
    } \
  } \
}
#define UNWRAP_STREAM_OR_ENQUEUE(__handle, __s, __boxer, __code, __rescue, __purpose) { \
  if (NqSerial::IsEmpty(__handle.s)) { \
    TRACE("UNWRAP_STREAM_OR_RESCUE(%s): invalid handle: %s", __purpose, INVALID_REASON(__handle)); \
  } else { \
    __boxer = NqUnwrapper::UnwrapBoxer(__handle); \
    __s = reinterpret_cast<NqStream *>(__handle.p); \
    if (__boxer->MainThread()) { \
      if (__s->stream_serial() == __handle.s) { \
        __code; \
      } \
    } else { \
      __rescue; \
    } \
  } \
}
#define UNSAFE_UNWRAP_STREAM(__handle, __s, __code, __purpose) { \
  __s = reinterpret_cast<NqStream *>(__handle.p); \
  __code; \
}
/*#define UNWRAP_ALARM(__handle, __a, __code) { \
  std::unique_lock<std::mutex> lock(NqUnwrapper::UnwrapMutex(__handle)); \
  __a = NqUnwrapper::UnwrapAlarm(__handle); \
  if (__a != nullptr) { \
    __code; \
  } \
}*/


