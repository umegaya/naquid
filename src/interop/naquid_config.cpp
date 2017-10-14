#include "interop/naquid_config.h"

namespace net {
void NaquidClientConfig::Setup() { //init other variables from client_
    //TODO(iyatomi): need to replace more formal verifier?
    proof_verifier_.reset(new NaquidProofVerifier);
}
const char NaquidServerConfig::kDefaultQuicSecret[] = "e3f0f228cd0517a1a303ca983184f5ef";
void NaquidServerConfig::Setup() { //init other variables from client_
}
} //net