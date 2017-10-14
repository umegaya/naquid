#pragma once

#include <string>

#include "net/quic/core/crypto/proof_verifier.h"

namespace net {
// TODO(iyatomi): because most of developer don't want heavy-wait proof verification, 
// need to implement minimum one (eg. allow self cert). 
// but Im not sure that such a light weight version is possible,
// if not possible I will change this to chromium one. 

// CAUTION: currently this proof verifier does not verify any proof. 
class NaquidProofVerifier : public ProofVerifier {
 public:
  NaquidProofVerifier() {}
  ~NaquidProofVerifier() override {}

  // ProofVerifier override.
  QuicAsyncStatus VerifyProof(
      const std::string& hostname,
      const uint16_t port,
      const std::string& server_config,
      QuicVersion quic_version,
      QuicStringPiece chlo_hash,
      const std::vector<std::string>& certs,
      const std::string& cert_sct,
      const std::string& signature,
      const ProofVerifyContext* context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* verify_details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return QUIC_SUCCESS;
  }

  QuicAsyncStatus VerifyCertChain(
      const std::string& hostname,
      const std::vector<std::string>& certs,
      const ProofVerifyContext* context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return QUIC_SUCCESS;
  }
};
} //net