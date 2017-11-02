#pragma once 

namespace net {
template <class S>
class NqObjectAllocator {
  std::stack<std::unique_ptr<S>> pool_;
 public:
  NqObjectAllocator() {}
  template <class... Args>
  inline void New(Args... args) {
    if (pool.size() > 0) {
      auto p = pool.pop();
      return new (p.release()) S(args...);
    } else {
      return new S(boxer_, args...);
    }
  }
  inline void Delete(S *s) {
    s->~S();
    pool_.push(s);
  }
};
}