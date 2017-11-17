#include "core/nq_config.h"

#include "net/cert/ct_known_logs.h"
#include "net/cert/ct_log_verifier.h"

namespace net {
void NqClientConfig::Setup() { //init other variables from client_
  ConfigureSelf(client_);
}
std::unique_ptr<ProofVerifier> NqClientConfig::NewProofVerifier() const { 
  if (client_.insecure) {
    return std::unique_ptr<ProofVerifier>(new NqNoopProofVerifier);
  } else {
    return std::unique_ptr<ProofVerifier>(new NqProofVerifier);
  }
}



const char NqServerConfig::kDefaultQuicSecret[] = "e3f0f228cd0517a1a303ca983184f5ef";
void NqServerConfig::Setup() { //init other variables from client_
  ConfigureSelf(server_);
}
std::unique_ptr<QuicCryptoServerConfig> 
NqServerConfig::NewCryptoConfig(QuicClock *clock) const {
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
} //net