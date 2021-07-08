#pragma once

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_types.h"
namespace net {
typedef QuicStreamId NqQuicStreamId;
typedef QuicConnectionId NqQuicConnectionId;
} // net
#else
#include <stdint.h>
namespace net {
typedef uint32_t NqQuicStreamId;
typedef uint64_t NqQuicConnectionId;
} // net
#endif
