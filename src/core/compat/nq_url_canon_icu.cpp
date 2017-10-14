#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "url/url_canon_icu.h"
#include "url/url_canon_internal.h"  // for _itoa_s

#include "basis/defs.h"

namespace url {
bool IDNToASCII(const base::char16* src, int src_len, CanonOutputW* output) {
	ASSERT(false);
	return false;
}
}
