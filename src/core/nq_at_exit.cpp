#include "nq_at_exit.h"

#if defined(NQ_CHROMIUM_BACKEND)
base::AtExitManager at_exit;

extern nq_at_exit_manager_t nq_at_exit_manager() {
	return &at_exit;
}
#else
extern nq_at_exit_manager_t nq_at_exit_manager() {
	return nullptr;
}
#endif
