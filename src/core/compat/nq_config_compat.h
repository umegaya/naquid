#pragma once

#include "core/nq_config_base.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "core/compat/chromium/nq_proof_verifier.h"
#include "core/compat/chromium/nq_proof_source.h"

#include "net/quic/core/quic_config.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"

namespace nq {
using namespace net;
class NqQuicConfig : public QuicConfig {
 public:
  template <class CONF>
  void ConfigureSelf(const CONF &c) {
    auto idle_timeout = c.idle_timeout;
    if (idle_timeout <= 0) {
      idle_timeout = nq_time_sec(5);
    }
    auto idle_timeout_us = clock::to_us(c.idle_timeout);
    SetIdleNetworkTimeout(
      QuicTime::Delta::FromMicroseconds(idle_timeout_us),
      QuicTime::Delta::FromMicroseconds(idle_timeout_us));

    set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromMicroseconds(clock::to_us(c.idle_timeout)));
    if (max_idle_time_before_crypto_handshake() > max_time_before_crypto_handshake()) {
      set_max_time_before_crypto_handshake(
        max_idle_time_before_crypto_handshake());
    }
    auto handshake_timeout_us = clock::to_us(c.handshake_timeout);
    if (handshake_timeout_us > 
      static_cast<uint64_t>(max_time_before_crypto_handshake().ToMicroseconds())) {
      set_max_time_before_crypto_handshake(
        QuicTime::Delta::FromMicroseconds(handshake_timeout_us));
    }
  }
};

class NqClientConfigCompat : public NqClientConfigBase {
 public:
  NqClientConfigCompat() : NqClientConfigBase(), config_() {}
  NqClientConfigCompat(const nq_clconf_t &conf) : NqClientConfigBase(conf), config_() {}
  // get/set
  const QuicConfig &chromium() const { return config_; }
  // operation
  void Setup() override;
  std::unique_ptr<ProofVerifier> NewProofVerifier() const;
 protected:
  NqQuicConfig config_;
};

class NqServerConfigCompat : public NqServerConfigBase {
 public:
  NqServerConfigCompat(const nq_addr_t &addr) : 
    NqServerConfigBase(addr), config_(), crypto_options_() {}
  NqServerConfigCompat(const nq_addr_t &addr, const nq_svconf_t &conf) : 
    NqServerConfigBase(addr, conf), config_(), crypto_options_() {}
  // get/set
  const QuicConfig &chromium() const { return config_; }
  // operation
  void Setup() override;
  std::unique_ptr<QuicCryptoServerConfig> NewCryptoConfig(QuicClock *clock) const;
 protected:
  NqQuicConfig config_;
  QuicCryptoServerConfig::ConfigOptions crypto_options_;
};

} //namespace nq
#else
#include "basis/syscall.h"
#include "core/compat/quiche/deps.h"

namespace nq {
class NqClientConfigCompat;
class NqServerConfigCompat;
class NqQuicConfig {
 public:
  NqQuicConfig() : config_(nullptr) {}
  ~NqQuicConfig() {
    if (config_ != nullptr) {
      quiche_config_free(config_);
      config_ = nullptr;
    }
  }
  //cast operator
  operator quiche_config *() { return config_; }

  //operation
  bool Initialize(const NqProtocolManager::Protocol p) {
    config_ = quiche_config_new(p);
    if (config_ == nullptr) {
      return false;
    }
    return true;
  }
  template <class CONF>
  void CommonSetup(const CONF &config) {
    quiche_config_set_max_recv_udp_payload_size(config_, Syscall::kDefaultSocketReceiveBuffer);
    quiche_config_set_max_send_udp_payload_size(config_, Syscall::kDefaultSocketReceiveBuffer);
    auto idle_timeout = config.idle_timeout;
    if (idle_timeout <= 0) {
      idle_timeout = nq_time_sec(5);
    }
    if (config.handshake_timeout > 0) {
      if (config.handshake_timeout > idle_timeout) {
        logger::warn({
          {"msg", "handshake_timeout does not support for quiche"},
          {"handshake_timeout", config.handshake_timeout},
          {"idle_timeout", idle_timeout}
        });
      }
    }
    quiche_config_set_max_idle_timeout(config_, idle_timeout);
  }
  void Setup(const nq_clconf_t &config) {
    CommonSetup(config);
    quiche_config_verify_peer(config_, !config.insecure);
  }
  void Setup(const nq_svconf_t &config) {
    CommonSetup(config);
  }

  //factory
  static NqQuicConfig *Create(const NqClientConfigCompat &config);
  static NqQuicConfig *Create(const NqServerConfigCompat &config);
 private:
  quiche_config *config_;
};
class NqClientConfigCompat : public NqClientConfigBase {
 public:
  NqClientConfigCompat() : NqClientConfigBase() {}
  NqClientConfigCompat(const nq_clconf_t &conf) : NqClientConfigBase(conf) {}
  //operation
  std::unique_ptr<NqQuicConfig> NewQuicConfig() const {
    return std::unique_ptr<NqQuicConfig>(NqQuicConfig::Create(*this));
  }
};
class NqServerConfigCompat : public NqServerConfigBase {
 public:
  NqServerConfigCompat(const nq_addr_t &addr) : 
    NqServerConfigBase(addr) {}
  NqServerConfigCompat(const nq_addr_t &addr, const nq_svconf_t &conf) : 
    NqServerConfigBase(addr, conf) {}
  //operation
  std::unique_ptr<NqQuicConfig> NewQuicConfig() const {
    return std::unique_ptr<NqQuicConfig>(NqQuicConfig::Create(*this));
  }
};
} //namespace nq
#endif
