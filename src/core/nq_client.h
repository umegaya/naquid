#pragma once

#include "core/compat/nq_client_compat.h"

namespace net {
class NqClient : public NqClientCompat {
 public:
  NqClient(NqQuicSocketAddress server_address,
               NqClientLoop &loop,
               const NqQuicServerId& server_id,
               const NqClientConfig &config)
    : NqClientCompat(server_address, loop, server_id, config) {}
  ~NqClient() override {}

  //implement custom allocator
  void* operator new(std::size_t sz);
  void* operator new(std::size_t sz, NqClientLoop *l);
  void operator delete(void *p) noexcept;
  void operator delete(void *p, NqClientLoop *l) noexcept;
};
} //net
