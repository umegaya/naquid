#pragma once

#include "core/nq_loop.h"
#include "core/nq_config.h"

namespace net {
class NqServerLoop : public NqLoop {
 public:
  NqServerLoop() {}
  ~NqServerLoop() {}
};
}
