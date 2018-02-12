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

class ClosureCallerBase {
 public:
  virtual ~ClosureCallerBase() {};
  virtual nq_closure_t closure() = 0;
};

class ReplyClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_rpc_t, nq_error_t, const void *, nq_size_t)> cb_;
 public:
  ReplyClosureCaller() : cb_() {}
  ~ReplyClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_reply, &ReplyClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc, nq_error_t r, const void *p, nq_size_t l) {
    auto pcc = (ReplyClosureCaller *)arg;
    pcc->cb_(rpc, r, p, l);
    delete pcc;
  }
};

class RpcRequestClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_rpc_t, uint16_t, nq_msgid_t, const void *, nq_size_t)> cb_;
 public:
  RpcRequestClosureCaller() : cb_() {}
  ~RpcRequestClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_request, &RpcRequestClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *p, nq_size_t l) {
    auto pcc = (RpcRequestClosureCaller *)arg;
    pcc->cb_(rpc, type, msgid, p, l);
  }
};
class RpcNotifyClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_rpc_t, uint16_t, const void *, nq_size_t)> cb_;
 public:
  RpcNotifyClosureCaller() : cb_() {}
  ~RpcNotifyClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_rpc_notify, &RpcNotifyClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc, uint16_t type, const void *p, nq_size_t l) {
    auto pcc = (RpcNotifyClosureCaller *)arg;
    pcc->cb_(rpc, type, p, l);
  }
};
class StreamRecordClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_stream_t, const void *, nq_size_t)> cb_;
 public:
  StreamRecordClosureCaller() : cb_() {}
  ~StreamRecordClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_stream_record, &StreamRecordClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_stream_t s, const void * p, nq_size_t l) { 
    auto pcc = (StreamRecordClosureCaller *)arg;
    pcc->cb_(s, p, l);
  }  
};
class ConnOpenStreamClosureCaller : public ClosureCallerBase {
 public:
  bool is_stream_;
  std::function<bool (nq_rpc_t, void **)> cb_;
  std::function<bool (nq_stream_t, void **)> stream_cb_;
 public:
  ConnOpenStreamClosureCaller() : is_stream_(false), cb_() {}
  ConnOpenStreamClosureCaller(std::function<bool (nq_rpc_t, void **)> cb) : is_stream_(false), cb_(cb) {}
  ConnOpenStreamClosureCaller(std::function<bool (nq_stream_t, void **)> cb) : is_stream_(true), stream_cb_(cb) {}
  ~ConnOpenStreamClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    if (is_stream_) {
      nq_closure_init(clsr, on_stream_open, &ConnOpenStreamClosureCaller::CallStream, this);
    } else {
      nq_closure_init(clsr, on_rpc_open, &ConnOpenStreamClosureCaller::Call, this);
    }
    return clsr;
  }
  static bool Call(void *arg, nq_rpc_t rpc, void **ppctx) { 
    auto pcc = (ConnOpenStreamClosureCaller *)arg;
    return pcc->cb_(rpc, ppctx);
  }
  static bool CallStream(void *arg, nq_stream_t st, void **ppctx) { 
    auto pcc = (ConnOpenStreamClosureCaller *)arg;
    return pcc->stream_cb_(st, ppctx);
  }
};
class ConnCloseStreamClosureCaller : public ClosureCallerBase {
 public:
  bool is_stream_;
  std::function<void (nq_rpc_t)> cb_;
  std::function<bool (nq_stream_t)> stream_cb_;
 public:
  ConnCloseStreamClosureCaller() : is_stream_(false), cb_() {}
  ConnCloseStreamClosureCaller(std::function<bool (nq_rpc_t)> cb) : is_stream_(false), cb_(cb) {}
  ConnCloseStreamClosureCaller(std::function<bool (nq_stream_t)> cb) : is_stream_(true), stream_cb_(cb) {}
  ~ConnCloseStreamClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    if (is_stream_) {
      nq_closure_init(clsr, on_stream_close, &ConnCloseStreamClosureCaller::CallStream, this);
    } else {
      nq_closure_init(clsr, on_rpc_close, &ConnCloseStreamClosureCaller::Call, this);
    }
    return clsr;
  }
  static void Call(void *arg, nq_rpc_t rpc) { 
    auto pcc = (ConnCloseStreamClosureCaller *)arg;
    pcc->cb_(rpc);
  }
  static void CallStream(void *arg, nq_stream_t st) { 
    auto pcc = (ConnCloseStreamClosureCaller *)arg;
    pcc->stream_cb_(st);
  }
};
class ConnOpenClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_conn_t, void **)> cb_;
 public:
  ConnOpenClosureCaller() : cb_() {}
  ~ConnOpenClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_client_conn_open, &ConnOpenClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_conn_t conn, void **ppctx) { 
    auto pcc = (ConnOpenClosureCaller *)arg;
    return pcc->cb_(conn, ppctx);
  }
};
class ConnCloseClosureCaller : public ClosureCallerBase {
 public:
  std::function<nq_time_t (nq_conn_t, nq_error_t, const nq_error_detail_t*, bool)> cb_;
 public:
  ConnCloseClosureCaller() : cb_() {}
  ~ConnCloseClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_client_conn_close, &ConnCloseClosureCaller::Call, this);
    return clsr;
  }
  static nq_time_t Call(void *arg, nq_conn_t conn, nq_error_t result, const nq_error_detail_t *detail, bool from_remote) { 
    auto pcc = (ConnCloseClosureCaller *)arg;
    return pcc->cb_(conn, result, detail, from_remote);
  }
};
class ConnFinalizeClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_conn_t, void*)> cb_;
 public:
  ConnFinalizeClosureCaller() : cb_() {}
  ~ConnFinalizeClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_client_conn_finalize, &ConnFinalizeClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_conn_t conn, void *ctx) { 
    auto pcc = (ConnFinalizeClosureCaller *)arg;
    return pcc->cb_(conn, ctx);
  }
};
class AlarmClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_time_t*)> cb_;
 public:
  AlarmClosureCaller() : cb_() {}
  ~AlarmClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_alarm, &AlarmClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_time_t *tm) { 
    auto pcc = (AlarmClosureCaller *)arg;
    auto ptm = *tm;
    pcc->cb_(tm);
    if (*tm != 0 && *tm <= ptm) {
      delete pcc;
    }
  }  
};
class StreamTaskClosureCaller : public ClosureCallerBase {
 public:
  bool is_stream_;
  std::function<void (nq_rpc_t rpc)> cb_;
  std::function<void (nq_stream_t s)> stream_cb_;
 public:
  StreamTaskClosureCaller() : cb_() {}
  StreamTaskClosureCaller(std::function<void (nq_rpc_t)> cb) : is_stream_(false), cb_(cb) {}
  StreamTaskClosureCaller(std::function<void (nq_stream_t)> cb) : is_stream_(true), stream_cb_(cb) {}
  ~StreamTaskClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    if (is_stream_) {
      nq_closure_init(clsr, on_stream_task, &StreamTaskClosureCaller::Call, this);
    } else {
      nq_closure_init(clsr, on_rpc_task, &StreamTaskClosureCaller::Call, this);
    }
    return clsr;
  }
  static void Call(void *arg, nq_stream_t s) { 
    auto pcc = (StreamTaskClosureCaller *)arg;
    return pcc->stream_cb_(s);
  }  
  static void Call(void *arg, nq_rpc_t rpc) { 
    auto pcc = (StreamTaskClosureCaller *)arg;
    return pcc->cb_(rpc);
  }  
  inline void InvokeTask(nq_rpc_t rpc) {
    nq_rpc_task(rpc, closure());
  }
  inline void InvokeTask(nq_stream_t s) {
    nq_stream_task(s, closure());
  }  
};
class ModifyHdmapClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (nq_hdmap_t hdmap)> cb_;
 public:
  ModifyHdmapClosureCaller(std::function<void (nq_hdmap_t hdmap)> cb) : cb_(cb) {}
  ~ModifyHdmapClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_conn_modify_hdmap, &ModifyHdmapClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, nq_hdmap_t hm) { 
    auto pcc = (ModifyHdmapClosureCaller *)arg;
    return pcc->cb_(hm);
  }  
};
class StreamAckClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (int, nq_time_t)> cb_;
 public:
  StreamAckClosureCaller(std::function<void (int, nq_time_t)> cb) : cb_(cb) {}
  ~StreamAckClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_stream_ack, &StreamAckClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, int byte, nq_time_t delay_ns) { 
    auto pcc = (StreamAckClosureCaller *)arg;
    pcc->cb_(byte, delay_ns);
    delete pcc;
  }    
};
class StreamRetransmitClosureCaller : public ClosureCallerBase {
 public:
  std::function<void (int)> cb_;
 public:
  StreamRetransmitClosureCaller(std::function<void (int)> cb) : cb_(cb) {}
  ~StreamRetransmitClosureCaller() override {}
  nq_closure_t closure() override {
    nq_closure_t clsr;
    nq_closure_init(clsr, on_stream_retransmit, &StreamRetransmitClosureCaller::Call, this);
    return clsr;
  }
  static void Call(void *arg, int byte) { 
    auto pcc = (StreamRetransmitClosureCaller *)arg;
    pcc->cb_(byte);
  }    
};



