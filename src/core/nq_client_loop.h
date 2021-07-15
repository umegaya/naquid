#pragma once

#include "core/compat/nq_client_loop_compat.h"

namespace net {
class NqClientLoop : public NqClientLoopCompat {
 public:
  NqClientLoop(int max_client_hint, int max_stream_hint) : 
    NqClientLoopCompat(max_client_hint, max_stream_hint) {}
  ~NqClientLoop() {}

  static inline NqClientLoop *FromHandle(nq_client_t cl) { return (NqClientLoop *)cl; }

  //family_pref specifies first lookup address family
  bool Resolve(int family_pref, const std::string &host, nq_on_resolve_host_t cb);
  bool Resolve(int family_pref, const std::string &host, int port, const nq_clconf_t *conf);

  NqClient *Create(const std::string &host, 
                   const NqQuicServerId server_id,
                   const NqQuicSocketAddress server_address,
                   NqClientConfig &config); 
};
} // net
