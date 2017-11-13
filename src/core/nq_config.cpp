#include "core/nq_config.h"

#include "net/cert/ct_known_logs.h"
#include "net/cert/ct_log_verifier.h"

namespace net {
void NqClientConfig::Setup() { //init other variables from client_
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
}
} //net