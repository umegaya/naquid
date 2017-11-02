#pragma once

#include <atomic>

#include "basis/defs.h"

namespace nq {
template <typename NUMBER>
class MsgIdFactory {
  std::atomic<NUMBER> seed_;
  //0xFFFFFF....
  static const NUMBER kLimit = 
    (((NUMBER)0x80) << (8 * (sizeof(NUMBER) - 1))) + 
    ((((NUMBER)0x80) << (8 * (sizeof(NUMBER) - 1))) - 100);
 public:
  MsgIdFactory() : seed_(0) {}

  NUMBER New() {
    while (true) {
      NUMBER expect = seed_;
      uint16_t desired = expect + 1;
      if (desired >= kLimit) {
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