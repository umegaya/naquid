#pragma once

#include "nq.h"
#include "basis/defs.h"
#include "basis/syscall.h"

#if defined(__ENABLE_EPOLL__)
#include <sys/epoll.h>
#include <sys/types.h>

#if !defined(EPOLLRDHUP)
#define EPOLLRDHUP 0x2000
#endif
#if defined(__ANDROID_NDK__) && !defined(EPOLLONESHOT)
#define EPOLLONESHOT (1u << 30)
#endif

namespace nq {
namespace internal {
	class Epoll {
	protected:
		Fd fd_;
	public:
		constexpr static uint32_t EV_READ = EPOLLIN;
		constexpr static uint32_t EV_WRITE = EPOLLOUT;
  #if !defined(LOOP_LEVEL_TRIGGER)
    constexpr static uint32_t EV_ET = EPOLLET;
  #else
    constexpr static uint32_t EV_ET = 0;
  #endif
		typedef struct epoll_event Event;
		typedef int Timeout;
		
		Epoll() : fd_(INVALID_FD) {}
		
		//instance method
		inline int Open(int max_nfd) {
			if ((fd_ = ::epoll_create(max_nfd)) < 0) {
				TRACE("ev:syscall fails,call:epoll_create,nfd:%d,errno:%d", max_nfd, Errno());
				return NQ_ESYSCALL;
			}
			return NQ_OK;
		}
		inline void Close() { Syscall::Close(fd_); }
		inline int Errno() { return Syscall::Errno(); }
		inline bool EAgain() { return Syscall::EAgain(); }
		inline int Add(Fd d, uint32_t flag) {
			Event e;
			e.events = (flag | EV_ET | EPOLLRDHUP);
			e.data.fd = d;
			return ::epoll_ctl(fd_, EPOLL_CTL_ADD, d, &e) != 0 ? NQ_ESYSCALL : NQ_OK;
		}
		inline int Mod(Fd d, uint32_t flag) {
			Event e;
			e.events = (flag | EV_ET | EPOLLRDHUP);
			e.data.fd = d;
			return ::epoll_ctl(fd_, EPOLL_CTL_MOD, d, &e) != 0 ? NQ_ESYSCALL : NQ_OK;
		}
		inline int Del(Fd d) {
			Event e;
			return ::epoll_ctl(fd_, EPOLL_CTL_DEL, d, &e) != 0 ? NQ_ESYSCALL : NQ_OK;
		}
		inline int Wait(Event *ev, int size, Timeout &to) {
			return ::epoll_wait(fd_, ev, size, to);
		}

		//static method
		static inline void InitEvent(Event &e, Fd fd = INVALID_FD) { e.events = 0; e.data.fd = fd; }
		static inline Fd From(const Event &e) { return e.data.fd; }
		static inline bool Readable(const Event &e) { return e.events & EV_READ; }
		static inline bool Writable(const Event &e) { return e.events & EV_WRITE; }
		static inline bool Closed(const Event &e) { return e.events & EPOLLRDHUP; }
		static inline void ToTimeout(int timeout_ns, Timeout &to) { to = (timeout_ns / (1000 * 1000)); }
	private:
		const Epoll &operator = (const Epoll &);
	};
}
typedef internal::Epoll LoopImpl;
}

#elif defined(__ENABLE_KQUEUE__)

#include <sys/event.h>
#include <sys/types.h>

namespace nq {
namespace internal {
	class Kqueue {
	protected:
		Fd fd_;
	public:
		constexpr static uint32_t EV_READ = 0x01;
		constexpr static uint32_t EV_WRITE = 0x02;
  #if !defined(LOOP_LEVEL_TRIGGER)
    constexpr static uint32_t EV_ET = EV_CLEAR;
  #else
    constexpr static uint32_t EV_ET = 0;
  #endif
		typedef struct kevent Event;
		typedef struct timespec Timeout;

		Kqueue() : fd_(INVALID_FD) {}
		
		//instance method
		inline int Open(int max_nfd) {
			fd_ = ::kqueue();
			return fd_ < 0 ? NQ_ESYSCALL : NQ_OK;
		}
		inline void Close() { 
      if (fd_ != INVALID_FD) { 
        Syscall::Close(fd_); 
        fd_ = INVALID_FD;
      }
    }
		inline int Errno() { return Syscall::Errno(); }
		inline bool EAgain() { return Syscall::EAgain(); }
		inline int Add(Fd d, uint32_t flag) {
			return register_from_flag(d, flag, EV_ADD | EV_ET | EV_EOF);
		}
		inline int Mod(Fd d, uint32_t flag) {
			return register_from_flag(d, flag, EV_ADD | EV_ET | EV_EOF);
		}
		inline int Del(Fd d) {
			return register_from_flag(d, EV_READ, EV_DELETE);
		}
		inline int Wait(Event *ev, int size, Timeout &to) {
			return ::kevent(fd_, nullptr, 0, ev, size, &to);
		}

		//static method
		static inline void InitEvent(Event &e, Fd fd = INVALID_FD) { 
			e.filter = 0; e.flags = 0; e.ident = fd; 
		}
		static inline Fd From(const Event &e) { return e.ident; }
		static inline bool Readable(const Event &e) { return e.filter == EVFILT_READ; }
		static inline bool Writable(const Event &e) { return e.filter == EVFILT_WRITE; }
		/* TODO: not sure about this check */
		static inline bool Closed(const Event &e) { return e.flags & (EV_EOF | EV_ERROR);}
		static inline void ToTimeout(int timeout_ns, Timeout &to) {
			to.tv_sec = (timeout_ns / (1000 * 1000 * 1000));
			to.tv_nsec = (timeout_ns % (1000 * 1000 * 1000));
		}
	private:
		const Kqueue &operator = (const Kqueue &);
		inline int register_from_flag(Fd d, uint32_t flag, uint32_t control_flag) {
			int r = NQ_OK, cnt = 0;
			Event ev[2];
			if (flag & EV_WRITE) {
				EV_SET(&(ev[cnt++]), d, EVFILT_WRITE, control_flag, 0, 0, nullptr);
			}
			if (flag & EV_READ) {
				EV_SET(&(ev[cnt++]), d, EVFILT_READ, control_flag, 0, 0, nullptr);
			}
			if (::kevent(fd_, ev, cnt, nullptr, 0, nullptr) != 0) {
				r = NQ_ESYSCALL;
			}
			return r;
		}
	};
}
typedef internal::Kqueue LoopImpl;
}

#else //TODO: windows
#error no suitable poller function
#endif
