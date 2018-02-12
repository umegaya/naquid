#pragma once

#include <map>
#include <mutex>

#include "core/nq_session.h"
#include "core/nq_server.h"
#include "core/nq_config.h"

namespace net {
class NqStream;
class NqServerSession : public NqSession, 
                        public NqSession::Delegate {
 public:
  NqServerSession(QuicConnection *connection,
                  const NqServer::PortConfig &port_config);
  ~NqServerSession() { ASSERT(session_serial_.IsEmpty()); }

  nq_conn_t ToHandle();
  NqStream *FindStream(QuicStreamId id);
  NqStreamIndex NewStreamIndex();
  //if you set included closed to true, be careful to use returned value, 
  //this pointer soon will be invalid.
  NqStream *FindStreamBySerial(const nq_serial_t &s, bool include_closed = false);
  void InitSerial();
  inline void InvalidateSerial() { 
    std::unique_lock<std::mutex> lk(static_mutex());
    session_serial_.Clear(); 
  }

  std::mutex &static_mutex();
  NqBoxer *boxer();
  NqDispatcher *dispatcher();
  inline const NqSerial &session_serial() const { return session_serial_; }
  inline NqSessionIndex session_index() const { 
    return NqConnSerialCodec::ServerSessionIndex(session_serial_); }


  //implements QuicSession
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicStream* CreateOutgoingDynamicStream() override;


  //implements NqSession::Delegate
  void *Context() const override { return context_; }
  void Destroy() override { ASSERT(false); }
  void DoReconnect() override { ASSERT(false); }
  void OnReachabilityChange(nq_reachability_t) override { ASSERT(false); }
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  void OnOpen() override;
  void Disconnect() override;
  bool Reconnect() override; //only supported for client 
  bool IsClient() const override;
  bool IsConnected() const override { return true; }
  void InitStream(const std::string &name, void *ctx) override;
  void OpenStream(const std::string &name, void *ctx) override;
  QuicCryptoStream *NewCryptoStream(NqSession *session) override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
  NqLoop *GetLoop() override;
  uint64_t ReconnectDurationUS() const override { return 0; }
  QuicConnection *Connection() override { return connection(); }
  const NqSerial &SessionSerial() const override { return session_serial(); }

 private:
  const NqServer::PortConfig &port_config_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
  NqSerial session_serial_;
  void *context_;
};

}
