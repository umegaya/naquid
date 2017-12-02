#pragma once

#include "defs.h"
#include <stack>

namespace nq {
template <class E>
class Allocator {
  STATIC_ASSERT((sizeof(E) % 8) == 0, "allocator target type should have 8 byte alignment");
  typedef struct {
    char p[sizeof(E)];
  } Chunk;
  //TODO(iyatoim): it may be more fast by custom allocator
  std::vector<Chunk> pool_;
  std::stack<size_t> empty_;
 public:
  Allocator(size_t n = 0) : pool_(n), empty_() {
    for (int i = n-1; i >= 0; i--) {
      empty_.push(i);
    }
  }
  void *Alloc(std::size_t sz) {
    ASSERT(sz == sizeof(E));
    size_t idx;
    if (empty_.size() > 0) {
      idx = empty_.top();
      empty_.pop();
    } else {
      idx = pool_.size();
      pool_.emplace(pool_.end());
    }
    return pool_[idx].p;
  }
  void Free(void *a) {
    size_t idx = 
      reinterpret_cast<Chunk*>(a) - 
      reinterpret_cast<Chunk*>(pool_.data());
    empty_.push(idx);
  }
};
}
