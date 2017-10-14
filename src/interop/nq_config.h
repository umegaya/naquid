#pragma once

#include "net/quic/core/quic_config.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"

#include "naquid.h"
#include "interop/naquid_proof_verifier.h"
#include "interop/naquid_proof_source.h"

namespace net {

class NaquidClientConfig : public QuicConfig {
 protected:
  nq_clconf_t client_;
  std::unique_ptr<ProofVerifier> proof_verifier_;
 public:
  NaquidClientConfig(const nq_clconf_t &conf) : QuicConfig(), client_(conf) {
    Setup();
  }
  void Setup(); //init other variables from client_
  const nq_clconf_t &client() const { return client_; }
  std::unique_ptr<ProofVerifier> proof_verifier() { return std::move(proof_verifier_); }
};

class NaquidServerConfig : public QuicConfig {
 protected:
  nq_svconf_t server_;
  QuicCryptoServerConfig crypto_;
  static const char kDefaultQuicSecret[];
 public:
  NaquidServerConfig(const nq_addr_t &addr) : QuicConfig(), 
    crypto_(kDefaultQuicSecret, 
      QuicRandom::GetInstance(), 
      std::unique_ptr<ProofSource>(new NaquidProofSource(addr))
    ) {
    memset(&server_, 0, sizeof(server_));
    nq_closure_init(server_.on_open, on_conn_open, NoopOnOpen, nullptr);
    nq_closure_init(server_.on_close, on_conn_close, NoopOnClose, nullptr);
    Setup();
  }
  NaquidServerConfig(const nq_addr_t &addr, const nq_svconf_t &conf) : 
    QuicConfig(), server_(conf), 
    crypto_(
      conf.quic_secret == nullptr ? kDefaultQuicSecret : conf.quic_secret, 
      QuicRandom::GetInstance(),
      std::unique_ptr<ProofSource>(new NaquidProofSource(addr))
    ) {
    Setup();
  }
  void Setup(); //init other variables from server_
  const nq_svconf_t &server() const { return server_; }
  const QuicCryptoServerConfig *crypto() const { return &crypto_; }
 protected:
  static bool NoopOnOpen(void *, nq_conn_t) { return false; }
  static nq_time_t NoopOnClose(void *, nq_conn_t, nq_result_t, const char*, bool) { return 0; }
};

} //net
