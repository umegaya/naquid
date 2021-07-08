#pragma once

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_types.h"
#include "net/quic/core/quic_server_id.h"
namespace net {
typedef QuicStreamId NqQuicStreamId;
typedef QuicConnectionId NqQuicConnectionId;
class NqQuicServerId : public QuicServerId {
public:
  NqQuicServerId(const std::string &host, int port) : QuicServerId(host, port, PRIVACY_MODE_ENABLED) {}
};
} // net
#else
#include <stdint.h>
namespace net {
typedef uint32_t NqQuicStreamId;
typedef uint64_t NqQuicConnectionId;
class NqQuicServerId {
  NqQuicServerId(const std::string &host, int port) {
    id_ = host + ":" + std::to_string(port);
  }
private:
  std::string id_;
};
} // net
#endif
