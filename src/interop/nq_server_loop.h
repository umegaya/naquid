#pragma once

#include "interop/naquid_loop.h"
#include "interop/naquid_config.h"

namespace net {
class NaquidServerLoop : public NaquidLoop {
 public:
  NaquidServerLoop() {}
  ~NaquidServerLoop() {}
};
}
