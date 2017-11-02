#pragma once

#include <string>
#include <thread>

#include "net/quic/core/quic_server_id.h"

#include "core/nq_loop.h"
#include "core/nq_config.h"
#include "core/nq_boxer.h"

namespace net {
class NqClient;
class NqClientLoop : public NqLoop,
                     public NqBoxer {
  nq::HandlerMap handler_map_;
  NqSessionContainer<NqClient> client_map_;
  NqBoxer::Processor processor_;
  std::thread::id thread_id_;
 public:
  NqClientLoop() : handler_map_(), client_map_(), processor_() {
    set_main_thread();
  }
  ~NqClientLoop() { Close(); }

  void Poll();
  void Close();
  void RemoveClient(NqClient *cl);
  NqClient *Create(const std::string &host, 
                   int port, 
                   NqClientConfig &config);

  inline nq::HandlerMap *mutable_handler_map() { return &handler_map_; }
  inline const nq::HandlerMap *handler_map() const { return &handler_map_; }
  inline NqSessionIndex new_session_index() { return client_map_.NewIndex(); }
  inline nq_client_t ToHandle() { return (nq_client_t)this; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }
  inline void set_main_thread() { thread_id_ = std::this_thread::get_id(); }
  inline const NqSessionContainer<NqClient> &client_map() const { return client_map_; }

  static inline NqClientLoop *FromHandle(nq_client_t cl) { return (NqClientLoop *)cl; }
  static bool ParseUrl(const std::string &host, 
                       int port, 
                       int address_family,
                       QuicServerId& server_id, 
                       QuicSocketAddress &address, 
                       QuicConfig &config);

  //implements NqBoxer
  void Enqueue(NqBoxer::Op *op) override { processor_.enqueue(op); }
  nq_conn_t Box(NqSession::Delegate *d) override;
  nq_stream_t Box(NqStream *s) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqSession::Delegate **unboxed) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqStream **unboxed) override;
  bool IsClient() const override { return true; }
  bool Valid(uint64_t serial, OpTarget target) const override {
    switch (target) {
    case Conn:
      return client_map().Has(NqConnSerialCodec::ClientSessionIndex(serial));
    case Stream:
      return client_map().Has(NqStreamSerialCodec::ClientSessionIndex(serial));
    default:
      ASSERT(false);
      return false;
    }
  }
};
}
