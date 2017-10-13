#pragma once

#include <string>

#include "net/quic/core/quic_stream.h"
#include "net/quic/core/quic_alarm.h"

#include "core/closure.h"
#include "core/msgid_factory.h"
#include "interop/naquid_loop.h"

namespace net {

class NaquidSession;
class NaquidStreamHandler;

class NaquidStream : public QuicStream {
  std::unique_ptr<NaquidStreamHandler> handler_;
  std::string buffer_; //scratchpad for initial handshake (receiver side) or stream protocol name
  bool establish_side_;
 public:
  NaquidStream(QuicStreamId id, NaquidSession* nq_session, bool establish_side);
  ~NaquidStream() override {}

  void OnDataAvailable() override;
  void OnClose() override;

  NaquidStreamHandler *CreateStreamHandler(const std::string &name);
  void Disconnect();

  inline const std::string &protocol() const { return buffer_; }
  inline bool establish_side() const { return establish_side_; }
  bool set_protocol(const std::string &name);

 protected:
  NaquidSession *nq_session();
};



class NaquidStreamHandler {
protected:
  NaquidStream *stream_;
  nq_closure_t on_open_, on_close_;
  bool proto_sent_;
public:
  NaquidStreamHandler(NaquidStream *stream) : stream_(stream), proto_sent_(false) {}
  
  //interface
  virtual void OnRecv(const void *p, nq_size_t len) = 0;
  virtual void Send(const void *p, nq_size_t len) = 0;  
  virtual void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) = 0;

  //operation
  inline bool OnOpen() { return nq_closure_call(on_open_, on_stream_open, CastFrom(this)); }
  inline void OnClose() { nq_closure_call(on_close_, on_stream_close, CastFrom(this)); }
  inline void SetLifeCycleCallback(nq_closure_t on_open, nq_closure_t on_close) {
    on_open_ = on_open;
    on_close_ = on_close;
  }
  inline void Disconnect() { stream_->Disconnect(); }
  inline void ProtoSent() { proto_sent_ = true; }
  inline void WriteBytes(const char *p, nq_size_t len) {
    if (!proto_sent_) { //TODO: use unlikely
      const auto& name = stream_->protocol();
      stream_->WriteOrBufferData(QuicStringPiece(name.c_str(), name.length()), false, nullptr);
      OnOpen(); //client stream side OnOpen
      proto_sent_ = true;
    }
    stream_->WriteOrBufferData(QuicStringPiece(p, len), false, nullptr);
  }
  static inline nq_stream_t CastFrom(NaquidStreamHandler *h) { return (nq_stream_t)h; }
  static const void *ToPV(const char *p) { return static_cast<const void *>(p); }
  static const char *ToCStr(const void *p) { return static_cast<const char *>(p); }
};

// A QUIC stream that separated with encoded length
class NaquidSimpleStreamHandler : public NaquidStreamHandler {
  nq_closure_t on_recv_;
  std::string parse_buffer_;
 public:
  NaquidSimpleStreamHandler(NaquidStream *stream, nq_closure_t on_recv) : 
    NaquidStreamHandler(stream), on_recv_(on_recv), parse_buffer_() {};

  //implements NaquidStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override;
  void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) override { ASSERT(false); }

 private:
  DISALLOW_COPY_AND_ASSIGN(NaquidSimpleStreamHandler);
};

// A QUIC stream that has customized record reader/writer to define record boundary
class NaquidRawStreamHandler : public NaquidStreamHandler {
  nq_closure_t on_recv_, reader_, writer_;
 public:
  NaquidRawStreamHandler(NaquidStream *stream, nq_closure_t on_recv, nq_closure_t reader, nq_closure_t writer) : 
    NaquidStreamHandler(stream), on_recv_(on_recv), reader_(reader), writer_(writer) {}
  //implements NaquidStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override {
    nq_closure_call(writer_, stream_writer, p, len, CastFrom(this));
  }
  void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) override { ASSERT(false); }

 private:
  DISALLOW_COPY_AND_ASSIGN(NaquidRawStreamHandler);
};

// A QUIC stream handles RPC style communication
class NaquidSimpleRPCStreamHandler : public NaquidStreamHandler {
  class Request : public QuicAlarm::Delegate {
   public:
    Request(NaquidSimpleRPCStreamHandler *stream, 
            nq_msgid_t msgid,
            nq_closure_t on_data) : 
            stream_(stream), msgid_(msgid), on_data_(on_data) {}
    void OnAlarm() override { 
      auto it = stream_->req_map_.find(msgid_);
      if (it != stream_->req_map_.end()) {
        stream_->req_map_.erase(it);
      }
    }
   public:
    NaquidSimpleRPCStreamHandler *stream_; 
    nq_msgid_t msgid_, padd_[3];
    nq_closure_t on_data_;
  };
  void EntryRequest(nq_msgid_t msgid, nq_closure_t cb, uint64_t timeout_duration_us = 30 * 1000 * 1000);
 private:
  std::string parse_buffer_;
  nq_closure_t on_recv_;
  nq::MsgIdFactory msgid_factory_;
  std::map<uint32_t, Request*> req_map_;
  NaquidLoop *loop_;
 public:
  NaquidSimpleRPCStreamHandler(NaquidStream *stream, nq_closure_t on_recv) : 
    NaquidStreamHandler(stream), parse_buffer_(), on_recv_(on_recv), msgid_factory_(), req_map_() {};

  static inline nq_rpc_t CastFrom(NaquidSimpleRPCStreamHandler *h) { return (nq_rpc_t)h; }
  //implements NaquidStream
  void OnRecv(const void *p, nq_size_t len) override;
  void Send(const void *p, nq_size_t len) override { ASSERT(false); }
  void Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) override;
  void Send(uint16_t type, const void *p, nq_size_t len);

 private:
  DISALLOW_COPY_AND_ASSIGN(NaquidSimpleRPCStreamHandler);
};

// TODO: customized rpc handler? but needed?

}  // namespace net
