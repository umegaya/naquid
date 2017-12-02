#include "common.h"

#include <cstdlib>

namespace nqtest {
nq_stream_t Test::Conn::invalid_stream = { const_cast<char *>("invalid"), 0 };
nq_rpc_t Test::Conn::invalid_rpc { const_cast<char *>("invalid"), 0 };


void Test::OnConnOpen(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void **ppctx) {
  if (hsev != NQ_HS_DONE) {
    return;
  }
  auto tc = (Conn *)arg;
  if (tc->th.joinable()) {
    //already start
    tc->should_signal = true;
    nq_closure_t clsr;
    if (tc->FindClosure(CallbackType::ConnOpen, clsr)) {
      nq_closure_call(clsr, on_client_conn_open, c, hsev, ppctx);
    }
    return; 
  }
  auto cid = c.s;
  tc->th = std::thread([tc, cid] {
    auto t = tc->t;
    if (cid != 0) {
      TRACE("launch connection_id=%llx", cid);
      t->testproc_(*tc);
    } else {
      //run as immediate failure
      TRACE("fail to launch connection");
      t->Start();
      t->End(false);
    }
  });
  tc->t->StartThread();
  return;
}
nq_time_t Test::OnConnClose(void *arg, nq_conn_t c, nq_result_t r, const char *reason, bool close_from_remote) {
  auto tc = (Conn *)arg;
  tc->should_signal = true;
  tc->disconnect++;
  nq_closure_t clsr;
  if (tc->FindClosure(CallbackType::ConnClose, clsr)) {
    return nq_closure_call(clsr, on_client_conn_close, c, r, reason, close_from_remote);
  }
  return nq_time_sec(1);
}
void Test::OnConnFinalize(void *arg, nq_conn_t c, void *ctx) {
  auto tc = (Conn *)arg;
  tc->t->closed_conn_++;
  nq_closure_t clsr;
  if (tc->FindClosure(CallbackType::ConnFinalize, clsr)) {
    nq_closure_call(clsr, on_client_conn_finalize, c, ctx);
  }
}



bool Test::OnStreamOpen(void *arg, nq_stream_t s, void **ppctx) {
  auto c = (Conn *)arg;
  c->AddStream(s);
  c->should_signal = true;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnOpenStream, clsr)) {
    nq_closure_call(clsr, on_stream_open, s, ppctx);    
  }
  return true;
}
void Test::OnStreamClose(void *arg, nq_stream_t s) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnCloseStream, clsr)) {
    nq_closure_call(clsr, on_stream_close, s);    
  }
  if (c->RemoveStream(s)) {
    c->should_signal = true;
  }
}
void Test::OnStreamRecord(void *arg, nq_stream_t s, const void *data, nq_size_t len) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::StreamRecord, s, clsr)) {
    nq_closure_call(clsr, on_stream_record, s, data, len);    
  } else {
    c->records.push_back(MakeString(data, len));
  }
}
void Test::OnStreamRecordSimple(void *arg, nq_stream_t s, const void *data, nq_size_t len) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::StreamRecord, s, clsr)) {
    nq_closure_call(clsr, on_stream_record, s, data, len);    
  } else {
   c->records.push_back(MakeString(data, len));
  }
}
nq_size_t Test::StreamWriter(void *arg, nq_stream_t s, const void *data, nq_size_t len, void **pbuf) {
  auto c = (Conn *)arg;
  //append \n as delimiter
  auto blen = c->send_buf_len;
  while (c->send_buf_len < (len + 1)) {
    c->send_buf_len <<= 1;
  }
  if (blen != c->send_buf_len) {
    c->send_buf = reinterpret_cast<char *>(realloc(c->send_buf, c->send_buf_len));
    ASSERT(c->send_buf);
  }
  memcpy(c->send_buf, data, len);
  c->send_buf[len] = '\n';
  *pbuf = c->send_buf;
  return len + 1;
}
void *Test::StreamReader(void *arg, const char *data, nq_size_t dlen, int *p_reclen) {
  //auto c = (Conn *)arg;
  //use text protocol (use \n as delimiter)
  auto idx = MakeString(data, dlen).find('\n');
  if (idx != std::string::npos) {
    *p_reclen = idx;
    return const_cast<void *>(reinterpret_cast<const void *>(data));
  }
  return nullptr;
}



