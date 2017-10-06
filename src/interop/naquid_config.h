#pragma once

#include "net/quic/core/quic_config.h"

#include "naquid.h"
#include "interop/naquid_proof_verifier.h"

namespace net {

class NaquidClientConfig : public QuicConfig {
  nq_clconf_t client_;
  std::unique_ptr<ProofVerifier> proof_verifier_;
 public:
  NaquidClientConfig(const nq_clconf_t &conf) : QuicConfig(), client_(conf) {
  	//TODO(iyatomi): need to replace more formal verifier?
    proof_verifier_.reset(new NaquidProofVerifier);
  }
  const nq_clconf_t &client() const { return client_; }
  std::unique_ptr<ProofVerifier> proof_verifier() { return std::move(proof_verifier_); }
};


} //net