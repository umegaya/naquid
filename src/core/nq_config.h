#pragma once

#include "net/quic/core/quic_config.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"

#include "nq.h"
#include "basis/timespec.h"
#include "core/nq_proof_verifier.h"
#include "core/nq_proof_source.h"

namespace net {

class NqConfig : public QuicConfig {
 protected:
  template <class CONF>
  void ConfigureSelf(const CONF &c) {
    auto idle_timeout = c.idle_timeout;
    if (idle_timeout <= 0) {
      idle_timeout = nq_time_sec(5);
    }
    auto idle_timeout_us = nq::clock::to_us(c.idle_timeout);
    SetIdleNetworkTimeout(
      QuicTime::Delta::FromMicroseconds(idle_timeout_us),
      QuicTime::Delta::FromMicroseconds(idle_timeout_us));

    set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromMicroseconds(nq::clock::to_us(c.idle_timeout)));
    if (max_idle_time_before_crypto_handshake() > max_time_before_crypto_handshake()) {
      set_max_time_before_crypto_handshake(
        max_idle_time_before_crypto_handshake());
    }
    auto handshake_timeout_us = nq::clock::to_us(c.handshake_timeout);
    if (handshake_timeout_us > max_time_before_crypto_handshake().ToMicroseconds()) {
      set_max_time_before_crypto_handshake(
        QuicTime::Delta::FromMicroseconds(handshake_timeout_us));
    }
  }
};

class NqClientConfig : public NqConfig {
 protected:
  nq_clconf_t client_;
 public:
  NqClientConfig(const nq_clconf_t &conf) : NqConfig(), client_(conf) {
    Setup();
  }
  void Setup(); //init other variables from client_
  const nq_clconf_t &client() const { return client_; }
  std::unique_ptr<ProofVerifier> NewProofVerifier() const;
};

class NqServerConfig : public NqConfig {
 protected:
  nq_addr_t addr_;
  nq_svconf_t server_;
  QuicCryptoServerConfig::ConfigOptions crypto_options_;
  static const char kDefaultQuicSecret[];
 public:
  NqServerConfig(const nq_addr_t &addr) : 
    NqConfig(), addr_(addr), crypto_options_() {
    memset(&server_, 0, sizeof(server_));
    nq_closure_init(server_.on_open, on_conn_open, NoopOnOpen, nullptr);
    nq_closure_init(server_.on_close, on_conn_close, NoopOnClose, nullptr);
    Setup();
  }
  NqServerConfig(const nq_addr_t &addr, const nq_svconf_t &conf) : 
    NqConfig(), addr_(addr), server_(conf), crypto_options_() {
    Setup();
  }
  void Setup(); //init other variables from server_
  const nq_svconf_t &server() const { return server_; }
  std::unique_ptr<QuicCryptoServerConfig> NewCryptoConfig(QuicClock *clock) const;
 protected:
  static bool NoopOnOpen(void *, nq_conn_t, nq_handshake_event_t, void *) { return false; }
  static nq_time_t NoopOnClose(void *, nq_conn_t, nq_result_t, const char*, bool) { return 0; }
};

} //net
