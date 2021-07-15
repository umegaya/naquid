#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <mutex>
#include <stack>
#include <functional>

#include <arpa/inet.h>

#include "core/compat/nq_quic_types.h"

#include "basis/allocator.h"
#include "core/nq_alarm.h"
#include "core/nq_config.h"
#include "core/nq_session_delegate.h"

namespace net {

class NqQuicServerId;
class NqClientLoop;
class NqClientStream;
class NqBoxer;
class NqClient;
class NqReachability;

class NqClientBase : public NqSessionDelegate,
                     public NqAlarmBase {
 public:
  enum ConnectState : uint8_t {
    DISCONNECT,
    CONNECTING,
    CONNECTED,
    FINALIZED,
    RECONNECTING,
  };
  class StreamManager {
    struct Entry {
      NqClientStream *handle_;
      std::string name_;
      void *context_;
      Entry(NqClientStream *h) : 
        handle_(h), name_(), context_(nullptr) {}
      Entry(NqClientStream *h, const std::string &name) : 
        handle_(h), name_(name), context_(nullptr) {}
      inline void SetStream(NqClientStream *s) { handle_ = s; }
      inline void ClearStream() { handle_ = nullptr; }
      inline NqClientStream *Stream() { return handle_; }
      inline void *Context() const { return context_; }
      inline void **ContextBuffer() { return &context_; }      
      inline void SetName(const std::string &name) { name_ = name; }
      inline const std::string &Name() const { return name_; }
    };

    // MUST_DO(iyatomi): re-implement QuicSmallMap without chromium sources for performance (and use size 10)
    using StreamMap = std::map<NqStreamIndex, Entry>;

    StreamMap entries_;
   public:
    StreamManager() : entries_() {}
    
    bool OnOutgoingOpen(NqClientBase *client, bool connected,
                        const std::string &name, void *ctx);
    NqStreamIndex OnIncomingOpen(NqClientBase *client, NqClientStream *s);
    void OnClose(NqClientStream *s);

    NqClientStream *FindOrCreateStream(
      NqClientBase *client, 
      NqStreamIndex stream_index, 
      bool connected);

    //recover all created outgoing streams on reconnection done
    void RecoverOutgoingStreams(NqClientBase *client);
    void CleanupStreamsOnClose();
    
    //it should be used by non-owner thread of this client. 
    inline void *FindContext(NqStreamIndex index) const {
      auto *e = FindEntry(index);
      return e == nullptr ? nullptr : e->Context();
    }
    inline void **FindContextBuffer(NqStreamIndex index) {
      auto *e = FindEntry(index);
      return e == nullptr ? nullptr : e->ContextBuffer();
    }
    inline const std::string &FindStreamName(NqStreamIndex index) const {
      static std::string empty_;
      auto *e = FindEntry(index);
      return e == nullptr ? empty_ : e->Name();
    }
   protected:
    Entry *FindEntry(NqStreamIndex index);
    inline const Entry *FindEntry(NqStreamIndex index) const { 
      return const_cast<StreamManager *>(this)->FindEntry(index); 
    }
    void OnOutgoingClose(NqClientStream *s);
    void OnIncomingClose(NqClientStream *s);    
  };
 public:
  // This will create its own QuicClientEpollNetworkHelper.
  NqClientBase(NqQuicSocketAddress server_address,
               NqClientLoop &loop,
               const NqQuicServerId& server_id,
               const NqClientConfig &config);
  ~NqClientBase() override;

  // getter
  inline bool destroyed() const { return connect_state_ == FINALIZED; }
  inline StreamManager &stream_manager() { return stream_manager_; }
  inline const StreamManager &stream_manager() const { return stream_manager_; }
  inline NqClientLoop *client_loop() { return loop_; }
  inline const NqSerial &session_serial() const { return session_serial_; }
  inline NqSessionIndex session_index() const { 
    return NqConnSerialCodec::ClientSessionIndex(session_serial_); }
    inline bool IsReachabilityTracked() const { return reachability_ != nullptr; }
  std::mutex &static_mutex();
  NqBoxer *boxer();
  NqClient *nq_client();
  
  // operation
  nq_conn_t ToHandle();
  NqClientStream *FindOrCreateStream(NqStreamIndex index);
  NqStreamIndex NewStreamIndex();
  void InitSerial();
  void ScheduleDestroy();
  void OnFinalize();

  bool TrackReachability(const std::string &host);
  
  inline void InvalidateSerial() { 
    std::unique_lock<std::mutex> lk(static_mutex());
    session_serial_.Clear(); 
  }

  void OnInitializeSession() { connect_state_ = CONNECTING; }

  // NqClientBase abstract interface
  virtual bool Initialize() = 0;
  virtual void StartConnect() = 0;
  virtual void StartDisconnect() = 0;
  virtual void ForceShutdown() = 0;
  virtual bool MigrateSocket() = 0;
  virtual NqClientStream *NewStream() = 0;


  // implements NqAlarmBase
  void OnFire() override;
  bool IsNonQuicAlarm() const override { return true  ; }

  // implements NqSession::Delegate
  void *Context() const override { return context_; }
  void Destroy() override;
  void DoReconnect() override;
  void OnClose(QuicErrorCode error,
               const std::string& error_details,
               ConnectionCloseSource close_by_peer_or_self) override;
  void OnOpen() override;
  bool IsClient() const override { return true; }
  bool IsConnected() const override { return connect_state_ == CONNECTED; }
  void Disconnect() override;
  bool Reconnect() override;
  void OnReachabilityChange(nq_reachability_t state) override;
  uint64_t ReconnectDurationUS() const override;
  const nq::HandlerMap *GetHandlerMap() const override;
  nq::HandlerMap *ResetHandlerMap() override;
  void InitStream(const std::string &name, void *ctx) override;
  void OpenStream(const std::string &name, void *ctx) override;
  NqLoop *GetLoop() override;
  // implement at NqClientCompat
  // NqQuicConnectionId ConnectionId() override
  // void FlushWriteBuffer() override 
  // int UnderlyingFd() override
  const NqSerial &SessionSerial() const override { return session_serial(); }

  //callback for reachability
  static void OnReachabilityChangeTranpoline(void *self, nq_reachability_t status);

 protected:
  NqClientLoop* loop_;
  std::unique_ptr<nq::HandlerMap> own_handler_map_;
  nq_on_client_conn_close_t on_close_;
  nq_on_client_conn_open_t on_open_;
  nq_on_client_conn_finalize_t on_finalize_;
  NqSerial session_serial_;
  StreamManager stream_manager_;
  uint64_t next_reconnect_us_ts_;
  nq::atomic<ConnectState> connect_state_;
  void *context_;
  NqReachability *reachability_;

  DISALLOW_COPY_AND_ASSIGN(NqClientBase);
};

} //net
