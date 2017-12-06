#pragma once

#include <stack>

#include "base/memory/manual_constructor.h"

#include "defs.h"

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
template <class E, class B>
class AllocatorWithBSS {
  STATIC_ASSERT((sizeof(E) % 8) == 0, "allocator target type should have 8 byte alignment");
  STATIC_ASSERT((sizeof(B) % 8) == 0, "allocator bss type should have 8 byte alignment");
  typedef struct {
    char p[sizeof(E)];
    base::ManualConstructor<B> bss;
  } Chunk;
  //TODO(iyatoim): it may be more fast by custom allocator
  std::vector<Chunk> pool_;
  std::stack<size_t> empty_;
 public:
  AllocatorWithBSS(size_t n = 0) : pool_(n), empty_() {
    for (int i = n-1; i >= 0; i--) {
      empty_.push(i);
      pool_[i].bss.Init();
    }
  }
  ~AllocatorWithBSS() {
    for (int i = pool_.size()-1; i >= 0; i--) {
      pool_[i].bss.Destroy();
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
      pool_[idx].bss.Init();
    }
    return pool_[idx].p;
  }
  B *BSS(void *ptr) {
    return reinterpret_cast<B *>(reinterpret_cast<char *>(ptr) + sizeof(E));
  }
  void Free(void *a) {
    size_t idx = 
      reinterpret_cast<Chunk*>(a) - 
      reinterpret_cast<Chunk*>(pool_.data());
    empty_.push(idx);
  }
};
}
