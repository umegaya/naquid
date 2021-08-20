#include "core/compat/chromium/nq_proof_verifier.h"

#include "crypto/signature_verifier.h"
#include "net/cert/asn1_util.h"
#include "net/quic/core/crypto/crypto_protocol.h"

namespace nq {
namespace chromium {
using namespace net;
class NqProofVerifyDetails : public ProofVerifyDetails {
 public:
  ProofVerifyDetails* Clone() const override {
    return new NqProofVerifyDetails(error_line_);
  }
  NqProofVerifyDetails(int error_line) : error_line_(error_line) {}
 private:
    int error_line_;
};
#define PUT_VERIFY_DETAIL(vd) (vd->reset(new NqProofVerifyDetails(__LINE__)))
scoped_refptr<X509Certificate> NqProofVerifier::GetX509Certificate(
    const std::vector<std::string>& certs,
    std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* verify_details) {
  // variable which stores result
  scoped_refptr<X509Certificate> cert;
  if (certs.empty()) {
    *error_details = "Failed to create certificate chain. Certs are empty.";
    DLOG(WARNING) << *error_details;
    PUT_VERIFY_DETAIL(verify_details);
    return cert;
  }

  // Convert certs to X509Certificate.
  std::vector<QuicStringPiece> cert_pieces(certs.size());
  for (unsigned i = 0; i < certs.size(); i++) {
    cert_pieces[i] = QuicStringPiece(certs[i]);
  }
  cert = X509Certificate::CreateFromDERCertChain(cert_pieces);
  if (!cert.get()) {
    *error_details = "Failed to create certificate chain";
    DLOG(WARNING) << *error_details;
    PUT_VERIFY_DETAIL(verify_details);
  }
  return cert;
}
bool NqProofVerifier::VerifySignature(const std::string& signed_data,
                                      QuicVersion quic_version,
                                      QuicStringPiece chlo_hash,
                                      const std::string& signature,
                                      const std::string& cert,
                                      scoped_refptr<X509Certificate> cert_chain) {
  QuicStringPiece spki;
  if (!asn1::ExtractSPKIFromDERCert(cert, &spki)) {
    DLOG(WARNING) << "ExtractSPKIFromDERCert failed";
    return false;
  }

  crypto::SignatureVerifier verifier;

  size_t size_bits;
  X509Certificate::PublicKeyType type;
  X509Certificate::GetPublicKeyInfo(cert_chain->os_cert_handle(), &size_bits, &type);
  if (type == X509Certificate::kPublicKeyTypeRSA) {
    crypto::SignatureVerifier::HashAlgorithm hash_alg =
        crypto::SignatureVerifier::SHA256;
    crypto::SignatureVerifier::HashAlgorithm mask_hash_alg = hash_alg;
    unsigned int hash_len = 32;  // 32 is the length of a SHA-256 hash.

    bool ok = verifier.VerifyInitRSAPSS(
        hash_alg, mask_hash_alg, hash_len,
        reinterpret_cast<const uint8_t*>(signature.data()), signature.size(),
        reinterpret_cast<const uint8_t*>(spki.data()), spki.size());
    if (!ok) {
      DLOG(WARNING) << "VerifyInitRSAPSS failed";
      return false;
    }
  } else if (type == X509Certificate::kPublicKeyTypeECDSA) {
    if (!verifier.VerifyInit(crypto::SignatureVerifier::ECDSA_SHA256,
                             reinterpret_cast<const uint8_t*>(signature.data()),
                             signature.size(),
                             reinterpret_cast<const uint8_t*>(spki.data()),
                             spki.size())) {
      DLOG(WARNING) << "VerifyInit failed";
      return false;
    }
  } else {
    LOG(ERROR) << "Unsupported public key type " << type;
    return false;
  }

  verifier.VerifyUpdate(reinterpret_cast<const uint8_t*>(kProofSignatureLabel),
                        sizeof(kProofSignatureLabel));
  uint32_t len = chlo_hash.length();
  verifier.VerifyUpdate(reinterpret_cast<const uint8_t*>(&len), sizeof(len));
  verifier.VerifyUpdate(reinterpret_cast<const uint8_t*>(chlo_hash.data()),
                        len);

  verifier.VerifyUpdate(reinterpret_cast<const uint8_t*>(signed_data.data()),
                        signed_data.size());

  if (!verifier.VerifyFinal()) {
    DLOG(WARNING) << "VerifyFinal failed";
    return false;
  }

  DVLOG(1) << "VerifyFinal success";
  return true;
}
bool NqProofVerifier::VerifyCertChain(
  const std::string& hostname,
  scoped_refptr<X509Certificate> cert_chain,
  const ProofVerifyContext* context,
  std::string* error_details,
  std::unique_ptr<ProofVerifyDetails>* verify_details) {
  //check hostname
  if (!cert_chain->VerifyNameMatch(hostname, true)) {
    *error_details = std::string("Failed to verify hostname") + ":" + hostname;
    DLOG(WARNING) << *error_details;
    PUT_VERIFY_DETAIL(verify_details);
    return false;
  }
  //verify cert chain
  return true;
}

// ProofVerifier override.
QuicAsyncStatus NqProofVerifier::VerifyProof(
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
    std::unique_ptr<ProofVerifierCallback> callback) {
  //we will check signature and hostname, cert chain (without certificate transparency)
  DCHECK(error_details);
  DCHECK(verify_details);
  DCHECK(callback);

  error_details->clear();

  // Converts |certs| to |cert|.
  auto cert_chain = GetX509Certificate(certs, error_details, verify_details);
  if (!cert_chain.get()) {
    return QUIC_FAILURE;
  }

  // omit certificate transparency check, which seems to use chromium job
  // TODO(iyatomi): recover check by preparing ioProcessor for it

  // We call VerifySignature first to avoid copying of server_config and
  // signature.
  if (!signature.empty() &&
      !VerifySignature(server_config, quic_version, chlo_hash, signature,
                       certs[0], cert_chain)) {
    *error_details = "Failed to verify signature of server config";
    DLOG(WARNING) << *error_details;
    PUT_VERIFY_DETAIL(verify_details);
    return QUIC_FAILURE;
  }
  if (!VerifyCertChain(hostname, cert_chain, context, error_details, verify_details)) {
    return QUIC_FAILURE;
  }
  return QUIC_SUCCESS;
}

QuicAsyncStatus NqProofVerifier::VerifyCertChain(
    const std::string& hostname,
    const std::vector<std::string>& certs,
    const ProofVerifyContext* context,
    std::string* error_details,
    std::unique_ptr<ProofVerifyDetails>* verify_details,
    std::unique_ptr<ProofVerifierCallback> callback) {
  // Converts |certs| to |cert|.
  auto cert_chain = GetX509Certificate(certs, error_details, verify_details);
  if (!cert_chain.get()) {
    *error_details = "Invalid cert chain";
    DLOG(WARNING) << *error_details;
    PUT_VERIFY_DETAIL(verify_details);
    return QUIC_FAILURE;
  }
  if (!VerifyCertChain(hostname, cert_chain, context, error_details, verify_details)) {
    //details set inside above func
    return QUIC_FAILURE; 
  }
  return QUIC_SUCCESS;
}
} //namespace chromium 
} //namespace nq
