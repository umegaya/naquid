#pragma once

#include <string>

#include "net/quic/core/crypto/proof_verifier.h"
#include "net/quic/chromium/crypto/proof_verifier_chromium.h"

#include "basis/defs.h"

namespace nq {
namespace chromium {
using namespace net;
// TODO(iyatomi): because most of developer don't want heavy-wait proof verification (eg. certificate transparency), 
// need to implement minimum one (eg. allow self cert). 
// but Im not sure that such a light weight version is possible,
// if not possible I will change this to chromium one. 

// CAUTION: currently this proof verifier does not verify any proof. 
class NqNoopProofVerifier : public ProofVerifier {
 public:
  NqNoopProofVerifier() {}
  ~NqNoopProofVerifier() override {}

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
    //we will check signature and hostname, cert chain (without certificate transparency)
    return QUIC_SUCCESS;
  }

  QuicAsyncStatus VerifyCertChain(
      const std::string& hostname,
      const std::vector<std::string>& certs,
      const ProofVerifyContext* context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    ASSERT(false);
    return QUIC_SUCCESS;
  }
};
class NqProofVerifier : public ProofVerifier {
 public:
  NqProofVerifier() {}
  ~NqProofVerifier() override {}

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
      std::unique_ptr<ProofVerifierCallback> callback) override;

  QuicAsyncStatus VerifyCertChain(
      const std::string& hostname,
      const std::vector<std::string>& certs,
      const ProofVerifyContext* context,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override;

protected:
  scoped_refptr<X509Certificate> GetX509Certificate(
    const std::vector<std::string>& certs,
    std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* verify_details);

  bool VerifyCertChain(
    const std::string& hostname,
    scoped_refptr<X509Certificate> certificate,
    const ProofVerifyContext* context,
    std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* details);

  bool VerifySignature(const std::string& signed_data,
    QuicVersion quic_version,
    QuicStringPiece chlo_hash,
    const std::string& signature,
    const std::string& cert,
    scoped_refptr<X509Certificate> cert_chain);
};
} //namespace chromium
} //namespace nq
