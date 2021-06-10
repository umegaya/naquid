#pragma once
#include <stdlib.h>
#include <type_traits>

namespace nq {
namespace convert {
static void *ref_decl = nullptr;

//specialized converters
template <
  class T,
  typename std::enable_if<std::is_integral<T>::value >::type*& = ref_decl
>
static bool Conv(const char *src, T *dst) {
  char *buf;
  uint64_t tmp = strtoll(src, &buf, 10);
  if ((*buf) == 0) {
    *dst = (T)tmp;
    return true;
  }
  return false;  
}
template <class T>
static bool Conv(const std::string &src, T *dst) {
  return Conv<T>(src.c_str(), dst);
}


//wrapper
//try to convert. if error, returns error and dst not initialized.
template <class F, class T>
static bool Try(F src, T *dst) {
  return Conv(src, dst);
}
//do convert, if error, dst will be initialized with fallback.
template <class F, class T>
static T Do(F src, T fallback) {
  T tmp;
  if (Conv(src, &tmp)) {
    return tmp;
  } else {
    return fallback;
  }
}
} //namespace convert
} //namespace nq
