#pragma once

#include "interop/nq_loop.h"
#include "interop/nq_config.h"

namespace net {
class NqServerLoop : public NqLoop {
 public:
  NqServerLoop() {}
  ~NqServerLoop() {}
};
}
