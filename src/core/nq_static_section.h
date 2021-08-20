#pragma once

namespace nq {
//statically allocated section which is related with object which is allocated with
//AllocatorWithBSS. this memory block keep on being alive even if related object freed.
class NqStaticSection {
  std::mutex mutex_;
 public:
  NqStaticSection() : mutex_() {}
  ~NqStaticSection() {}
  std::mutex &mutex() { return mutex_; }
};  
}