class Test {
 public:
  struct Conn;
  struct RunOptions {
    nq_time_t idle_timeout, handshake_timeout, rpc_timeout, execute_duration;
    bool raw_mode;
    RunOptions() {
      idle_timeout = nq_time_sec(60);
      handshake_timeout = nq_time_sec(60);
      rpc_timeout = nq_time_sec(60);
      execute_duration = 0;
      raw_mode = false;
    }
  };
  typedef std::function<void (bool)> Latch;
  typedef std::function<void (Conn &conn)> TestProc;
  typedef std::function<void (Conn &conn, const RunOptions &options)> TestInitProc;
  enum CallbackType {
    RpcNotify,
    RpcRequest,
    StreamRecord,
    ConnOpenStream,
    ConnCloseStream,
    ConnOpen,
    ConnClose,
    ConnFinalize,
    CallbackType_Max,
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
   public:
    static nq_stream_t invalid_stream;
    static nq_rpc_t invalid_rpc;
    struct Stream {
      std::map<CallbackType, std::unique_ptr<ClosureCallerBase>> notifiers;
      union {
        nq_rpc_t rpc;
        nq_stream_t st;
      };
      Stream(nq_stream_t s) : notifiers() {
        st = s;
      }
      Stream(nq_rpc_t r) : notifiers() {
        rpc = r;
      }
    };
    typedef uint64_t SessionSerialType;
   public:
    int index, disconnect;
    bool opened;
    Test *t;

