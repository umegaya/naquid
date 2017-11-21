#pragma once

#include "nq.h"
#include "atomic.h"

#if !defined(TRACE)
	#include <string>
	#include <mutex>
	#if defined(DEBUG)
		extern std::mutex g_TRACE_mtx;
		static inline void TRACE(const std::string &fmt) {
			std::unique_lock<std::mutex> lock(g_TRACE_mtx);
			fprintf(stderr, "%s", fmt.c_str());
			fprintf(stderr, "\n");
		}
		template<class... Args>
		static inline void TRACE(const std::string &fmt, const Args... args) {
			std::unique_lock<std::mutex> lock(g_TRACE_mtx);
			fprintf(stderr, fmt.c_str(), args...);
			fprintf(stderr, "\n");
		}
	#else
		#define TRACE(...) //fprintf(stderr, __VA_ARGS__)
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

#if !defined(STATIC_ASSERT)
#include <type_traits>
#define STATIC_ASSERT static_assert 
#endif

namespace nq {

#if defined(__ENABLE_EPOLL__) || defined(__ENABLE_KQUEUE__)
	typedef int Fd;
	constexpr Fd INVALID_FD = -1;	
#elif defined(__ENABLE_IOCP__)
//TODO(iyatomi): windows definition
#else
#endif

}
