#pragma once

#include "nq.h"

namespace nq {
	namespace clock {
		nq_time_t now();
		void now(long &sec, long &nsec);
		nq_time_t sleep(nq_time_t dur);
		nq_time_t pause(nq_time_t dur);
		inline uint64_t to_us(nq_time_t t) {
			return t / 1000;
		}
	}
}
