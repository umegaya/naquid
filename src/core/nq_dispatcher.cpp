#include "core/nq_dispatcher.h"

#include "net/tools/quic/quic_default_packet_writer.h"

#include "core/compat/nq_client_loop.h"
#include "core/nq_packet_writer.h"
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
  session_limit_(config.server().use_max_session_hint_as_limit ? config.server().max_session_hint : 0), 
  server_(worker.server()), config_(config), crypto_config_(std::move(crypto_config)), loop_(worker.loop()), reader_(worker.reader()), 
  cert_cache_(config.server().quic_cert_cache_size <= 0 ? kDefaultCertCacheSize : config.server().quic_cert_cache_size), 
  thread_id_(worker.thread_id()), server_map_(), alarm_map_(), 
  session_allocator_(config.server().max_session_hint), stream_allocator_(config.server().max_stream_hint),
  alarm_allocator_(config.server().max_session_hint) {
  invoke_queues_ = server_.InvokeQueuesFromPort(port);
  ASSERT(invoke_queues_ != nullptr);
  SetFromConfig(config);
}
void NqDispatcher::SetFromConfig(const NqServerConfig &config) {
  if (config.server().idle_timeout > 0) {
    buffered_packets().SetConnectionLifeSpan(QuicTime::Delta::FromMicroseconds(nq::clock::to_us(config.server().idle_timeout)));
  }
}
void NqDispatcher::Shutdown() {
  nq::logger::info({
    {"msg", "shutdown start"},
    {"worker_index", index_},
    {"port", port_},
    {"session_remain", session_map().size()},
  });
  auto it = session_map().begin();
  for (; it != session_map().end(); ) {
    auto it_prev = it;
    it++;
    const_cast<NqSession *>(static_cast<const NqSession *>(it_prev->second.get()))->delegate()->Disconnect();
  }
}
bool NqDispatcher::ShutdownFinished(nq_time_t shutdown_start) const { 
  if (session_map().size() <= 0) {
    nq::logger::info({
      {"msg", "shutdown finished"},
      {"reason", "all session closed"},
      {"worker_index", index_},
      {"port", port_},
    });
    return true;
  } else if ((shutdown_start + config_.server().shutdown_timeout) < nq_time_now()) {
    nq::logger::error({
      {"msg", "shutdown finished"},
      {"reason", "timeout"},
      {"worker_index", index_},
      {"port", port_},
      {"shutdown_start", shutdown_start},
      {"shutdown_timeout", config_.server().shutdown_timeout},
    });
    return true;
  } else {
    return false;
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
  InitializeWithWriter(new NqPacketWriter(fd));
  return NQ_OK;
}
void NqDispatcher::OnRecv(NqPacket *packet) {
  auto conn_id = packet->ConnectionId();
  if (conn_id == 0) { 
    return; 
  }
  auto idx = conn_id % n_worker_;
  //TRACE("conn_id = %llu @ %d %llu\n", conn_id, index_, idx);
  if (index_ == idx) {
    //TODO(iyatomi): if idx is same as current index, directly process packet here
    Process(packet);
  } else {
    server_.Q4(idx).enqueue(packet);
  }
}

//implements QuicCryptoServerStream::Helper
bool NqDispatcher::CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                        const QuicSocketAddress& self_address,
                                        std::string* error_details) const {
  //TODO(iyatomi): reject when number of connection is too much, getting the config and 
  //total connection number from server_.
  //TODO(iyatomi): if entering shutdown mode, always return false 
  if (!server_.alive()) {
    *error_details = "server entering graceful shutdown";
    return false;
  } else if (session_limit_ > 0 && session_map().size() >= session_limit_) {
    *error_details = "session count exceeds limit";
    return false;
  }
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
  s->InitSerial();
  return s;
}

//implements NqBoxer
void NqDispatcher::Enqueue(Op *op) {
  //TODO(iyatomi): NqDispatcher owns invoke_queue
  invoke_queues_[index_].enqueue(op);
}
NqAlarm *NqDispatcher::NewAlarm() {
  auto a = new(this) NqAlarm();
  auto idx = alarm_map_.Add(a);
  nq_serial_t s;
  NqAlarmSerialCodec::ServerEncode(s, idx);
  a->InitSerial(s);
  return a;
}
void NqDispatcher::RemoveAlarm(NqAlarmIndex index) {
  alarm_map_.Remove(index);
}
}

