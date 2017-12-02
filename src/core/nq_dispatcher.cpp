#include "core/nq_dispatcher.h"

#include "net/tools/quic/quic_default_packet_writer.h"

#include "core/nq_client_loop.h"
#include "core/nq_server_session.h"
#include "core/nq_server.h"
#include "core/nq_network_helper.h"
#include "core/nq_stub_interface.h"

namespace net {
NqDispatcher::NqDispatcher(int port, const NqServerConfig& config, 
                           std::unique_ptr<QuicCryptoServerConfig> crypto_config, 
                           NqWorker &worker) : 
	QuicDispatcher(config,
                 crypto_config.get(),
                 new QuicVersionManager(net::AllSupportedVersions()),
                 //TODO(iyatomi): enable to pass worker.loop or this directory to QuicDispatcher ctor. 
                 //main reason to wrap these object now, is QuicDispatcher need to store them with unique_ptr.
                 std::unique_ptr<QuicConnectionHelperInterface>(new NqStubConnectionHelper(worker.loop())), 
                 std::unique_ptr<QuicCryptoServerStream::Helper>(new NqStubCryptoServerStreamHelper(*this)),
                 std::unique_ptr<QuicAlarmFactory>(new NqStubAlarmFactory(worker.loop()))
                ), 
	port_(port), 
  accept_per_loop_(config.server().accept_per_loop <= 0 ? kNumSessionsToCreatePerSocketEvent : config.server().accept_per_loop),
  index_(worker.index()), n_worker_(worker.server().n_worker()), 
  server_(worker.server()), crypto_config_(std::move(crypto_config)), loop_(worker.loop()), reader_(worker.reader()), 
  cert_cache_(config.server().quic_cert_cache_size <= 0 ? kDefaultCertCacheSize : config.server().quic_cert_cache_size), 
  server_map_(), alarm_map_(), thread_id_(worker.thread_id()),
  session_allocator_(config.server().max_session_hint), stream_allocator_(config.server().max_stream_hint) {
  invoke_queues_ = const_cast<NqServer &>(server_).InvokeQueuesFromPort(port);
  ASSERT(invoke_queues_ != nullptr);
  SetFromConfig(config);
}
void NqDispatcher::SetFromConfig(const NqServerConfig &config) {
  if (config.server().idle_timeout > 0) {
    buffered_packets().SetConnectionLifeSpan(QuicTime::Delta::FromMicroseconds(nq::clock::to_us(config.server().idle_timeout)));
  }
}
  



//implement QuicDispatcher
void NqDispatcher::OnConnectionClosed(QuicConnectionId connection_id,
                        QuicErrorCode error,
                        const std::string& error_details) {
  auto it = session_map().find(connection_id);
  if (it != session_map().end()) {
    auto idx = static_cast<NqServerSession *>(it->second.get())->session_index();
    server_map_.Remove(idx);
    server_map_.Deactivate(idx); //make connection invalid 
  }
  QuicDispatcher::OnConnectionClosed(connection_id, error, error_details);  
}

//implements nq::IoProcessor
void NqDispatcher::OnEvent(nq::Fd fd, const Event &e) {
  if (NqLoop::Writable(e)) {
    writer()->SetWritable(); //indicate fd become writable
  }
  if (NqLoop::Readable(e)) {
    while (reader_.Read(fd, port_, *(loop_.GetClock()), this, nullptr)) {}
  } 
}
int NqDispatcher::OnOpen(nq::Fd fd) {
  InitializeWithWriter(new QuicDefaultPacketWriter(fd));
  return NQ_OK;
}
void NqDispatcher::OnRecv(NqPacket *packet) {
  auto conn_id = packet->ConnectionId();
  if (conn_id == 0) { 
    return; 
  }
  //fprintf(stderr, "conn_id = %llu @ %d\n", conn_id, index_);
  auto idx = conn_id % n_worker_;
  if (index_ == idx) {
    //TODO(iyatomi): if idx is same as current index, directly process packet here
    Process(packet);
  } else {
    //otherwise send other queue (sorry const_cast!)
    const_cast<NqServer &>(server_).Q4(idx).enqueue(packet);
  }
}

//implements QuicCryptoServerStream::Helper
bool NqDispatcher::CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                            const QuicSocketAddress& self_address,
                                            std::string* error_details) const {
  //TODO(iyatomi): reject when number of connection is too much, getting the config and 
  //total connection number from server_.
  //TODO(iyatomi): if entering shutdown mode, always return false 
  return true;
}


//implements QuicDispatcher
QuicSession* NqDispatcher::CreateQuicSession(QuicConnectionId connection_id,
                                             const QuicSocketAddress& client_address,
                                             QuicStringPiece alpn) {
    auto it = server_.port_configs().find(port_);
    if (it == server_.port_configs().end()) {
    	return nullptr;
    }

    QuicConnection* connection = new QuicConnection(
      connection_id, client_address, &loop_, &loop_,
      CreatePerConnectionWriter(),
      /* owns_writer= */ true, Perspective::IS_SERVER, GetSupportedVersions());

    auto s = new(this) NqServerSession(connection, it->second);
    s->Initialize();
    server_map_.Add(s->session_index(), s);
    server_map_.Activate(s->session_index(), s); //make connection valid
    s->OnOpen(NQ_HS_START);
    return s;
}

