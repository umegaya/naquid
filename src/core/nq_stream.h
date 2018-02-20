#pragma once

#include <string>

#include "net/quic/core/quic_stream.h"
#include "net/quic/core/quic_alarm.h"
#include "net/quic/core/quic_spdy_stream.h"

#include "basis/header_codec.h"
#include "basis/id_factory.h"
#include "basis/timespec.h"
#include "core/nq_closure.h"
#include "core/nq_loop.h"
#include "core/nq_alarm.h"
#include "core/nq_serial_codec.h"

namespace net {

class NqSession;
class NqBoxer;
class NqStreamHandler;

class NqStream : public QuicStream {
 protected:
  NqSerial stream_serial_;
  std::string buffer_; //scratchpad for initial handshake (receiver side) or stream protocol name (including ternminate)
 private:
  std::unique_ptr<NqStreamHandler> handler_;
  SpdyPriority priority_;
  bool establish_side_, established_, proto_sent_;
 public:
  NqStream(QuicStreamId id, 
           NqSession* nq_session, 
           bool establish_side, 
           SpdyPriority priority = kDefaultPriority);
  ~NqStream() override {
    ASSERT(stream_serial_.IsEmpty()); 
  }

  NqLoop *GetLoop();

  inline void SendHandshake() {
    if (!proto_sent_) {
      WriteOrBufferData(QuicStringPiece(buffer_.c_str(), buffer_.length()), false, nullptr);
      proto_sent_ = true;
    }
  }
  bool TryOpenRawHandler(bool *p_on_open_fail);
  bool OpenHandler(const std::string &name, bool update_buffer_with_name);

  void Disconnect();

  void OnDataAvailable() override;
  void OnClose() override;
  virtual void *Context() = 0;
  virtual void **ContextBuffer() = 0;
  virtual NqBoxer *GetBoxer() = 0;
  virtual const std::string &Protocol() const = 0;
  virtual std::mutex &StaticMutex() = 0;
  inline void InvalidateSerial() { 
    std::unique_lock<std::mutex> lk(StaticMutex());
    stream_serial_.Clear(); 
  }

  inline bool establish_side() const { return establish_side_; }
  inline bool proto_sent() const { return proto_sent_; }
  inline void set_proto_sent() { proto_sent_ = true; }
  inline const NqSerial &stream_serial() const { return stream_serial_; }
  //NqSessionIndex session_index() const;

  NqSession *nq_session();
  const NqSession *nq_session() const;
  bool set_protocol(const std::string &name);

  //following code assumes nq_stream_t and nq_rpc_t has exactly same way to create and memory layout, 
  //which can be partially checked this static assertion.
  STATIC_ASSERT(sizeof(nq_stream_t) == sizeof(nq_rpc_t) && sizeof(nq_stream_t) == 16, "size difer");
  STATIC_ASSERT(offsetof(nq_stream_t, p) == offsetof(nq_rpc_t, p) && offsetof(nq_stream_t, p) == 8, "offset of p differ");
  STATIC_ASSERT(offsetof(nq_stream_t, s) == offsetof(nq_rpc_t, s) && offsetof(nq_stream_t, s) == 0, "offset of s differ");
  //FYI(iyatomi): make this virtual if nq_stream_t and nq_rpc_t need to have different memory layout
  inline void RunTask(nq_closure_t cb) { return nq_dyn_closure_call(cb, on_stream_task, ToHandle<nq_stream_t>()); }
  template <class T> inline T *Handler() const { return static_cast<T *>(handler_.get()); }
  template <class H> inline H ToHandle() { return MakeHandle<H, NqStream>(this, stream_serial_); }

 protected:
  friend class NqStreamHandler;
  friend class NqDispatcher;
  NqStreamHandler *CreateStreamHandler(const std::string &name);
  NqStreamHandler *CreateStreamHandler(const nq::HandlerMap::HandlerEntry *he);
};

class NqClientStream : public NqStream {
 public:
  NqClientStream(QuicStreamId id, 
           NqSession* nq_session, 
           bool establish_side, 
           SpdyPriority priority = kDefaultPriority) : 
    NqStream(id, nq_session, establish_side, priority) {}

  void InitSerial(NqStreamIndex idx);
  std::mutex &static_mutex();
  NqBoxer *boxer();

  NqBoxer *GetBoxer() override { return boxer(); }
  void *Context() override;
  void OnClose() override;
  void **ContextBuffer() override;
  const std::string &Protocol() const override;
  std::mutex &StaticMutex() override { return static_mutex(); }
};
class NqServerStream : public NqStream {
  void *context_;
 public:
  NqServerStream(QuicStreamId id, 
           NqSession* nq_session, 
           bool establish_side, 
           SpdyPriority priority = kDefaultPriority) : 
    NqStream(id, nq_session, establish_side, priority), context_(nullptr) {}

