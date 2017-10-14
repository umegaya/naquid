#pragma once

#include <map>
#include <thread>

#include "net/tools/quic/quic_dispatcher.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"

#include "interop/naquid_worker.h"

namespace net {
class NaquidWorker;
class NaquidServerConfig;
class NaquidDispatcher : public QuicDispatcher, 
                         public nq::IoProcessor,
                         public QuicCryptoServerStream::Helper,
                         public NaquidPacketReader::Delegate {
  int port_, index_, n_worker_;
  const NaquidServer &server_;
  NaquidServerLoop &loop_;
  NaquidPacketReader &reader_;
  QuicCompressedCertsCache cert_cache_;
  std::map<QuicConnectionId, std::unique_ptr<QuicConnection>> conn_map_;
 public:
  //TODO(iyatomi): find proper cache size
  static const int kDefaultCertCacheSize = 16; 
  NaquidDispatcher(int port, const NaquidServerConfig& config, NaquidWorker &worker);
  void Process(NaquidPacket *p) {
    ProcessPacket(p->server_address(), p->client_address(), *p);
    reader_.Pool(const_cast<char *>(p->data()), p);
  }
  inline QuicCompressedCertsCache *cert_cache() { return &cert_cache_; }
  //implements nq::IoProcessor
  void OnEvent(nq::Fd fd, const Event &e) override;
  void OnClose(nq::Fd fd) override {}
	int OnOpen(nq::Fd fd) override;

  //implements NaquidPacketReader::Delegate
  void OnRecv(NaquidPacket *packet) override;

  //implements QuicCryptoServerStream::Helper
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const override {
    return loop_.GetRandomGenerator()->RandUint64();
  }
  // Returns true if |message|, which was received on |self_address| is
  // acceptable according to the visitor's policy. Otherwise, returns false
  // and populates |error_details|.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                    const QuicSocketAddress& self_address,
                                    std::string* error_details) const override;


 protected:
  //implements QuicDispatcher
  QuicSession* CreateQuicSession(
    QuicConnectionId connection_id,
    const QuicSocketAddress& client_address,
    QuicStringPiece alpn) override;
};
}