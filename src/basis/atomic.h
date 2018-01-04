#pragma once
#if defined(_Atomic)
#include <stdatomic.h>
namespace nq {
	template <class T>
	struct atomic_trait {
		typedef atomic_ullong concrete_type;
	};
	template <>
	struct atomic_trait<int8_t> {
		typedef atomic_schar concrete_type;
	};
	template <>
	struct atomic_trait<uint8_t> {
		typedef atomic_uchar concrete_type;
	};
	template <>
	struct atomic_trait<int16_t> {
		typedef atomic_short concrete_type;
	};
	template <>
	struct atomic_trait<uint16_t> {
		typedef atomic_ushort concrete_type;
	};
	template <>
	struct atomic_trait<int32_t> {
		typedef atomic_int concrete_type;
	};
	template <>
	struct atomic_trait<uint32_t> {
		typedef atomic_uint concrete_type;
	};
	template <>
	struct atomic_trait<int64_t> {
		typedef atomic_llong concrete_type;
	};
	template <>
	struct atomic_trait<uint64_t> {
		typedef atomic_ullong concrete_type;
	};
	template <class T>
	using atomic = typename atomic_trait<T>::concrete_type;
}
#else
#include <atomic>
namespace nq {
	template <class T>
	using atomic = std::atomic<T>;
}
#endif
