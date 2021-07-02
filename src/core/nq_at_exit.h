#pragma once

#if defined(NQ_CHROMIUM_BACKEND)
#include "base/at_exit.h"

typedef base::AtExitManager *nq_at_exit_manager_t;
#else
typedef void *nq_at_exit_manager_t;
#endif
