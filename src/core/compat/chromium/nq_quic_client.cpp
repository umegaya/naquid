#include "net/quic/platform/api/quic_ptr_util.h"
#include "core/nq_client_loop.h"
#include "core/compat/nq_client_compat.h"
#include "core/compat/chromium/nq_client_session.h"
#include "core/compat/chromium/nq_stub_interface.h"
#include "core/compat/chromium/nq_network_helper.h"

namespace nq {
namespace chromium {
using namespace net;

NqQuicClient::NqQuicClient(NqClientCompat *client,
                           NqClientLoop &loop,
                           const NqQuicServerId &server_id,
                           const NqClientConfig &config)
  : QuicClientBase(
    server_id,
    config.protocol_manager().supported_versions(),
    config.chromium(),
    new NqStubConnectionHelper(loop),
    new NqStubAlarmFactory(loop),
    QuicWrapUnique(new NqNetworkHelper(&loop, client)),
    config.NewProofVerifier()
  ), client_(client) {
}

// operation
void NqQuicClient::ForceShutdown() {
  if (connected()) {
    session()->connection()->CloseConnection(
      QUIC_PEER_GOING_AWAY, "Client being torn down",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }
}

// getter/setter
NqClientLoop *NqQuicClient::loop() { 
  return static_cast<NqClientLoop *>(static_cast<NqStubConnectionHelper *>(helper())->loop()); 
}


// implements QuicClientBase
std::unique_ptr<QuicSession>
NqQuicClient::CreateQuicClientSession(NqQuicConnection* connection) {
  auto cs = new NqClientSession(connection, loop(), client_, *config());
  cs->InitCryptoStream(
    new QuicCryptoClientStream(server_id(), cs, new ProofVerifyContext(), crypto_config(), this)
  );
  return QuicWrapUnique(cs);
}
void NqQuicClient::InitializeSession() {
  client_->OnInitializeSession();
  QuicClientBase::InitializeSession();
  nq_session()->GetClientCryptoStream()->CryptoConnect();
}


//implements QuicCryptoClientStream::ProofHandler
void NqQuicClient::OnProofValid(const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(iyatomi): Handle the proof verification.
}
void NqQuicClient::OnProofVerifyDetailsAvailable(const ProofVerifyDetails& verify_details) {
  // TODO(iyatomi): Handle the proof verification.
}
} //namespace chromium
} //namespace nq
