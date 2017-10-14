#pragma once

#include "basis/loop_impl.h"
#include "basis/io_processor.h"

namespace nq {
	class Loop : public LoopImpl, IoProcessor {
		IoProcessor **processors_;
		int max_nfd_;
		LoopImpl::Timeout timeout_;
	public:
		typedef LoopImpl::Event Event;
		Loop() : LoopImpl(), processors_(nullptr), max_nfd_(-1) {}
		~Loop() { Close(); }
		template <class T> T *ProcessorAt(int fd) { return (T *)processors_[fd]; }
		inline int Open(int max_nfd, int timeout_ns = 1000 * 1000) {
			max_nfd_ = max_nfd; //TODO: use getrlimit if max_nfd omitted
			ToTimeout(timeout_ns, timeout_);
			processors_ = new IoProcessor*[max_nfd_];
			memset(processors_, 0, sizeof(IoProcessor *) * max_nfd_);
			return LoopImpl::Open(max_nfd_);
		}
		inline void Close() {
			if (processors_ != nullptr) {
				delete []processors_;
			}
			LoopImpl::Close();
		}
		inline int Add(Fd fd, IoProcessor *h, uint32_t flags) {
			int r = h->OnOpen(fd);
			if (r < 0) { return r; }
			ASSERT(processors_[fd] == nullptr);
			processors_[fd] = h;
			return LoopImpl::Add(fd, flags);
		}
		inline int Mod(Fd fd, uint32_t flags) {
			ASSERT(processors_[fd] != nullptr);
			return LoopImpl::Mod(fd, flags);
		}
		inline int Del(Fd fd) {
			ASSERT(processors_[fd] != nullptr);
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
	};
}
