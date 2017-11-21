#pragma once
#include <nq.h>
#include <basis/defs.h>
#include <basis/endian.h>

#include "rpctypes.h"

#include <functional>
#include <vector>
#include <thread>
#include <string>
#include <mutex>
#include <map>
#include <condition_variable>

namespace nqtest {
class Test {
 public:
  struct Conn;
  typedef std::function<void (bool)> Latch;
  typedef std::function<void (Conn &conn)> TestProc;
  enum CallbackType {
    Notify,
    Request,
    OpenStream,
    CloseStream,
  };
  struct RequestData {
    int type;
    nq_msgid_t msgid;
    std::string payload;
  };
  struct NotifyData {
    int type;
    std::string payload;
  };
  struct Conn {
    int index;
    nq_conn_t c;
    Test *t;
    std::thread th;
    std::mutex mtx;
    char *send_buf;
    nq_size_t send_buf_len;
    std::vector<nq_sid_t> streams;
    std::map<CallbackType, nq_closure_t> notifiers;
    std::vector<std::string> records;
    std::vector<RequestData> requests;
    std::vector<NotifyData> notifies;
    std::condition_variable cond;
    bool should_signal;
    ~Conn() {
      if (send_buf != nullptr) {
        free(send_buf);
      }
    }
    void Signal() {
      std::unique_lock<std::mutex> lock(mtx);
      cond.notify_one();
    }
    nq_stream_t NewStream(const std::string &name) {
      return nq_conn_stream(c, name.c_str());
    }
    nq_rpc_t NewRpc(const std::string &name) {
      return nq_conn_rpc(c, name.c_str());
    }
    Latch NewLatch() {
      return t->NewLatch();
    }
    void Init() {
      send_buf = (char *)malloc(256);
      send_buf_len = 256;
    }
  };
 protected:
  nq::atm_int_t running_;
  nq::atm_int_t result_;
  nq::atm_int_t test_start_;
  nq::atm_int_t thread_start_;
  nq::atm_int_t closed_conn_;
  TestProc testproc_;
  nq_addr_t addr_;
  int concurrency_;
 public:
  Test(const nq_addr_t &addr, TestProc tf, int cc = 1) : 
    running_(0), result_(0), test_start_(0), thread_start_(0), closed_conn_(0), testproc_(tf), addr_(addr), concurrency_(cc) {}  
  Latch NewLatch() {
    Start();
    return std::bind(&Test::End, this, std::placeholders::_1);
  }
  bool Run(nq_time_t idle_timeout = 0, nq_time_t timeout = 0);

 protected:
  void StartThread() { thread_start_++; }
  void Start() { test_start_++; running_++; }
  void End(bool ok) { 
    ASSERT(ok);
    running_--; 
    while (true) {
            int32_t expect = result_.load();
            if (expect < 0) {
              break;
            }
            int32_t desired = ok ? 1 : -1;
            if (atomic_compare_exchange_weak(&result_, &expect, desired)) {
                return;
            }
        }
  }
  bool IsSuccess() const { return result_.load() == 1; }
  bool Finished() const { return test_start_.load() > 0 && thread_start_ == concurrency_ && running_.load() == 0; }

  static void RegisterCallback(Conn *tc);

  static bool OnConnOpen(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void *now_always_null);
  static nq_time_t OnConnClose(void *arg, nq_conn_t c, nq_result_t r, const char *reason, bool closed_from_remote);
  static void OnConnFinalize(void *arg, nq_conn_t c);

  static bool OnStreamOpen(void *arg, nq_stream_t s, void **pctx);
  static void OnStreamClose(void *arg, nq_stream_t s);
  static void OnStreamRecord(void *arg, nq_stream_t s, const void *data, nq_size_t len);
  static void OnStreamRecordSimple(void *arg, nq_stream_t s, const void *data, nq_size_t len);
  static nq_size_t StreamWriter(void *arg, nq_stream_t s, const void *data, nq_size_t len, void **ppbuf);
  static void *StreamReader(void *arg, const char *data, nq_size_t dlen, int *p_reclen);

  static bool OnRpcOpen(void *arg, nq_rpc_t s, void **pctx);
  static void OnRpcClose(void *arg, nq_rpc_t s);
  static void OnRpcRequest(void *arg, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t dlen);
  static void OnRpcNotify(void *arg, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t dlen);
};



class ReplyClosureCaller {
 public:
  std::function<void (nq_rpc_t, nq_result_t, const void *, nq_size_t)> cb_;
 public:
  ReplyClosureCaller() : cb_() {}
  nq_closure_t closure() {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_reply, &ReplyClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc, nq_result_t r, const void *p, nq_size_t l) {
    auto pcc = (ReplyClosureCaller *)arg;
    pcc->cb_(rpc, r, p, l);
    delete pcc;
  }
};
class RequestClosureCaller {
 public:
  std::function<void (nq_rpc_t, uint16_t, nq_msgid_t, const void *, nq_size_t)> cb_;
 public:
  RequestClosureCaller() : cb_() {}
  nq_closure_t closure() {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_request, &RequestClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *p, nq_size_t l) {
    auto pcc = (RequestClosureCaller *)arg;
    pcc->cb_(rpc, type, msgid, p, l);
    delete pcc;
  }
};
class NotifyClosureCaller {
 public:
  std::function<void (nq_rpc_t, uint16_t, const void *, nq_size_t)> cb_;
 public:
  NotifyClosureCaller() : cb_() {}
  nq_closure_t closure() {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_notify, &NotifyClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc, uint16_t type, const void *p, nq_size_t l) {
    auto pcc = (NotifyClosureCaller *)arg;
    pcc->cb_(rpc, type, p, l);
    delete pcc;
  }
};
class OpenStreamClosureCaller {
 public:
  std::function<bool (nq_rpc_t, void **)> cb_;
 public:
  OpenStreamClosureCaller() : cb_() {}
  nq_closure_t closure() {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_open, &OpenStreamClosureCaller::Call, this);
    return clsr;
  }
  static bool Call(void *arg, nq_rpc_t rpc, void **ppctx) { 
    auto pcc = (OpenStreamClosureCaller *)arg;
    auto r = pcc->cb_(rpc, ppctx);
    delete pcc;
    return r;
  }
};

} //nqtest

static inline std::string MakeString(const void *pvoid, nq_size_t length) {
  return std::string(static_cast<const char *>(pvoid), length);
}

#define RPC(stream, type, buff, blen, callback) { \
  auto *pcc = new nqtest::ReplyClosureCaller(); \
  pcc->cb_ = callback; \
  nq_rpc_call(stream, type, buff, blen, pcc->closure()); \
}

#define WATCH(conn, type, callback) { \
  auto *pcc = new nqtest::type##ClosureCaller(); \
  pcc->cb_ = callback; \
  conn.notifiers[Test::CallbackType::type] = pcc->closure(); \
}


#define ALERT_AND_EXIT(msg) { \
  TRACE("test failure: %s", msg); \
  exit(1); \
}

#define CONDWAIT(conn, lock, on_awake) { \
  conn.cond.wait(lock); \
  on_awake; \
}