  inline void *context() { return context_; }

  void InitSerial(NqStreamIndex idx);
  std::mutex &static_mutex();
  NqBoxer *boxer();

  NqBoxer *GetBoxer() override { return boxer(); }
  void OnClose() override;
  void *Context() override { return context_; }
  void **ContextBuffer() override { return &context_; }
  const std::string &Protocol() const override { return buffer_; }
  std::mutex &StaticMutex() override { return static_mutex(); }
};



class NqStreamHandler {
 protected:
  NqStream *stream_;
  nq_closure_t on_open_;
  nq_closure_t on_close_;
 public:
  NqStreamHandler(NqStream *stream) : stream_(stream) {}
  virtual ~NqStreamHandler() {}
  
  //interface
  virtual void OnRecv(const void *p, nq_size_t len) = 0;
  virtual void Send(const void *p, nq_size_t len) = 0;  
  virtual void SendEx(const void *p, nq_size_t len, const nq_stream_opt_t &opt) = 0;  
  virtual void Cleanup() = 0;

  //operation
  //it has same assumption and restriction as NqStream::RunTask
  inline bool OnOpen() { 
    return nq_dyn_closure_call(on_open_, on_stream_open, stream_->ToHandle<nq_stream_t>(), stream_->ContextBuffer());
  }
  inline void OnClose() { 
    nq_dyn_closure_call(on_close_, on_stream_close, stream_->ToHandle<nq_stream_t>()); 
    Cleanup();
  }
  inline void SetLifeCycleCallback(nq_on_stream_open_t on_open, nq_on_stream_close_t on_close) {
    on_open_ = nq_to_dyn_closure(on_open);
    on_close_ = nq_to_dyn_closure(on_close);
  }
  inline void SetLifeCycleCallback(nq_on_rpc_open_t on_open, nq_on_rpc_close_t on_close) {
    on_open_ = nq_to_dyn_closure(on_open);
    on_close_ = nq_to_dyn_closure(on_close);
  }
  inline void Disconnect() { stream_->Disconnect(); }
  inline NqStream *stream() { return stream_; }
  void WriteBytes(const char *p, nq_size_t len);
  void WriteBytes(const char *p, nq_size_t len, const nq_stream_opt_t &opt);
  static const void *ToPV(const char *p) { return static_cast<const void *>(p); }
  static const char *ToCStr(const void *p) { return static_cast<const char *>(p); }

  static constexpr size_t len_buff_len = nq::LengthCodec::EncodeLength(sizeof(nq_size_t));
  static constexpr size_t header_buff_len = 8;  
 protected:
  NqSession *nq_session() { return stream_->nq_session(); }
};

// A QUIC stream that separated with encoded length
class NqSimpleStreamHandler : public NqStreamHandler {
  nq_on_stream_record_t on_recv_;
  std::string parse_buffer_;
 public:
  NqSimpleStreamHandler(NqStream *stream, nq_on_stream_record_t on_recv) : 
    NqStreamHandler(stream), on_recv_(on_recv), parse_buffer_() {};

  inline void SendCommon(const void *p, nq_size_t len, const nq_stream_opt_t *opt) {
    ALLOCA(buffer, char, len_buff_len + len);
    auto enc_len = nq::LengthCodec::Encode(len, buffer, sizeof(buffer));
    memcpy(buffer + enc_len, p, len);
    if (opt != nullptr) {
      WriteBytes(buffer, enc_len + len, *opt);
    } else {
      WriteBytes(buffer, enc_len + len);    
    }
  }

  //implements NqStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override;
  void SendEx(const void *p, nq_size_t len, const nq_stream_opt_t &opt) override;
  void Cleanup() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NqSimpleStreamHandler);
};

// A QUIC stream that has customized record reader/writer to define record boundary
class NqRawStreamHandler : public NqStreamHandler {
  nq_on_stream_record_t on_recv_;
  nq_stream_reader_t reader_;
  nq_stream_writer_t writer_;
 public:
  NqRawStreamHandler(NqStream *stream, nq_on_stream_record_t on_recv, nq_stream_reader_t reader, nq_stream_writer_t writer) : 
    NqStreamHandler(stream), on_recv_(on_recv), reader_(reader), writer_(writer) {}
    
