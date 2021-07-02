#pragma once

#include <stack>
#include <vector>
#include <memory>

#include "memory.h"

#include "defs.h"

namespace nq {
struct EmptyBSS {
};
template <class E, class B> 
struct BlockTrait {
  STATIC_ASSERT((sizeof(E) % 8) == 0, "allocator target type should have 8 byte alignment");
  STATIC_ASSERT((sizeof(B) % 8) == 0, "allocator bss type should have 8 byte alignment");
  typedef struct BlockTag {
    alignas(E) char p[sizeof(E)];
    ManualConstructedOf<B> bss;
    inline void Init() { bss.Init(); }
    inline void Destroy() { bss.Destroy(); }
    static inline B *Bss(void *ptr) {
      return reinterpret_cast<B *>(reinterpret_cast<char *>(ptr) + sizeof(p)); 
    }
  } Block;
};
template <class E>
struct BlockTrait<E, EmptyBSS> {
  STATIC_ASSERT((sizeof(E) % 8) == 0, "allocator target type should have 8 byte alignment");
  typedef struct BlockTag {
    char p[sizeof(E)];
    inline void Init() {}
    inline void Destroy() {}
    static inline EmptyBSS *Bss(void *ptr) {
      return nullptr;   
    }
  } Block;  
};
// allocator that each block has 'BSS' section that never be destructed by freeing user region of the block
template <class E, class B = EmptyBSS>
class Allocator {
  typedef typename BlockTrait<E, B>::Block Block;
  //TODO(iyatoim): it may be faster by custom allocator
  std::vector<std::unique_ptr<Block[]>> chunks_;
  size_t total_block_, chunk_size_;
  std::stack<Block*> pool_;
 public:
  Allocator(size_t chunk_size) : 
    chunks_(), total_block_(chunk_size), chunk_size_(chunk_size), pool_() {
    ASSERT(chunk_size_ > 0);
    GrowChunk();
  }
  ~Allocator() {
    for (auto &c : chunks_) {
      auto pb = c.get();
      for (size_t i = 0; i < chunk_size_; i++) {
        pb[i].Destroy();
      }
    }
  }
  inline void *Alloc(std::size_t sz) {
    ASSERT(sz == sizeof(E));
    Block *block;
    if (pool_.size() <= 0) {
      GrowChunk();
    }
    ASSERT(pool_.size() > 0);
    block = pool_.top();
    pool_.pop();
    return block->p;
  }
  inline void Free(void *a) {
    pool_.push(reinterpret_cast<Block *>(a));
  }
  inline B *Bss(void *ptr) {
    return Block::Bss(ptr);
  }
 protected:
  inline void GrowChunk() {
    chunks_.emplace(chunks_.end(), new Block[chunk_size_]);
    auto pb = chunks_[chunks_.size() - 1].get();
    for (size_t i = 0; i < chunk_size_; i++) {
      auto ptr = pb + i;
      ptr->Init();
      pool_.push(ptr);
    }
  }
};
}
