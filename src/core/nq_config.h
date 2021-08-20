#pragma once

#include "core/compat/nq_config_compat.h"

namespace nq {
class NqClientConfig : public NqClientConfigCompat {
 public:
  NqClientConfig() : NqClientConfigCompat() {}
  NqClientConfig(const nq_clconf_t &conf) : NqClientConfigCompat(conf) {}
};
class NqServerConfig : public NqServerConfigCompat {
 public:
  NqServerConfig(const nq_addr_t &addr) : NqServerConfigCompat(addr) {}
  NqServerConfig(const nq_addr_t &addr, const nq_svconf_t &conf) : NqServerConfigCompat(addr, conf) {}
};
} // namespace nq
