#include "core/nq_config.h"

#include "net/cert/ct_known_logs.h"
#include "net/cert/ct_log_verifier.h"

#include "basis/timespec.h"

namespace net {
void NqClientConfig::Setup() { //init other variables from client_
  if (client_.handshake_idle_timeout > 0) {
    set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromMicroseconds(nq::clock::to_us(client_.handshake_idle_timeout)));
    if (max_idle_time_before_crypto_handshake() > 
        max_time_before_crypto_handshake()) {
      set_max_time_before_crypto_handshake(
        max_idle_time_before_crypto_handshake());
    }
  }
  if (client_.handshake_timeout > 0) {
    set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromMicroseconds(nq::clock::to_us(client_.handshake_timeout)));
  }
}
std::unique_ptr<ProofVerifier> NqClientConfig::NewProofVerifier() const { 
  if (client_.insecure) {
    return std::unique_ptr<ProofVerifier>(new NqNoopProofVerifier);
  } else {
    return std::unique_ptr<ProofVerifier>(new NqProofVerifier);
  }
}
const char NqServerConfig::kDefaultQuicSecret[] = "e3f0f228cd0517a1a303ca983184f5ef";
void NqServerConfig::Setup() { //init other variables from client_
  if (server_.handshake_idle_timeout > 0) {
    set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromMicroseconds(nq::clock::to_us(server_.handshake_idle_timeout)));
    if (max_idle_time_before_crypto_handshake() > 
        max_time_before_crypto_handshake()) {
      set_max_time_before_crypto_handshake(
        max_idle_time_before_crypto_handshake());
    }
  }
  if (server_.handshake_timeout > 0) {
    set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromMicroseconds(nq::clock::to_us(server_.handshake_timeout)));
  }
}
} //net