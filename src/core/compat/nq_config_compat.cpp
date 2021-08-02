#include "core/nq_config.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/cert/ct_known_logs.h"
#include "net/cert/ct_log_verifier.h"

namespace net {
void NqClientConfigCompat::Setup() { //init other variables from client_
  config_.ConfigureSelf(client_);
  NqClientConfigBase::Setup();
}
std::unique_ptr<ProofVerifier> NqClientConfigCompat::NewProofVerifier() const { 
  if (client_.insecure) {
    return std::unique_ptr<ProofVerifier>(new NqNoopProofVerifier);
  } else {
    return std::unique_ptr<ProofVerifier>(new NqProofVerifier);
  }
}



void NqServerConfigCompat::Setup() { //init other variables from client_
  config_.ConfigureSelf(server_);
  NqServerConfigBase::Setup();
}
std::unique_ptr<QuicCryptoServerConfig> 
NqServerConfigCompat::NewCryptoConfig(QuicClock *clock) const {
#if !defined(DEBUG)
  if (server_.quic_secret == nullptr) {
    return nullptr; //should use original secret
  }
#endif
  auto c = std::unique_ptr<QuicCryptoServerConfig>(new QuicCryptoServerConfig(
    server_.quic_secret == nullptr ? kDefaultQuicSecret : server_.quic_secret, 
    QuicRandom::GetInstance(),
    std::unique_ptr<ProofSource>(new NqProofSource(addr_))
  ));
  std::unique_ptr<CryptoHandshakeMessage> scfg(
    c->AddDefaultConfig(
      QuicRandom::GetInstance(), 
      clock, 
      crypto_options_
    )
  );
  return c;
}
} // namespace net
#endif