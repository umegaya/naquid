#include "common.h"

#include <cstdlib>
#include <memory.h>
#include <condition_variable>
#include <mutex>

namespace nqtest {
nq_stream_t Test::Conn::invalid_stream = { {{0}}, const_cast<char *>("invalid") };
nq_rpc_t Test::Conn::invalid_rpc { {{0}}, const_cast<char *>("invalid") };


void Test::OnConnOpen(void *arg, nq_conn_t c, void **ppctx) {
  auto tc = (Conn *)arg;
  if (tc->opened) {
    //already start
    nq_closure_t clsr;
    if (tc->FindClosure(CallbackType::ConnOpen, clsr)) {
      TRACE("OnConnOpen: call closure, %d", tc->disconnect);
      nq_closure_call(clsr, on_client_conn_open, c, ppctx);
    }
    TRACE("OnConnOpen: no closure, %d", tc->disconnect);
    return; 
  }
  //ensure thread completely start up before next network event happens
  TRACE("launch connection_id=%llx|%llx", c.s.data[0], c.s.data[1]);
  tc->SetConn(c);
  tc->t->testproc_(*tc);
  tc->opened = true;
  tc->t->StartThread();
  return;
}
nq_time_t Test::OnConnClose(void *arg, nq_conn_t c, nq_error_t r, const nq_error_detail_t *detail, bool close_from_remote) {
  auto tc = (Conn *)arg;
  tc->disconnect++;
  nq_closure_t clsr;
  if (tc->FindClosure(CallbackType::ConnClose, clsr)) {
    TRACE("OnConnClose: call closure, %d %s", tc->disconnect, detail->msg);
    return nq_closure_call(clsr, on_client_conn_close, c, r, detail, close_from_remote);
  }
  TRACE("OnConnClose: no closure set yet, %d %s", tc->disconnect, detail->msg);
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
  if (*ppctx != nullptr) {
    auto cc = (ConnOpenStreamClosureCaller *)(*ppctx);
    *ppctx = nullptr;
    nq_closure_call(cc->closure(), on_stream_open, s, ppctx);   
    delete cc;
  }
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnOpenStream, clsr)) {
    return nq_closure_call(clsr, on_stream_open, s, ppctx);   
  }
  return true;
}
void Test::OnStreamClose(void *arg, nq_stream_t s) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnCloseStream, clsr)) {
    nq_closure_call(clsr, on_stream_close, s);    
  }
  c->RemoveStream(s);
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
void *Test::StreamReader(void *arg, nq_stream_t s, const char *data, nq_size_t dlen, int *p_reclen) {
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
  if (*ppctx != nullptr) {
    auto cc = (ConnOpenStreamClosureCaller *)(*ppctx);
    *ppctx = nullptr;
    nq_closure_call(cc->closure(), on_rpc_open, rpc, ppctx);   
    delete cc;
  }
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnOpenStream, clsr)) {
    return nq_closure_call(clsr, on_rpc_open, rpc, ppctx);    
  }
  return true;
}
void Test::OnRpcClose(void *arg, nq_rpc_t rpc) {
  auto c = (Conn *)arg;
  nq_closure_t clsr;
  if (c->FindClosure(CallbackType::ConnCloseStream, clsr)) {
    nq_closure_call(clsr, on_rpc_close, rpc);    
  }
  c->RemoveStream(rpc);
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
  MODIFY_HDMAP(tc.c, ([&tc, &options](nq_hdmap_t hm) {
    auto ptc = &tc;

    nq_rpc_handler_t rh;
    nq_closure_init(rh.on_rpc_open, on_rpc_open, &Test::OnRpcOpen, ptc);
    nq_closure_init(rh.on_rpc_close, on_rpc_close, &Test::OnRpcClose, ptc);
    nq_closure_init(rh.on_rpc_request, on_rpc_request, &Test::OnRpcRequest, ptc);
    nq_closure_init(rh.on_rpc_notify, on_rpc_notify, &Test::OnRpcNotify, ptc);
    rh.use_large_msgid = false;
    rh.timeout = options.rpc_timeout;
    nq_hdmap_rpc_handler(hm, "rpc", rh);
    //tc.AddStream(nq_conn_rpc(tc.c, "rpc"));

    nq_stream_handler_t rsh;
    nq_closure_init(rsh.on_stream_open, on_stream_open, &Test::OnStreamOpen, ptc);
    nq_closure_init(rsh.on_stream_close, on_stream_close, &Test::OnStreamClose, ptc);
    nq_closure_init(rsh.on_stream_record, on_stream_record, &Test::OnStreamRecord, ptc);
    nq_closure_init(rsh.stream_reader, stream_reader, &Test::StreamReader, ptc);
    nq_closure_init(rsh.stream_writer, stream_writer, &Test::StreamWriter, ptc);
    nq_hdmap_stream_handler(hm, "rst", rsh);
    //tc.AddStream(nq_conn_stream(tc.c, "rst"));

    nq_stream_handler_t ssh;
    nq_closure_init(ssh.on_stream_open, on_stream_open, &Test::OnStreamOpen, ptc);
    nq_closure_init(ssh.on_stream_close, on_stream_close, &Test::OnStreamClose, ptc);
    nq_closure_init(ssh.on_stream_record, on_stream_record, &Test::OnStreamRecordSimple, ptc);
    ssh.stream_reader = nq_closure_empty();
    ssh.stream_writer = nq_closure_empty();
    nq_hdmap_stream_handler(hm, "sst", ssh);
    //tc.AddStream(nq_conn_stream(tc.c, "sst"));

    if (options.raw_mode) {
      nq_stream_handler_t rmh;
      nq_closure_init(rmh.on_stream_open, on_stream_open, &Test::OnStreamOpen, ptc);
      nq_closure_init(rmh.on_stream_close, on_stream_close, &Test::OnStreamClose, ptc);
      nq_closure_init(rmh.on_stream_record, on_stream_record, &Test::OnStreamRecord, ptc);
      nq_closure_init(rmh.stream_reader, stream_reader, &Test::StreamReader, ptc);
      nq_closure_init(rmh.stream_writer, stream_writer, &Test::StreamWriter, ptc);
      nq_hdmap_raw_handler(hm, rmh);
      return;
    }
  }));
}


bool Test::Run(const RunOptions *opt) {
  static RunOptions fallback;
  current_options_ = (opt != nullptr) ? *opt : fallback;
  nq_client_t cl = nq_client_create(256, 256 * 4);

  nq_clconf_t conf;
  conf.insecure = false;
  conf.track_reachability = false;
  conf.handshake_timeout = current_options_.handshake_timeout;
  conf.idle_timeout = current_options_.idle_timeout;

  Conn *conns = new Conn[concurrency_];
  for (int i = 0; i < concurrency_; i++) {
    nq_closure_init(conf.on_open, on_client_conn_open, &Test::OnConnOpen, conns + i);
    nq_closure_init(conf.on_close, on_client_conn_close, &Test::OnConnClose, conns + i);
    nq_closure_init(conf.on_finalize, on_client_conn_finalize, &Test::OnConnFinalize, conns + i);
    if (!nq_client_connect(cl, &addr_, &conf)) {
      ASSERT(false);
      return false;
    }
    conns[i].Init(this, i);
  }
  auto execute_duration = current_options_.execute_duration;
  nq_time_t end = nq_time_now() + execute_duration;
  while (!Finished() && (execute_duration == 0 || nq_time_now() < end)) {
    nq_client_poll(cl);
    nq_time_sleep(nq_time_msec(1));
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
