#include "core/nq_config.h"

namespace net {
void NqClientConfig::Setup() { //init other variables from client_
    //TODO(iyatomi): need to replace more formal verifier?
    proof_verifier_.reset(new NqProofVerifier);
}
const char NqServerConfig::kDefaultQuicSecret[] = "e3f0f228cd0517a1a303ca983184f5ef";
void NqServerConfig::Setup() { //init other variables from client_
}
} //net