//implements NqBoxer
void NqDispatcher::Enqueue(Op *op) {
  int windex;
  switch (op->target_) {
  case NqBoxer::Conn:
    windex = NqConnSerialCodec::ServerWorkerIndex(op->serial_);
    break;
  case NqBoxer::Stream:
    windex = NqStreamSerialCodec::ServerWorkerIndex(op->serial_);
    break;
  default:
    ASSERT(false);
    return;
  }
  invoke_queues_[windex].enqueue(op);
}
nq_conn_t NqDispatcher::Box(NqSession::Delegate *d) {
  auto ss = static_cast<NqServerSession *>(d);
  return {
    .p = static_cast<NqBoxer*>(this),
    .s = NqConnSerialCodec::ServerEncode(
      ss->session_index(),
      ss->connection_id(),
      n_worker_
    ),
  };
}
nq_stream_t NqDispatcher::Box(NqStream *s) {
  auto ss = static_cast<NqServerSession *>(s->nq_session());
  return {
    .p = static_cast<NqBoxer*>(this),
    .s = NqStreamSerialCodec::ServerEncode(
      ss->session_index(),
      ss->connection_id(),
      s->id(),
      n_worker_
    ),
  };
}
nq_alarm_t NqDispatcher::Box(NqAlarm *a) {
  AddAlarm(a);
  return {
    .p = static_cast<NqBoxer*>(this),
    .s = NqAlarmSerialCodec::ServerEncode(a->alarm_index(), index_),
  };
}
NqBoxer::UnboxResult 
NqDispatcher::Unbox(uint64_t serial, NqSession::Delegate **unboxed) {
  if (!main_thread()) {
    return NqBoxer::UnboxResult::NeedTransfer;
  }
  auto index = NqConnSerialCodec::ServerSessionIndex(serial);
  auto it = server_map_.find(index);
  if (it != server_map_.end()) {
    *unboxed = it->second;
    return NqBoxer::UnboxResult::Ok;
  }
  return NqBoxer::UnboxResult::SerialExpire;  
}
NqBoxer::UnboxResult
NqDispatcher::Unbox(uint64_t serial, NqStream **unboxed) {
  if (!main_thread()) {
    return NqBoxer::UnboxResult::NeedTransfer;
  }
  auto index = NqStreamSerialCodec::ServerSessionIndex(serial);
  auto it = server_map_.find(index);
  if (it != server_map_.end()) {
    auto stream_id = NqStreamSerialCodec::ServerStreamId(serial);
    *unboxed = it->second->FindStream(stream_id);
    if (*unboxed != nullptr) {
      return NqBoxer::UnboxResult::Ok;
    }
  }
  return NqBoxer::UnboxResult::SerialExpire;
}
NqBoxer::UnboxResult 
NqDispatcher::Unbox(uint64_t serial, NqAlarm **unboxed) {
  if (!main_thread()) {
    return NqBoxer::UnboxResult::NeedTransfer;
  }
  auto index = NqAlarmSerialCodec::ServerAlarmIndex(serial);
  auto it = alarm_map_.find(index);
  if (it != alarm_map_.end()) {
    *unboxed = it->second;
    return NqBoxer::UnboxResult::Ok;
  }
  return NqBoxer::UnboxResult::SerialExpire;
}
const NqSession::Delegate *NqDispatcher::FindConn(uint64_t serial, OpTarget target) const {
  switch (target) {
  case Conn:
    return server_map().Active(NqConnSerialCodec::ServerSessionIndex(serial));
  case Stream:
    return server_map().Active(NqStreamSerialCodec::ServerSessionIndex(serial));
  default:
    ASSERT(false);
    return nullptr;
  }
}
const NqStream *NqDispatcher::FindStream(uint64_t serial) const {
  auto c = server_map().Active(NqStreamSerialCodec::ServerSessionIndex(serial));
  if (c == nullptr) { return nullptr; }
  return c->FindStreamForRead(NqStreamSerialCodec::ServerStreamId(serial));
}
void NqDispatcher::AddAlarm(NqAlarm *a) {
  if (a->alarm_index() == 0) {
    a->set_alarm_index(new_alarm_index());
    alarm_map_.Add(a->alarm_index(), a);
  } else {
#if defined(DEBUG)
    auto it = alarm_map_.find(a->alarm_index());
    ASSERT(it != alarm_map_.end() && it->second == a);
#endif
  }
}
void NqDispatcher::RemoveAlarm(NqAlarmIndex index) {
  alarm_map_.Remove(index);
}
}