    nq_conn_t c;

    std::map<SessionSerialType, Stream> streams;
    std::map<CallbackType, std::unique_ptr<ClosureCallerBase>> conn_notifiers;

    char *send_buf;
    nq_size_t send_buf_len;

    std::vector<std::string> records;
    std::vector<RequestData> requests;
    std::vector<NotifyData> notifies;
    std::condition_variable cond;

   public:
    ~Conn() {
      if (send_buf != nullptr) {
        free(send_buf);
      }
    }
    void Signal() {
      cond.notify_one();
    }
    void Wait(std::mutex &mtx) {
      std::unique_lock<std::mutex> lk(mtx);
      cond.wait(lk);
    }
    void OpenStream(const std::string &name, std::function<bool (nq_stream_t, void **)> cb) {
      auto cc = new ConnOpenStreamClosureCaller(cb);
      nq_conn_stream(c, name.c_str(), cc);
    }
    void OpenRpc(const std::string &name, std::function<bool (nq_rpc_t, void **)> cb) {
      auto cc = new ConnOpenStreamClosureCaller(cb);
      nq_conn_rpc(c, name.c_str(), cc);
    }
    static inline const SessionSerialType &SessionSerial(nq_stream_t &s) { return s.s.data[0]; }
    static inline const SessionSerialType &SessionSerial(nq_rpc_t &rpc) { return rpc.s.data[0]; }
    void AddStream(nq_stream_t s) {
      auto sid = SessionSerial(s);
      if (streams.find(sid) == streams.end()) {
        streams.emplace(sid, Stream(s));
      }
    } 
    void AddStream(nq_rpc_t rpc) {
      auto sid = SessionSerial(rpc);
      if (streams.find(sid) == streams.end()) {
        TRACE("AddStream add rpc %llx", sid);
        streams.emplace(sid, Stream(rpc));
      }
    }
    bool RemoveStream(nq_stream_t s) {
      auto it = streams.find(SessionSerial(s));
      if (it != streams.end()) {
        streams.erase(it);
        return true;
      }
      return false;
    }
    bool RemoveStream(nq_rpc_t rpc) {
      auto it = streams.find(SessionSerial(rpc));
      if (it != streams.end()) {
        TRACE("AddStream remove rpc %llx", SessionSerial(rpc));
        streams.erase(it);
        return true;
      }
      return false;
    }
    bool FindClosure(CallbackType type, nq_closure_t &clsr) {
      auto it = conn_notifiers.find(type);
      if (it != conn_notifiers.end()) {
        clsr = it->second->closure();
        return true;
      }
      return false;
    }
    void SetClosure(CallbackType type, ClosureCallerBase *clsr) {
      conn_notifiers[type] = std::unique_ptr<ClosureCallerBase>(clsr);
    }
    bool FindClosure(CallbackType type, nq_stream_t s, nq_closure_t &clsr) {
      auto it = streams.find(SessionSerial(s));
      if (it != streams.end()) {
        auto it2 = it->second.notifiers.find(type);
        if (it2 != it->second.notifiers.end()) {
          clsr = it2->second->closure();
          return true;
        }
      }
      return false;
    }
    void SetClosure(CallbackType type, nq_stream_t s, ClosureCallerBase *clsr) {
      TRACE("SetClosure: sid = %llx", SessionSerial(s));
      auto it = streams.find(SessionSerial(s));
      if (it != streams.end()) {
        it->second.notifiers[type] = std::unique_ptr<ClosureCallerBase>(clsr);
      } else {
        ASSERT(false);
      }
    }
    bool FindClosure(CallbackType type, nq_rpc_t rpc, nq_closure_t &clsr) {
      auto it = streams.find(SessionSerial(rpc));
      if (it != streams.end()) {
        auto it2 = it->second.notifiers.find(type);
        if (it2 != it->second.notifiers.end()) {
          clsr = it2->second->closure();
          return true;
        }
      }
      return false;
    }
    void SetClosure(CallbackType type, nq_rpc_t rpc, ClosureCallerBase *clsr) {
      TRACE("SetClosure: sid = %llx", SessionSerial(rpc));
      auto it = streams.find(SessionSerial(rpc));
      if (it != streams.end()) {
        it->second.notifiers[type] = std::unique_ptr<ClosureCallerBase>(clsr);
      } else {
        ASSERT(false);
      }
    }
    Latch NewLatch() {
      return t->NewLatch();
    }
    void Init(Test *test, int idx) {
      send_buf = (char *)malloc(256);
      send_buf_len = 256;
      index = idx;
      disconnect = 0;
      opened = false;
      t = test;
    }
    void SetConn(nq_conn_t conn) {
      c = conn;
      (t->init_ != nullptr ? t->init_ : Test::RegisterCallback)(*this, t->current_options_);
    }
  };
 protected:
  nq::atomic<int> running_;
  nq::atomic<int> result_;
  nq::atomic<int> test_start_;
  nq::atomic<int> thread_start_;
  nq::atomic<int> closed_conn_;
  TestProc testproc_;
  TestInitProc init_;
  nq_addr_t addr_;
  int concurrency_;
  RunOptions current_options_;
 public:
  Test(const nq_addr_t &addr, TestProc tf, TestInitProc init = nullptr, int cc = 1) : 
    running_(0), result_(0), test_start_(0), thread_start_(0), closed_conn_(0), 
    testproc_(tf), init_(init), addr_(addr), concurrency_(cc) {}  
  Latch NewLatch() {
    Start();
    return std::bind(&Test::End, this, std::placeholders::_1);
  }
  bool Run(const RunOptions *opt = nullptr);

