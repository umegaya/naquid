#pragma once
#if defined(_Atomic)
#include <stdatomic.h>
namespace nq {
	typedef std::atomic_int atm_int_t;
	typedef std::atomic_ullong atm_uint64_t;
}
#else
#include <atomic>
namespace nq {
	typedef std::atomic<int> atm_int_t;
	typedef std::atomic<unsigned long long> atm_uint64_t;
}
#endif
