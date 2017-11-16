#pragma once

#include <string>

#include "net/quic/core/quic_stream.h"
#include "net/quic/core/quic_alarm.h"
#include "net/quic/core/quic_spdy_stream.h"

#include "basis/closure.h"
#include "basis/msgid_factory.h"
#include "core/nq_loop.h"
#include "core/nq_serial_codec.h"

namespace net {

class NqSession;
class NqClientSession;
class NqBoxer;
class NqStreamHandler;

class NqStream : public QuicStream {
  std::unique_ptr<NqStreamHandler> handler_;
  std::string buffer_; //scratchpad for initial handshake (receiver side) or stream protocol name
  SpdyPriority priority_;
  nq_stream_t handle_;
  bool establish_side_;
 public:
  NqStream(QuicStreamId id, 
           NqSession* nq_session, 
           bool establish_side, 
           SpdyPriority priority = kDefaultPriority);
  ~NqStream() override {}

  NqLoop *GetLoop();

  void OnDataAvailable() override;
  void OnClose() override;

  void Disconnect();

  inline const std::string &protocol() const { return buffer_; }
  inline bool establish_side() const { return establish_side_; }
  bool set_protocol(const std::string &name);
  NqSessionIndex session_index() const;
  nq_conn_t conn();

  void InitHandle();
  template <class T> inline T *Handler() const { return static_cast<T *>(handler_.get()); }
  template <class H> inline H ToHandle() { return { .p = handle_.p, .s = handle_.s }; }

 protected:
  friend class NqStreamHandler;
  friend class NqDispatcher;
  NqSession *nq_session();
  const NqSession *nq_session() const;
  NqStreamHandler *CreateStreamHandler(const std::string &name);
};

class NqClientStream : public NqStream {
  NqStreamNameId name_id_;
  NqStreamIndexPerNameId index_per_name_id_;
 public:
  NqClientStream(QuicStreamId id, 
           NqSession* nq_session, 
           bool establish_side, 
           SpdyPriority priority = kDefaultPriority) : 
    NqStream(id, nq_session, establish_side, priority) {}

  void OnClose() override;

  inline NqStreamNameId name_id() const { return name_id_; }
  inline NqStreamIndexPerNameId index_per_name_id() const { return index_per_name_id_; }
  inline void set_name_id(NqStreamNameId id) { name_id_ = id; }
  inline void set_index_per_name_id(NqStreamIndexPerNameId idx) { index_per_name_id_ = idx; }

};
class NqServerStream : public NqStream {
 public:
  NqServerStream(QuicStreamId id, 
           NqSession* nq_session, 
           bool establish_side, 
           SpdyPriority priority = kDefaultPriority) : 
    NqStream(id, nq_session, establish_side, priority) {}
  void OnClose() override;
};



class NqStreamHandler {
protected:
  NqStream *stream_;
  nq_closure_t on_open_, on_close_;
  bool proto_sent_;
public:
  NqStreamHandler(NqStream *stream) : stream_(stream), proto_sent_(false) {}
  virtual ~NqStreamHandler() {}
  
  //interface
  virtual void OnRecv(const void *p, nq_size_t len) = 0;
  virtual void Send(const void *p, nq_size_t len) = 0;  
  virtual void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) = 0;

  //operation
  inline bool OnOpen() { return nq_closure_call(on_open_, on_stream_open, stream_->ToHandle<nq_stream_t>()); }
  inline void OnClose() { nq_closure_call(on_close_, on_stream_close, stream_->ToHandle<nq_stream_t>()); }
  inline void SetLifeCycleCallback(nq_closure_t on_open, nq_closure_t on_close) {
    on_open_ = on_open;
    on_close_ = on_close;
  }
  inline void Disconnect() { stream_->Disconnect(); }
  inline void ProtoSent() { proto_sent_ = true; }
  inline NqStream *stream() { return stream_; }
  void WriteBytes(const char *p, nq_size_t len);
  static const void *ToPV(const char *p) { return static_cast<const void *>(p); }
  static const char *ToCStr(const void *p) { return static_cast<const char *>(p); }

