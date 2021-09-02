#pragma once

#include <map>
#include <mutex>

#include "core/compat/nq_server_session_compat.h"
#include "core/nq_session_delegate.h"
#include "core/nq_server.h"
#include "core/nq_config.h"

namespace nq {
class NqStream;
class NqServerSession : public NqServerSessionCompat {
 public:
  NqServerSession(NqQuicConnection *connection,
                  const NqServer::PortConfig &port_config);        
  ~NqServerSession() override { ASSERT(session_serial_.IsEmpty()); }

  nq_conn_t ToHandle();
  NqStream *FindStream(NqQuicStreamId id);
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
  inline const NqSerial &session_serial() const { return session_serial_; }
  inline NqSessionIndex session_index() const { 
    return NqConnSerialCodec::ServerSessionIndex(session_serial_); }

  //implements NqSessionDelegate (via NqServerSessionCompat)
  void *Context() const override { return context_; }
  void Destroy() override { ASSERT(false); }
  void DoReconnect() override { ASSERT(false); }
  void OnReachabilityChange(nq_reachability_t) override { ASSERT(false); }
  void OnClose(int error, const std::string& error_details, bool close_by_peer_or_self) override;
  void OnOpen() override;
  void Disconnect() override { DisconnectImpl(); }
  bool Reconnect() override; //only supported for client 
  bool IsClient() const override;
  bool IsConnected() const override { return true; }
  void InitStream(const std::string &name, void *ctx) override;
  void OpenStream(const std::string &name, void *ctx) override;
  int UnderlyingFd() override;
  const HandlerMap *GetHandlerMap() const override;
  HandlerMap *ResetHandlerMap() override;
  NqLoop *GetLoop() override;
  uint64_t ReconnectDurationUS() const override { return 0; }
  NqQuicConnectionId ConnectionId() override { return ConnectionIdImpl(); }
  void FlushWriteBuffer() override { FlushWriteBufferImpl(); }
  const NqSerial &SessionSerial() const override { return session_serial(); }

 private:
  const NqServer::PortConfig &port_config_;
  std::unique_ptr<HandlerMap> own_handler_map_;
  NqSerial session_serial_;
  void *context_;
};

}
