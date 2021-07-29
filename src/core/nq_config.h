#pragma once

#include "net/quic/core/quic_config.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"

#include "nq.h"
#include "basis/timespec.h"
#include "core/nq_proof_verifier.h"
#include "core/nq_proof_source.h"
#include "core/compat/nq_protocol_manager.h"

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
    if (handshake_timeout_us > 
      static_cast<uint64_t>(max_time_before_crypto_handshake().ToMicroseconds())) {
      set_max_time_before_crypto_handshake(
        QuicTime::Delta::FromMicroseconds(handshake_timeout_us));
    }
  }
};

class NqClientConfig : public NqConfig {
 protected:
  nq_clconf_t client_;
 public:
  NqClientConfig() : NqConfig() { memset(&client_, 0, sizeof(client_)); }
  NqClientConfig(const nq_clconf_t &conf) : NqConfig(), client_(conf) {
    Setup();
  }
  void Setup(); //init other variables from client_
  NqProtocolManager protocol_manager() const { return NqProtocolManager(client_.protocol); }
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
    nq_closure_init(server_.on_open, NoopOnOpen, nullptr);
    nq_closure_init(server_.on_close, NoopOnClose, nullptr);
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
  static void NoopOnOpen(void *, nq_conn_t, void **) {}
  static void NoopOnClose(void *, nq_conn_t, nq_error_t, const nq_error_detail_t*, bool) {}
};

} //net