 protected:
  NqSession *nq_session() { return stream_->nq_session(); }
};

// A QUIC stream that separated with encoded length
class NqSimpleStreamHandler : public NqStreamHandler {
  nq_closure_t on_recv_;
  std::string parse_buffer_;
 public:
  NqSimpleStreamHandler(NqStream *stream, nq_closure_t on_recv) : 
    NqStreamHandler(stream), on_recv_(on_recv), parse_buffer_() {};

  //implements NqStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override;
  void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) override { ASSERT(false); }

 private:
  DISALLOW_COPY_AND_ASSIGN(NqSimpleStreamHandler);
};

// A QUIC stream that has customized record reader/writer to define record boundary
class NqRawStreamHandler : public NqStreamHandler {
  nq_closure_t on_recv_, reader_, writer_;
 public:
  NqRawStreamHandler(NqStream *stream, nq_closure_t on_recv, nq_closure_t reader, nq_closure_t writer) : 
    NqStreamHandler(stream), on_recv_(on_recv), reader_(reader), writer_(writer) {}
  //implements NqStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override {
    nq_closure_call(writer_, stream_writer, p, len, stream_->ToHandle<nq_stream_t>());
  }
  void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) override { ASSERT(false); }

 private:
  DISALLOW_COPY_AND_ASSIGN(NqRawStreamHandler);
};

// A QUIC stream handles RPC style communication
class NqSimpleRPCStreamHandler : public NqStreamHandler {
  class Request : public QuicAlarm::Delegate {
   public:
    Request(NqSimpleRPCStreamHandler *stream_handler, 
            nq_msgid_t msgid,
            nq_closure_t on_data) : 
            stream_handler_(stream_handler), alarm_(nullptr), on_data_(on_data), msgid_(msgid) {}
    ~Request() {}
    void OnAlarm() override { 
      auto it = stream_handler_->req_map_.find(msgid_);
      if (it != stream_handler_->req_map_.end()) {
        //TODO(iyatomi): raise timeout error
        nq_closure_call(on_data_, on_rpc_reply, stream_handler_->stream()->ToHandle<nq_rpc_t>(), NQ_ETIMEOUT, "", 0);
        stream_handler_->req_map_.erase(it);
      }
      delete alarm_; //it deletes Requet object itself
    }
    inline void Cancel() { alarm_->Cancel(); }
   private:
    friend class NqSimpleRPCStreamHandler;
    NqSimpleRPCStreamHandler *stream_handler_; 
    QuicAlarm *alarm_;
    nq_closure_t on_data_;
    nq_msgid_t msgid_/*, padd_[3]*/;
  };
  void EntryRequest(nq_msgid_t msgid, nq_closure_t cb, uint64_t timeout_duration_us = 30 * 1000 * 1000);
 private:
  std::string parse_buffer_;
  nq_closure_t on_request_, on_notify_;
  nq::MsgIdFactory<nq_msgid_t> msgid_factory_;
  std::map<uint32_t, Request*> req_map_;
  NqLoop *loop_;
 public:
  NqSimpleRPCStreamHandler(NqStream *stream, nq_closure_t on_request, nq_closure_t on_notify, bool use_large_msgid) : 
    NqStreamHandler(stream), parse_buffer_(), 
    on_request_(on_request), on_notify_(on_notify), msgid_factory_(), req_map_(),
    loop_(stream->GetLoop()) {
      if (!use_large_msgid) { msgid_factory_.set_limit(0xFFFF); }
    };

  ~NqSimpleRPCStreamHandler() {
    for (auto &kv : req_map_) {
      kv.second->Cancel();
      delete kv.second;
    }
    req_map_.clear();
  }

  //implements NqStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override { ASSERT(false); }
  void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) override;
  void Notify(uint16_t type, const void *p, nq_size_t len);
  void Reply(nq_result_t result, nq_msgid_t msgid, const void *p, nq_size_t len);

 private:
  DISALLOW_COPY_AND_ASSIGN(NqSimpleRPCStreamHandler);
};

// TODO: customized rpc handler? but needed?

}  // namespace net
