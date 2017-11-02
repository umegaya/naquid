#pragma once

#include <cstdlib>

#include "basis/loop_impl.h"
#include "basis/io_processor.h"

namespace nq {
	class Loop : public LoopImpl, IoProcessor {
		IoProcessor **processors_;
		int max_nfd_;
		LoopImpl::Timeout timeout_;
	public:
		static const int kMinimumProcessorArraySize = 16;
		typedef LoopImpl::Event Event;
		Loop() : LoopImpl(), processors_(nullptr), max_nfd_(-1) {}
		~Loop() { Close(); }
		template <class T> T *ProcessorAt(int fd) { return (T *)processors_[fd]; }
		inline int Open(int max_nfd, int timeout_ns = 1000 * 1000) {
			if (max_nfd < kMinimumProcessorArraySize) {
				max_nfd = kMinimumProcessorArraySize;
			}
			max_nfd_ = max_nfd; //TODO: use getrlimit if max_nfd omitted
			ToTimeout(timeout_ns, timeout_);
			processors_ = (IoProcessor**)std::malloc(sizeof(IoProcessor*) * max_nfd_);
			memset(processors_, 0, sizeof(IoProcessor*) * max_nfd_);
			return LoopImpl::Open(max_nfd_);
		}
		inline void Close() {
			if (processors_ != nullptr) {
				delete []processors_;
        processors_ = nullptr;
			}
			LoopImpl::Close();
		}
		inline int Add(Fd fd, IoProcessor *h, uint32_t flags) {
			int r = h->OnOpen(fd);
			if (r < 0) { return r; }
      CheckAndGrow(fd);
			ASSERT(processors_[fd] == nullptr);
			processors_[fd] = h;
			return LoopImpl::Add(fd, flags);
		}
		inline int Mod(Fd fd, uint32_t flags) {
			ASSERT(fd < max_nfd_ && processors_[fd] != nullptr);
			return LoopImpl::Mod(fd, flags);
		}
		inline int Del(Fd fd) {
			ASSERT(fd < max_nfd_ && processors_[fd] != nullptr);
			int r = LoopImpl::Del(fd);
			if (r >= 0) {
				auto h = processors_[fd];
				processors_[fd] = nullptr;
				h->OnClose(fd);
			}
			return r;
		}
		inline void Poll() {
			Event list[max_nfd_];
			int n_list = LoopImpl::Wait(list, max_nfd_, timeout_);
			if (n_list <= 0) {
				return;
			}
			for (int i = 0; i < n_list; i++) {
				const auto &ev = list[i];
				Fd fd = LoopImpl::From(ev);
				auto h = processors_[fd];
				if (LoopImpl::Closed(ev)) {
					processors_[fd] = nullptr;
					h->OnClose(fd);
					continue;
				}
				h->OnEvent(fd, ev);
			}
		}
	public: //IoProcessor
		void OnEvent(Fd fd, const Event &e) override { Poll(); }
		int OnOpen(Fd) override { return NQ_OK; }
		void OnClose(Fd) override {}

		inline void CheckAndGrow(Fd fd) {
			if ((int)fd >= max_nfd_) {
        int old = max_nfd_;
        do {
          max_nfd_ <<= 1;
        } while (max_nfd_ > (int)fd);
        processors_ = (IoProcessor**)std::realloc(processors_, max_nfd_ * sizeof(IoProcessor*));
        memset(processors_ + old, 0, sizeof(IoProcessor*) * (max_nfd_ - old));
      }
		}
	};
}
