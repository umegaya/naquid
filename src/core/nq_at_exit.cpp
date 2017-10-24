#include "base/at_exit.h"

base::AtExitManager at_exit;

extern base::AtExitManager *nq_at_exit_manager() {
	return &at_exit;
}