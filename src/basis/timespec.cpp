#include "basis/timespec.h"
#include "basis/syscall.h"
#include <time.h>
#include <cerrno>

#if defined(WIN32)
#include <winnt.h>
#include <minwinbase.h>
#include <timezoneapi.h>
#include <profileapi.h>
#include <sysinfoapi.h>

#define	CLOCK_REALTIME (0)

//borrow from https://stackoverflow.com/questions/5404277/porting-clock-gettime-to-windows
LARGE_INTEGER
getFILETIMEoffset()
{
	SYSTEMTIME s;
	FILETIME f;
	LARGE_INTEGER t;

	s.wYear = 1970;
	s.wMonth = 1;
	s.wDay = 1;
	s.wHour = 0;
	s.wMinute = 0;
	s.wSecond = 0;
	s.wMilliseconds = 0;
	SystemTimeToFileTime(&s, &f);
	t.QuadPart = f.dwHighDateTime;
	t.QuadPart <<= 32;
	t.QuadPart |= f.dwLowDateTime;
	return (t);
}

int
clock_gettime(int X, struct timespec *tv)
{
	LARGE_INTEGER           t;
	FILETIME            f;
	double                  microseconds;
	static LARGE_INTEGER    offset;
	static double           frequencyToMicroseconds;
	static int              initialized = 0;
	static BOOL             usePerformanceCounter = 0;

	if (!initialized) {
		LARGE_INTEGER performanceFrequency;
		initialized = 1;
		usePerformanceCounter = QueryPerformanceFrequency(&performanceFrequency);
		if (usePerformanceCounter) {
			QueryPerformanceCounter(&offset);
			frequencyToMicroseconds = (double)performanceFrequency.QuadPart / 1000000.;
		}
		else {
			offset = getFILETIMEoffset();
			frequencyToMicroseconds = 10.;
		}
	}
	if (usePerformanceCounter) QueryPerformanceCounter(&t);
	else {
		GetSystemTimeAsFileTime(&f);
		t.QuadPart = f.dwHighDateTime;
		t.QuadPart <<= 32;
		t.QuadPart |= f.dwLowDateTime;
	}

	t.QuadPart -= offset.QuadPart;
	microseconds = (double)t.QuadPart / frequencyToMicroseconds;
	t.QuadPart = microseconds;
	tv->tv_sec = t.QuadPart / 1000000;
	tv->tv_nsec = (t.QuadPart % 1000000)*1000;
	return (0);
}
BOOLEAN nanosleep(LONGLONG ns) {
	/* Declarations */
	HANDLE timer;	/* Timer handle */
	LARGE_INTEGER li;	/* Time defintion */
						/* Create timer */
	if (!(timer = CreateWaitableTimer(NULL, TRUE, NULL)))
		return FALSE;
	/* Set timer properties */
	li.QuadPart = -ns;
	if (!SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE)) {
		CloseHandle(timer);
		return FALSE;
	}
	/* Start & wait for timer */
	WaitForSingleObject(timer, INFINITE);
	/* Clean resources */
	CloseHandle(timer);
	/* Slept without problems */
	return TRUE;
}
#endif 

namespace nq {
	namespace clock {
		static inline nq_time_t to_timespec(struct timespec &ts) {
			return ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
		}
		static inline nq_time_t rawsleep(nq_time_t dur, bool ignore_intr) {
#if defined(WIN32)
			return nanosleep(dur) ? 0 : dur;
#else
			int r; struct timespec ts, rs, *pts = &ts, *prs = &rs, *tmp;
			ts.tv_sec = dur / (1000 * 1000 * 1000);
			ts.tv_nsec = dur % (1000 * 1000 * 1000);
		resleep:
			//TRACE("start:%p %u(s) + %u(ns)\n", pts, pts->tv_sec, pts->tv_nsec);
			if (0 == (r = nanosleep(pts, prs))) {
				return 0;
			}
			//TRACE("left:%p %u(s) + %u(ns)\n", prs, prs->tv_sec, prs->tv_nsec);
			/* signal interrupt. keep on sleeping */
			if (r == -1 && errno == EINTR) {
				tmp = pts; pts = prs; prs = tmp;
				if (!ignore_intr) {
					goto resleep;
				}
			}
			return to_timespec(*prs);
#endif
		}
		nq_time_t now() {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			return to_timespec(ts);
		}
		void now(long &sec, long &nsec) {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			sec = ts.tv_sec;
			nsec = ts.tv_nsec;			
		}
		nq_time_t sleep(nq_time_t dur) {
			return rawsleep(dur, true);
		}
		nq_time_t pause(nq_time_t dur) {
			return rawsleep(dur, false);
		}
	}
}