 protected:
  void StartThread() { thread_start_++; }
  void Start() { test_start_++; running_++; TRACE("Start: running = %u\n", running_.load()); }
  void End(bool ok) { 
    ASSERT(ok);
    running_--; 
    TRACE("End: running = %u\n", running_.load());
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

  static void RegisterCallback(Conn &tc, const RunOptions &options);

  static void OnConnOpen(void *arg, nq_conn_t c, void **ppctx);
  static nq_time_t OnConnClose(void *arg, nq_conn_t c, nq_error_t r, const nq_error_detail_t *reason, bool closed_from_remote);
  static void OnConnFinalize(void *arg, nq_conn_t c, void *ctx);

  static bool OnStreamOpen(void *arg, nq_stream_t s, void **pctx);
  static void OnStreamClose(void *arg, nq_stream_t s);
  static void OnStreamRecord(void *arg, nq_stream_t s, const void *data, nq_size_t len);
  static void OnStreamRecordSimple(void *arg, nq_stream_t s, const void *data, nq_size_t len);
  static nq_size_t StreamWriter(void *arg, nq_stream_t s, const void *data, nq_size_t len, void **ppbuf);
  static void *StreamReader(void *arg, nq_stream_t s, const char *data, nq_size_t dlen, int *p_reclen);

  static bool OnRpcOpen(void *arg, nq_rpc_t s, void **pctx);
  static void OnRpcClose(void *arg, nq_rpc_t s);
  static void OnRpcRequest(void *arg, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t dlen);
  static void OnRpcNotify(void *arg, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t dlen);
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
#define ALARM(a, first, callback) {\
  auto *pcc = new nqtest::AlarmClosureCaller(); \
  pcc->cb_ = callback; \
  nq_alarm_set(a, nq_time_now() + first, pcc->closure()); \
}
#define RPCEX(stream, type, buff, blen, cb, to) { \
  auto *pcc = new nqtest::ReplyClosureCaller(); \
  pcc->cb_ = cb; \
  nq_rpc_opt_t opt; \
  opt.callback = pcc->closure(); \
  opt.timeout = to; \
  nq_rpc_call_ex(stream, type, buff, blen, &opt); \
}
#define TASK(stream, callback) { \
  auto *pcc = new nqtest::StreamTaskClosureCaller(callback); \
  pcc->InvokeTask(stream); \
}
#define WATCH_CONN(conn, type, callback) { \
  auto *pcc = new nqtest::type##ClosureCaller(); \
  pcc->cb_ = callback; \
  conn.SetClosure(nqtest::Test::CallbackType::type, pcc); \
}
#define WATCH_STREAM(conn, stream, type, callback) { \
  TRACE("WATCH_STREAM: at %p %u %s(%u)", &conn, nqtest::Test::CallbackType::type, __FILE__, __LINE__); \
  auto *pcc = new nqtest::type##ClosureCaller(); \
  pcc->cb_ = callback; \
  conn.SetClosure(nqtest::Test::CallbackType::type, stream, pcc); \
}
#define MODIFY_HDMAP(conn, callback) { \
  auto *pcc = new nqtest::ModifyHdmapClosureCaller(callback); \
  nq_conn_modify_hdmap(conn, pcc->closure()); \
  delete pcc; \
}


#define ALERT_AND_EXIT(msg) { \
  TRACE("test failure: %s", msg); \
  exit(1); \
}

#define CONDWAIT(conn, lock, on_awake) { \
  conn.cond.wait(lock); \
  on_awake; \
}
