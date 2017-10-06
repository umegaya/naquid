#pragma once

#include <atomic>

#include "core/defs.h"

namespace nq {
class MsgIdFactory {
  std::atomic<uint16_t> seed_;
 public:
  MsgIdFactory() : seed_(0) {}

  nq_msgid_t New() {
    while (true) {
      uint16_t expect = seed_;
      uint16_t desired = expect + 1;
      if (desired >= 65000) {
        desired = 1;
      }
      if (atomic_compare_exchange_weak(&seed_, &expect, desired)) {
        return desired;
      }
	}
    ASSERT(false);
    return 0;		
  }
};
}