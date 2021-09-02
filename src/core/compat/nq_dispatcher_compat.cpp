#include "core/nq_dispatcher.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/tools/quic/quic_default_packet_writer.h"

#include "core/nq_server_loop.h"
#include "core/nq_server_session.h"
#include "core/nq_server.h"
#include "core/compat/chromium/nq_network_helper.h"
#include "core/compat/chromium/nq_packet_writer.h"
#include "core/compat/chromium/nq_stub_interface.h"

namespace nq {
using namespace net;
NqDispatcherCompat::NqDispatcherCompat(int port, const NqServerConfig& config, NqWorker &worker) 
  : NqDispatcherBase(port, config, worker),
  crypto_config_(config.NewCryptoConfig(&(worker.loop()))),
  cert_cache_(config.server().quic_cert_cache_size <= 0 ? kDefaultCertCacheSize : config.server().quic_cert_cache_size),
  dispatcher_(*this, crypto_config_.get(), config, worker) {
}


//implement NqDispatcherBase
void NqDispatcherCompat::SetFromConfig(const NqServerConfig &config) {
  if (config.server().idle_timeout > 0) {
    dispatcher_.buffered_packets().SetConnectionLifeSpan(
      QuicTime::Delta::FromMicroseconds(clock::to_us(config.server().idle_timeout))
    );
  }
}
void NqDispatcherCompat::Shutdown() {
  logger::info({
    {"msg", "shutdown start"},
    {"worker_index", index_},
    {"port", port_},
    {"session_remain", dispatcher_.session_map().size()},
  });
  auto it = dispatcher_.session_map().begin();
  for (; it != dispatcher_.session_map().end(); ) {
    auto it_prev = it;
    it++;
    const_cast<NqSession *>(static_cast<const NqSession *>(it_prev->second.get()))->delegate()->Disconnect();
  }
}
bool NqDispatcherCompat::ShutdownFinished(nq_time_t shutdown_start) const { 
  if (dispatcher_.session_map().size() <= 0) {
    logger::info({
      {"msg", "shutdown finished"},
      {"reason", "all session closed"},
      {"worker_index", index_},
      {"port", port_},
    });
    return true;
  } else if ((shutdown_start + config_.server().shutdown_timeout) < nq_time_now()) {
    logger::error({
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


//implements IoProcessor
void NqDispatcherCompat::OnEvent(Fd fd, const Event &e) {
  if (NqLoop::Writable(e)) {
    dispatcher_.writer()->SetWritable(); //indicate fd become writable
  }
  if (NqLoop::Readable(e)) {
    while (dispatcher_.reader().Read(fd, port_, *(loop_.GetClock()), this, nullptr)) {}
  } 
}
int NqDispatcherCompat::OnOpen(Fd fd) {
  dispatcher_.InitializeWithWriter(new chromium::NqPacketWriter(fd));
  return NQ_OK;
}


//implements chromium::NqPacketReader::Delegate
void NqDispatcherCompat::OnRecv(NqPacket *packet) {
  auto conn_id = packet->ConnectionId();
  if (conn_id == 0) { 
    return; 
  }
  auto idx = conn_id % n_worker_;
  //TRACE("conn_id = %llu @ %d %llu\n", conn_id, index_, idx);
  if (index_ == idx) {
    //TODO(iyatomi): if idx is same as current index, directly process packet here
    dispatcher_.Process(packet);
  } else {
    server_.Q4(idx).enqueue(packet);
  }
}


//implements QuicCryptoServerStream::Helper
bool NqDispatcherCompat::CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                        const QuicSocketAddress& self_address,
                                        std::string* error_details) const {
  //TODO(iyatomi): reject when number of connection is too much, getting the config and 
  //total connection number from server_.
  //TODO(iyatomi): if entering shutdown mode, always return false 
  if (!server_.alive()) {
    *error_details = "server entering graceful shutdown";
    return false;
  } else if (session_limit_ > 0 && dispatcher_.session_map().size() >= session_limit_) {
    *error_details = "session count exceeds limit";
    return false;
  }
  return true;
}


// QuicDispatcher delegates
void NqDispatcherCompat::OnSessionClosed(NqServerSession *session) {
  auto idx = session->session_index();
  server_map_.Remove(idx);
}
QuicSession* NqDispatcherCompat::CreateQuicSession(QuicConnectionId connection_id,
                                                   const QuicSocketAddress& client_address,
                                                   QuicStringPiece alpn) {
  auto it = server_.port_configs().find(port_);
  if (it == server_.port_configs().end()) {
    return nullptr;
  }

  NqQuicConnection* connection = new NqQuicConnection(
    connection_id, client_address, &loop_, &loop_,
    dispatcher_.CreatePerConnectionWriterPublic(),
    /* owns_writer= */ true, Perspective::IS_SERVER, 
    dispatcher_.GetSupportedVersionsPublic());

  auto s = new(this) NqServerSession(connection, it->second);
  s->Initialize();
  s->InitSerial();
  return s;
}

} //namespace nq
#else
namespace nq {
void NqDispatcherCompat::OnEvent(Fd fd, const Event &e) {
  ASSERT(false);
}
int NqDispatcherCompat::OnOpen(Fd fd) {
  ASSERT(false);
  return NQ_OK;
}
} //namespace nq
#endif