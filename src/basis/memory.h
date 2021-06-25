#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

namespace nq {

// borrowed from chromium source 'base/memory/aligned_memory.h'
export void* AlignedAlloc(size_t size, size_t alignment);

inline void AlignedFree(void* ptr) {
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

// Deleter for use with smart pointers
//   eg. std::unique_ptr<Foo, base::AlignedMemoryDeleter> foo;
struct AlignedMemoryDeleter {
  inline void operator()(void* ptr) const {
    AlignedFree(ptr);
  }
};

// borrowed from chromium source 'base/memory/manual_constructor.h'
template <typename Type>
class ManualConstructedOf {
public:
  static void* operator new[](size_t size) {
    return AlignedAlloc(size, alignof(Type));
  }
  static void operator delete[](void* mem) {
    AlignedFree(mem);
  }

  inline Type* get() { return reinterpret_cast<Type*>(space_); }
  inline const Type* get() const  {
    return reinterpret_cast<const Type*>(space_);
  }

  inline Type* operator->() { return get(); }
  inline const Type* operator->() const { return get(); }

  inline Type& operator*() { return *get(); }
  inline const Type& operator*() const { return *get(); }

  template <typename... Ts>
  inline void Init(Ts&&... params) {
    new (space_) Type(std::forward<Ts>(params)...);
  }

  inline void InitFromMove(ManualConstructedOf<Type>&& o) {
    Init(std::move(*o));
  }

  inline void Destroy() {
    get()->~Type();
  }

 private:
  alignas(Type) char space_[sizeof(Type)];
};

}  // namespace nq
