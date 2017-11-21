#include "common.h"

#include <cstdlib>

namespace nqtest {
bool Test::OnConnOpen(void *arg, nq_conn_t c, nq_handshake_event_t hsev, void *now_always_null) {
  auto tc = (Conn *)arg;
  if (tc->th.joinable()) {
    tc->should_signal = true;
    return true; //already start
  }
  auto cid = nq_conn_cid(c);
  tc->th = std::thread([tc, cid] {
    auto t = tc->t;
    auto c = tc->c;
    if (cid != 0) {
      TRACE("launch connection_id=%llu", nq_conn_cid(c));
      t->testproc_(*tc);
    } else {
      //run as immediate failure
      TRACE("fail to launch connection");
      t->Start();
      t->End(false);
    }
  });
  tc->t->StartThread();
  return true;
}
nq_time_t Test::OnConnClose(void *arg, nq_conn_t c, nq_result_t r, const char *reason, bool closed_from_remote) {
  auto tc = (Conn *)arg;
  tc->should_signal = true;
  return nq_time_sec(1);
}
void Test::OnConnFinalize(void *arg, nq_conn_t c) {
  auto t = (Test *)arg;
  t->closed_conn_++;
}



bool Test::OnStreamOpen(void *arg, nq_stream_t s, void **pctx) {
  auto c = (Conn *)arg;
  c->streams.push_back(nq_stream_sid(s));
  c->should_signal = true;
  return true;
}
void Test::OnStreamClose(void *arg, nq_stream_t s) {
  auto c = (Conn *)arg;
  auto it = std::find(c->streams.begin(), c->streams.end(), nq_stream_sid(s));
  if (it != c->streams.end()) {
    c->streams.erase(it);
    c->should_signal = true;
  }
}
void Test::OnStreamRecord(void *arg, nq_stream_t s, const void *data, nq_size_t len) {
  auto c = (Conn *)arg;
  c->records.push_back(MakeString(data, len));
}
void Test::OnStreamRecordSimple(void *arg, nq_stream_t s, const void *data, nq_size_t len) {
  auto c = (Conn *)arg;
  c->records.push_back(MakeString(data, len));
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



bool Test::OnRpcOpen(void *arg, nq_rpc_t rpc, void **) {
  auto c = (Conn *)arg;
  c->streams.push_back(nq_rpc_sid(rpc));
  c->should_signal = true;
  return true;
}
void Test::OnRpcClose(void *arg, nq_rpc_t rpc) {
  auto c = (Conn *)arg;
  auto it = std::find(c->streams.begin(), c->streams.end(), nq_rpc_sid(rpc));
  if (it != c->streams.end()) {
    c->streams.erase(it);
    c->should_signal = true;
  }
}
void Test::OnRpcRequest(void *arg, nq_rpc_t rpc, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t dlen) {
  auto c = (Conn *)arg;
  RequestData r = {
    .type = type,
    .msgid = msgid,
    .payload = MakeString(data, dlen),
  };
  c->requests.push_back(r);
  auto it = c->notifiers.find(CallbackType::Request);
  if (it != c->notifiers.end()) {
    nq_closure_call(it->second, on_rpc_request, rpc, type, msgid, data, dlen);
  }
}
void Test::OnRpcNotify(void *arg, nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t dlen) {
  auto c = (Conn *)arg;
  NotifyData r = {
    .type = type,
    .payload = MakeString(data, dlen),
  };
  c->notifies.push_back(r);
  auto it = c->notifiers.find(CallbackType::Notify);
  if (it != c->notifiers.end()) {
    nq_closure_call(it->second, on_rpc_notify, rpc, type, data, dlen);
  }
}


void Test::RegisterCallback(Conn *tc) {
  auto hm = nq_conn_hdmap(tc->c);

  nq_rpc_handler_t rh;
  nq_closure_init(rh.on_rpc_open, on_rpc_open, &Test::OnRpcOpen, tc);
  nq_closure_init(rh.on_rpc_close, on_rpc_close, &Test::OnRpcClose, tc);
  nq_closure_init(rh.on_rpc_request, on_rpc_request, &Test::OnRpcRequest, tc);
  nq_closure_init(rh.on_rpc_notify, on_rpc_notify, &Test::OnRpcNotify, tc);
  rh.use_large_msgid = false;
  nq_hdmap_rpc_handler(hm, "rpc", rh);

  nq_stream_handler_t rsh;
  nq_closure_init(rsh.on_stream_open, on_stream_open, &Test::OnStreamOpen, tc);
  nq_closure_init(rsh.on_stream_close, on_stream_close, &Test::OnStreamClose, tc);
  nq_closure_init(rsh.on_stream_record, on_stream_record, &Test::OnStreamRecord, tc);
  nq_closure_init(rsh.stream_reader, stream_reader, &Test::StreamReader, tc);
  nq_closure_init(rsh.stream_writer, stream_writer, &Test::StreamWriter, tc);
  nq_hdmap_stream_handler(hm, "rst", rsh);

  nq_stream_handler_t ssh;
  nq_closure_init(ssh.on_stream_open, on_stream_open, &Test::OnStreamOpen, tc);
  nq_closure_init(ssh.on_stream_close, on_stream_close, &Test::OnStreamClose, tc);
  nq_closure_init(ssh.on_stream_record, on_stream_record, &Test::OnStreamRecordSimple, tc);
  nq_hdmap_stream_handler(hm, "sst", ssh);
}


bool Test::Run(nq_time_t idle_timeout, nq_time_t timeout) {
  nq_client_t cl = nq_client_create(256);
  nq_clconf_t conf = {
    .insecure = false,
    .handshake_timeout = 0,
    .idle_timeout = idle_timeout,
  };
  Conn *conns = new Conn[concurrency_];
  for (int i = 0; i < concurrency_; i++) {
    nq_closure_init(conf.on_open, on_conn_open, &Test::OnConnOpen, conns + i);
    nq_closure_init(conf.on_close, on_conn_close, &Test::OnConnClose, conns + i);
    nq_closure_init(conf.on_finalize, on_conn_finalize, &Test::OnConnFinalize, this);
    conns[i].Init();
    conns[i].index = i;
    conns[i].t = this;
    conns[i].c = nq_client_connect(cl, &addr_, &conf);
    conns[i].should_signal = false;
    RegisterCallback(conns + i);
  }
  nq_time_t end = nq_time_now() + timeout;
  while (!Finished() && (timeout == 0 || nq_time_now() < end)) {
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
    if (timeout == 0 || nq_time_now() < end) {
      //should be closed within timedout
      delete []conns;
      return false;
    }
    nq_client_poll(cl);
  }
  delete []conns;
  if (timeout > 0) {
    return true;
  }
  return IsSuccess();
}
}