bool Test::OnRpcOpen(void *arg, nq_rpc_t rpc, void **ppctx) {
  auto c = (Conn *)arg;
  c->AddStream(rpc);
  c->should_signal = true;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnOpenStream, clsr)) {
    nq_closure_call(clsr, on_rpc_open, rpc, ppctx);    
  }
  return true;
}
void Test::OnRpcClose(void *arg, nq_rpc_t rpc) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnCloseStream, clsr)) {
    nq_closure_call(clsr, on_rpc_close, rpc);    
  }
  if (c->RemoveStream(rpc)) {
    c->should_signal = true;
  }
}
void Test::OnRpcRequest(void *arg, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t dlen) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::RpcRequest, rpc, clsr)) {
    nq_closure_call(clsr, on_rpc_request, rpc, type, msgid, data, dlen);    
  } else {
    RequestData r = {
      .type = type,
      .msgid = msgid,
      .payload = MakeString(data, dlen),
    };
    c->requests.push_back(r);    
  }
}
void Test::OnRpcNotify(void *arg, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t dlen) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::RpcNotify, rpc, clsr)) {
    nq_closure_call(clsr, on_rpc_notify, rpc, type, data, dlen);    
  } else {
    NotifyData r = {
      .type = type,
      .payload = MakeString(data, dlen),
    };
    c->notifies.push_back(r);
  }
}


void Test::RegisterCallback(Conn &tc, const RunOptions &options) {
  auto hm = nq_conn_hdmap(tc.c);
  auto ptc = &tc;

  nq_rpc_handler_t rh;
  nq_closure_init(rh.on_rpc_open, on_rpc_open, &Test::OnRpcOpen, ptc);
  nq_closure_init(rh.on_rpc_close, on_rpc_close, &Test::OnRpcClose, ptc);
  nq_closure_init(rh.on_rpc_request, on_rpc_request, &Test::OnRpcRequest, ptc);
  nq_closure_init(rh.on_rpc_notify, on_rpc_notify, &Test::OnRpcNotify, ptc);
  rh.use_large_msgid = false;
  rh.timeout = options.rpc_timeout;
  nq_hdmap_rpc_handler(hm, "rpc", rh);
  tc.AddStream(nq_conn_rpc(tc.c, "rpc"));

  nq_stream_handler_t rsh;
  nq_closure_init(rsh.on_stream_open, on_stream_open, &Test::OnStreamOpen, ptc);
  nq_closure_init(rsh.on_stream_close, on_stream_close, &Test::OnStreamClose, ptc);
  nq_closure_init(rsh.on_stream_record, on_stream_record, &Test::OnStreamRecord, ptc);
  nq_closure_init(rsh.stream_reader, stream_reader, &Test::StreamReader, ptc);
  nq_closure_init(rsh.stream_writer, stream_writer, &Test::StreamWriter, ptc);
  nq_hdmap_stream_handler(hm, "rst", rsh);
  tc.AddStream(nq_conn_stream(tc.c, "rst"));

  nq_stream_handler_t ssh;
  nq_closure_init(ssh.on_stream_open, on_stream_open, &Test::OnStreamOpen, ptc);
  nq_closure_init(ssh.on_stream_close, on_stream_close, &Test::OnStreamClose, ptc);
  nq_closure_init(ssh.on_stream_record, on_stream_record, &Test::OnStreamRecordSimple, ptc);
  ssh.stream_reader = nq_closure_empty();
  ssh.stream_writer = nq_closure_empty();
  nq_hdmap_stream_handler(hm, "sst", ssh);
  tc.AddStream(nq_conn_stream(tc.c, "sst"));
}


bool Test::Run(const RunOptions *opt) {
  static RunOptions fallback;
  RunOptions options = (opt != nullptr) ? *opt : fallback;
  nq_client_t cl = nq_client_create(256, 256 * 4);
  nq_clconf_t conf = {
    .insecure = false,
    .handshake_timeout = options.handshake_timeout,
    .idle_timeout = options.idle_timeout,
  };
  Conn *conns = new Conn[concurrency_];
  for (int i = 0; i < concurrency_; i++) {
    nq_closure_init(conf.on_open, on_client_conn_open, &Test::OnConnOpen, conns + i);
    nq_closure_init(conf.on_close, on_client_conn_close, &Test::OnConnClose, conns + i);
    nq_closure_init(conf.on_finalize, on_client_conn_finalize, &Test::OnConnFinalize, conns + i);
    auto c = nq_client_connect(cl, &addr_, &conf);
    conns[i].Init(this, c, i, options);
  }
  auto execute_duration = options.execute_duration;
  nq_time_t end = nq_time_now() + execute_duration;
  while (!Finished() && (execute_duration == 0 || nq_time_now() < end)) {
    nq_client_poll(cl);
    for (int i = 0; i < concurrency_; i++) {
      if (conns[i].should_signal) {
        conns[i].should_signal = false;
        conns[i].Signal();
      }
    }
    nq_time_sleep(nq_time_msec(1));
  }
  for (int i = 0; i < concurrency_; i++) {
    if (conns[i].th.joinable()) {
      conns[i].th.join();
    }
  }
  for (int i = 0; i < concurrency_; i++) {
    nq_conn_close(conns[i].c);
  }
  //wait all actual conn close
  while (concurrency_ > closed_conn_) {
    if (execute_duration != 0 && nq_time_now() >= end) {
      //should be closed within timedout
      delete []conns;
      ASSERT(false);
      return false;
    }
    nq_client_poll(cl);
  }
  delete []conns;
  if (execute_duration > 0) {
    return true;
  }
  return IsSuccess();
}
}
