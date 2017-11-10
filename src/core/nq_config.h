#pragma once

#include "net/quic/core/quic_config.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"

#include "nq.h"
#include "core/nq_proof_verifier.h"
#include "core/nq_proof_source.h"

namespace net {

class NqClientConfig : public QuicConfig {
 protected:
  nq_clconf_t client_;
 public:
  NqClientConfig(const nq_clconf_t &conf) : QuicConfig(), client_(conf) {
    Setup();
  }
  void Setup(); //init other variables from client_
  const nq_clconf_t &client() const { return client_; }
  std::unique_ptr<ProofVerifier> NewProofVerifier() const { 
    return std::unique_ptr<ProofVerifier>(new NqProofVerifier); 
  }
};

class NqServerConfig : public QuicConfig {
 protected:
  nq_addr_t addr_;
  nq_svconf_t server_;
  QuicCryptoServerConfig::ConfigOptions crypto_options_;
  static const char kDefaultQuicSecret[];
 public:
  NqServerConfig(const nq_addr_t &addr) : 
    QuicConfig(), addr_(addr), crypto_options_() {
    memset(&server_, 0, sizeof(server_));
    nq_closure_init(server_.on_open, on_conn_open, NoopOnOpen, nullptr);
    nq_closure_init(server_.on_close, on_conn_close, NoopOnClose, nullptr);
    Setup();
  }
  NqServerConfig(const nq_addr_t &addr, const nq_svconf_t &conf) : 
    QuicConfig(), addr_(addr), server_(conf), crypto_options_() {
    Setup();
  }
  void Setup(); //init other variables from server_
  const nq_svconf_t &server() const { return server_; }
  std::unique_ptr<QuicCryptoServerConfig> NewCryptoConfig(QuicClock *clock) const {
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
 protected:
  static bool NoopOnOpen(void *, nq_conn_t, nq_handshake_event_t, void *) { return false; }
  static nq_time_t NoopOnClose(void *, nq_conn_t, nq_result_t, const char*, bool) { return 0; }
};

} //net
