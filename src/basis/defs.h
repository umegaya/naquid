#pragma once

#include "nq.h"

#if !defined(TRACE)
	#if defined(DEBUG)
		#define TRACE(...) fprintf(stderr, __VA_ARGS__)
	#else
		#define TRACE(...)
	#endif
#endif

#if !defined(ASSERT)
	#if !defined(NDEBUG)
		#include <assert.h>
		#define ASSERT(cond) assert(cond)
	#else
		#define ASSERT(cond)
	#endif
#endif

#if defined(__ENABLE_EPOLL__) || defined(__ENABLE_KQUEUE__)
namespace nq {
	typedef int Fd;
	constexpr Fd INVALID_FD = -1;	
}
#else
//TODO: windows definition?
#endif
