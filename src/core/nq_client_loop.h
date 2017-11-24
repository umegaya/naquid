#pragma once

#include <string>
#include <thread>

#include "net/quic/core/quic_server_id.h"
#include "net/quic/core/quic_version_manager.h"

#include "core/nq_loop.h"
#include "core/nq_config.h"
#include "core/nq_boxer.h"

namespace net {
class NqClient;
class NqAlarm;
class NqClientLoop : public NqLoop,
                     public NqBoxer,
                     public QuicSession::Visitor {
  nq::HandlerMap handler_map_;
  typedef NqObjectExistenceMapMT<NqClient, NqSessionIndex> ClientMap;
  ClientMap client_map_;
  NqObjectExistenceMap<NqAlarm, NqAlarmIndex> alarm_map_;
  NqBoxer::Processor processor_;
  QuicVersionManager versions_;
  std::thread::id thread_id_;
 public:
  NqClientLoop() : handler_map_(), client_map_(), alarm_map_(), processor_(), versions_(net::AllSupportedVersions()) {
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
  inline NqAlarmIndex new_alarm_index() { return alarm_map_.NewIndex(); }
  inline nq_client_t ToHandle() { return (nq_client_t)this; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }
  inline void set_main_thread() { thread_id_ = std::this_thread::get_id(); }
  inline const ClientMap &client_map() const { return client_map_; }
  inline ClientMap &client_map() { return client_map_; }

  static inline NqClientLoop *FromHandle(nq_client_t cl) { return (NqClientLoop *)cl; }
  static bool ParseUrl(const std::string &host, 
                       int port, 
                       int address_family,
                       QuicServerId& server_id, 
                       QuicSocketAddress &address, 
                       QuicConfig &config);

  //implements NqBoxer
  void Enqueue(NqBoxer::Op *op) override { processor_.enqueue(op); }
  NqLoop *Loop() override { return this; }
  nq_conn_t Box(NqSession::Delegate *d) override;
  nq_stream_t Box(NqStream *s) override;
  nq_alarm_t Box(NqAlarm *a) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqSession::Delegate **unboxed) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqStream **unboxed) override;
  NqBoxer::UnboxResult Unbox(uint64_t serial, NqAlarm **unboxed) override;
  bool IsClient() const override { return true; }
  const NqSession::Delegate *FindConn(uint64_t serial, NqBoxer::OpTarget target) const override;
  const NqStream *FindStream(uint64_t serial) const override;
  void RemoveAlarm(NqAlarmIndex index) override;

  //implement QuicSession::Visitor
  void OnConnectionClosed(QuicConnectionId connection_id,
                          QuicErrorCode error,
                          const std::string& error_details) override {}
  // Called when the session has become write blocked.
  void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) override {}
  // Called when the session receives reset on a stream from the peer.
  void OnRstStreamReceived(const QuicRstStreamFrame& frame) override {}

 protected:
  void AddAlarm(NqAlarm *a);
};
}
