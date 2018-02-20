#include "core/platform/nq_platform.h"
#if defined(OS_WIN)
#include "base/threading/platform_thread.h"

namespace base {
// static
PlatformThreadId PlatformThread::CurrentId() {
  return ::GetCurrentThreadId(); 
}
// static
PlatformThreadHandle PlatformThread::CurrentHandle() {
  return PlatformThreadHandle(::GetCurrentThread());
}
// static
void PlatformThread::YieldCurrentThread() {
  ::Sleep(0);
}
// static
void PlatformThread::Sleep(TimeDelta duration) {
  // When measured with a high resolution clock, Sleep() sometimes returns much
  // too early. We may need to call it repeatedly to get the desired duration.
  TimeTicks end = TimeTicks::Now() + duration;
  for (TimeTicks now = TimeTicks::Now(); now < end; now = TimeTicks::Now())
    ::Sleep(static_cast<DWORD>((end - now).InMillisecondsRoundedUp()));
}
}
#endif