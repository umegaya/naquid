#pragma once

#include "nq.h"

namespace nq {
	class LengthCodec {
	public:
		static inline nq_size_t Encode(nq_size_t sz_encode, char *buf, nq_size_t bufsz) {
			nq_size_t idx = 0;
			while (bufsz > idx) {
				buf[idx] = sz_encode & 0x7f;
				sz_encode >>= 7;
				if (sz_encode > 0) {
					idx++;
				} else {
					buf[idx] |= 0x80;
					return idx + 1;
				}
			}
			return 0; //buff length not enough
		}
		constexpr static size_t EncodeLength(size_t length_type_size) {
			return length_type_size * 2;
		}
		static inline nq_size_t Decode(nq_size_t *psz_decoded, const char *buf, nq_size_t bufsz) {
			nq_size_t idx = 0;
			*psz_decoded = 0;
			while (bufsz > idx) {
				*psz_decoded += ((buf[idx] & 0x7f) << (idx * 8));
				if ((buf[idx] & 0x80) == 0) {
					idx++;
				} else {
					return idx + 1;
				}
			}
			return 0; //not enough buffer arrived
		}
	};
}
