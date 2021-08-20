#pragma once

#include "core/nq_loop.h"
#include "core/nq_config.h"

namespace nq {
class NqServerLoop : public NqLoop {
 public:
  NqServerLoop() {}
  ~NqServerLoop() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NqServerLoop);
};
}
