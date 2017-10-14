#include "core/nq_dispatcher.h"

#include "net/tools/quic/quic_default_packet_writer.h"

#include "core/nq_client_loop.h"
#include "core/nq_server_session.h"
#include "core/nq_server.h"
#include "core/nq_network_helper.h"
#include "core/nq_stub_interface.h"

namespace net {
NqDispatcher::NqDispatcher(int port, const NqServerConfig& config, NqWorker &worker) : 
	QuicDispatcher(config,
                 config.crypto(),
                 new QuicVersionManager(net::AllSupportedVersions()),
                 std::unique_ptr<QuicConnectionHelperInterface>(new NqStubConnectionHelper(worker.loop())), 
                 std::unique_ptr<QuicCryptoServerStream::Helper>(new NqStubCryptoServerStreamHelper(*this)),
                 std::unique_ptr<QuicAlarmFactory>(new NqStubAlarmFactory(worker.loop()))), 
	port_(port), index_(worker.index()), n_worker_(worker.server().n_worker()), 
  server_(worker.server()), loop_(worker.loop()), reader_(worker.reader()), 
  cert_cache_(config.server().quic_cert_cache_size <= 0 ? kDefaultCertCacheSize : config.server().quic_cert_cache_size), 
  conn_map_() {}

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

    conn_map_[connection_id] = std::unique_ptr<QuicConnection>(connection);

    return new NqServerSession(connection, this, it->second);
}
}
