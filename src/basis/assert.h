#pragma once

#if !defined(ASSERT)
  #if !defined(NDEBUG)
    #include <assert.h>
    #define ASSERT(cond) assert((cond))
  #else
    #define ASSERT(cond)
  #endif
#endif

#if !defined(STATIC_ASSERT)
#include <type_traits>
#define STATIC_ASSERT static_assert 
#endif
