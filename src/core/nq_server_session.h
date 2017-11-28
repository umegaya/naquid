#pragma once

#include <map>
#include <mutex>

#include "core/nq_session.h"
#include "core/nq_server.h"
#include "core/nq_config.h"
#include "core/nq_dispatcher.h"

namespace net {
class NqStream;
class NqServerSession : public NqSession, 
                        public NqSession::Delegate {
 public:
  NqServerSession(QuicConnection *connection,
                  NqDispatcher *dispatcher,
                  const NqServer::PortConfig &port_config);

  inline nq_conn_t ToHandle() { return GetBoxer()->Box(this); }
  inline NqSessionIndex session_index() const { return session_index_; }
  NqStream *FindStream(QuicStreamId id);
  const NqStream *FindStreamForRead(QuicStreamId id) const;
  void RemoveStreamForRead(QuicStreamId id);

  //implements QuicSession
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;
  QuicStream* CreateOutgoingDynamicStream() override;

  //implements NqSession::Delegate
  uint64_t Id() const override { return connection_id(); }
  void *Context() const override { return context_; }
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  void OnOpen(nq_handshake_event_t hsev) override;
  void Disconnect() override;
  bool Reconnect() override; //only supported for client 
  bool IsClient() const override;
  QuicStream *NewStream(const std::string &name) override;
  QuicCryptoStream *NewCryptoStream(NqSession *session) override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
  NqLoop *GetLoop() override { return dispatcher_->loop(); }
  NqBoxer *GetBoxer() override { return dispatcher_; }
  NqSessionIndex SessionIndex() const override { return session_index_; }
  uint64_t ReconnectDurationUS() const override { return 0; }
  QuicConnection *Connection() override { return connection(); }


 private:
  NqDispatcher *dispatcher_;
  const NqServer::PortConfig &port_config_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
  NqSessionIndex session_index_;
  std::map<QuicStreamId, NqServerStream*> read_map_;
  std::mutex read_map_mutex_;
  void *context_;
};

}
