#pragma once


#include "basis/handler_map.h"
#include "core/nq_serial_codec.h"

namespace net {
class NqLoop;
class NqSession;
class NqSessionDelegate {
 public:
  virtual ~NqSessionDelegate() {}
  virtual void *Context() const = 0;
  virtual void Destroy() = 0;
  virtual void OnClose(QuicErrorCode error,
                          const std::string& error_details,
                          ConnectionCloseSource close_by_peer_or_self) = 0;
  virtual void OnOpen() = 0;
  virtual void Disconnect() = 0;
  virtual bool Reconnect() = 0; //only supported for client 
  virtual void DoReconnect() = 0;
  virtual void OnReachabilityChange(nq_reachability_t state) = 0;
  virtual uint64_t ReconnectDurationUS() const = 0;
  virtual bool IsClient() const = 0;
  virtual bool IsConnected() const = 0;
  virtual void InitStream(const std::string &name, void *ctx) = 0;
  virtual void OpenStream(const std::string &name, void *ctx) = 0;
  virtual int UnderlyingFd() = 0;
  virtual const nq::HandlerMap *GetHandlerMap() const = 0;
  virtual nq::HandlerMap *ResetHandlerMap() = 0;
  virtual NqLoop *GetLoop() = 0;
  virtual NqQuicConnectionId ConnectionId() = 0;
  virtual void FlushWriteBuffer() = 0;
  virtual const NqSerial &SessionSerial() const = 0;
  inline NqSessionIndex SessionIndex() const { 
      return NqSerial::ObjectIndex<NqSessionIndex>(SessionSerial());
  }
};
}