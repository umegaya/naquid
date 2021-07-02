#include "memory.h"
#include "assert.h"
#include "logger.h"

#if defined(NQ_ANDROID)
#include <malloc.h>
#endif

namespace base {

// borrowed from chromium source 'base/memory/aligned_memory.cpp'
void* AlignedAlloc(size_t size, size_t alignment) {
  ASSERT(size > 0U);
  ASSERT(alignment & (alignment - 1) == 0U);
  ASSERT(alignment % sizeof(void*) == 0U);
  void* ptr = nullptr;
#if defined(OS_WIN)
  ptr = _aligned_malloc(size, alignment);
  // 2021/6/25 iyatomi: __ANDROID_API__ >= 16 seems to provide posix_memalign
  // https://stackoverflow.com/questions/44852378/android-ndk-r15b-posix-memalign-undeclared-identifier
#elif defined(OS_ANDROID) && __ANDROID_API__ < 16
  // Android technically supports posix_memalign(), but does not expose it in
  // the current version of the library headers used by Chrome.  Luckily,
  // memalign() on Android returns pointers which can safely be used with
  // free(), so we can use it instead.  Issue filed to document this:
  // http://code.google.com/p/android/issues/detail?id=35391
  ptr = memalign(alignment, size);
#else
  if (posix_memalign(&ptr, alignment, size))
    ptr = NULL;
#endif
  // Since aligned allocations may fail for non-memory related reasons, force a
  // crash if we encounter a failed allocation; maintaining consistent behavior
  // with a normal allocation failure in Chrome.
  if (!ptr) {
    TRACE({
        {"msg", "If you crashed here, your aligned allocation is incorrect"},
        {"size", size},
        {"alignment", alignment}
    });
    ASSERT(false);
  }
  // Sanity check alignment just to be safe.
  ASSERT(reinterpret_cast<uintptr_t>(ptr) & (alignment - 1) == 0U);
  return ptr;
}

}  // namespace base
