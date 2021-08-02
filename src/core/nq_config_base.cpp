#include "core/nq_config.h"

namespace net {
void NqClientConfigBase::Setup() {}


const char NqServerConfigBase::kDefaultQuicSecret[] = "e3f0f228cd0517a1a303ca983184f5ef";
void NqServerConfigBase::Setup() {
  if (server_.shutdown_timeout <= 0) {
    server_.shutdown_timeout = nq_time_sec(5);
  }
}
} //net