#pragma once

#include "core/compat/nq_dispatcher_compat.h"

namespace net {
class NqDispatcher : public NqDispatcherCompat {
 public:
  NqDispatcher(int port, const NqServerConfig& config, NqWorker &worker) : NqDispatcherCompat(port, config, worker) {}
};
} // namespace net