  inline void SendCommon(const void *p, nq_size_t len, const nq_stream_opt_t *opt) {
    void *buf;
    auto size = nq_closure_call(writer_, stream_->ToHandle<nq_stream_t>(), p, len, &buf);
    if (size <= 0) {
      stream_->Disconnect();
    } else if (opt != nullptr) {
      WriteBytes(static_cast<char *>(buf), size, *opt);
    } else {
      WriteBytes(static_cast<char *>(buf), size);      
    }
  }
  //implements NqStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override { SendCommon(p, len, nullptr); }
  void SendEx(const void *p, nq_size_t len, const nq_stream_opt_t &opt) override { SendCommon(p, len, &opt); }
  void Cleanup() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NqRawStreamHandler);
};

// A QUIC stream handles RPC style communication
class NqSimpleRPCStreamHandler : public NqStreamHandler {
  class Request : public NqAlarmBase {
   public:
    Request(NqSimpleRPCStreamHandler *stream_handler, 
            nq_msgid_t msgid,
            nq_on_rpc_reply_t on_reply) : 
            NqAlarmBase(), 
            stream_handler_(stream_handler), on_reply_(on_reply), msgid_(msgid) {}
    ~Request() {}
    void OnFire(NqLoop *) override { 
      auto it = stream_handler_->req_map_.find(msgid_);
      if (it != stream_handler_->req_map_.end()) {
        nq_closure_call(on_reply_, stream_handler_->stream()->ToHandle<nq_rpc_t>(), NQ_ETIMEOUT, "", 0);
        stream_handler_->req_map_.erase(it);
      }
      delete this;
    }
    inline void GoAway() {
      nq_closure_call(on_reply_, stream_handler_->stream()->ToHandle<nq_rpc_t>(), NQ_EGOAWAY, "", 0);
    }
   private:
    friend class NqSimpleRPCStreamHandler;
    NqSimpleRPCStreamHandler *stream_handler_; 
    nq_on_rpc_reply_t on_reply_;
    nq_msgid_t msgid_/*, padd_[3]*/;
  };
  void EntryRequest(nq_msgid_t msgid, nq_on_rpc_reply_t cb, nq_time_t timeout_duration_ts = 0);
 private:
  std::string parse_buffer_;
  nq_on_rpc_request_t on_request_;
  nq_on_rpc_notify_t on_notify_;
  nq_time_t default_timeout_ts_;
  nq::IdFactory<nq_msgid_t> msgid_factory_;
  std::map<uint32_t, Request*> req_map_;
  NqLoop *loop_;
 public:
  NqSimpleRPCStreamHandler(NqStream *stream, 
    nq_on_rpc_request_t on_request, nq_on_rpc_notify_t on_notify, nq_time_t timeout, bool use_large_msgid) : 
    NqStreamHandler(stream), parse_buffer_(), 
    on_request_(on_request), on_notify_(on_notify), default_timeout_ts_(timeout),
    msgid_factory_(), req_map_(),
    loop_(stream->GetLoop()) {
    if (!use_large_msgid) { msgid_factory_.set_limit(0xFFFF); }
    if (default_timeout_ts_ == 0) { default_timeout_ts_ = nq_time_sec(30); }
  }

  ~NqSimpleRPCStreamHandler() {}

  void Cleanup() override {
    for (auto &kv : req_map_) {
      kv.second->GoAway();
      kv.second->Destroy(loop_);
    }
    req_map_.clear();
  }

  //implements NqStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override { ASSERT(false); }
  void SendEx(const void *p, nq_size_t len, const nq_stream_opt_t &opt) override { ASSERT(false); }  
  virtual void Call(uint16_t type, const void *p, nq_size_t len, nq_on_rpc_reply_t cb);
  virtual void CallEx(uint16_t type, const void *p, nq_size_t len, nq_rpc_opt_t &opt);
  void Notify(uint16_t type, const void *p, nq_size_t len);
  void Reply(nq_error_t result, nq_msgid_t msgid, const void *p, nq_size_t len);

 protected:
  inline void SendCommon(uint16_t type, nq_msgid_t msgid, const void *p, nq_size_t len) {
    ASSERT(type > 0);
    //pack and send buffer
    ALLOCA(buffer, char, header_buff_len + len_buff_len + len);
    size_t ofs = 0;
    ofs = nq::HeaderCodec::Encode(static_cast<int16_t>(type), msgid, buffer, sizeof(buffer));
    ofs += nq::LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
    memcpy(buffer + ofs, p, len);
    WriteBytes(buffer, ofs + len);      
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NqSimpleRPCStreamHandler);
};

// TODO: customized rpc handler? but needed?

}  // namespace net
