#pragma once

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_types.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/platform/api/quic_socket_address.h"
namespace net {
typedef QuicStreamId NqQuicStreamId;
typedef QuicConnectionId NqQuicConnectionId;
typedef QuicSocketAddress NqQuicSocketAddress;
typedef QuicCryptoStream NqQuicCryptoStream;
typedef QuicConnection NqQuicConnection;
class NqQuicServerId : public QuicServerId {
public:
  NqQuicServerId(const std::string &host, int port) : QuicServerId(host, port, PRIVACY_MODE_ENABLED) {}
};
} // net
#else
#include <stdint.h>
#include <sys/socket.h>
namespace net {
typedef std::uint32_t NqQuicStreamId;
typedef uint64_t NqQuicConnectionId;
typedef void *NqQuicCryptoStream;
typedef quiche_conn_t NqQuicConnection;
class NqQuicServerId {
 public:
  NqQuicServerId(const std::string &host, int port) {
    id_ = host + ":" + std::to_string(port);
  }
 private:
  std::string id_;
};
class NqQuicSocketAddress {
 public:
  NqQuicSocketAddress();
 private:
  int port_;
  struct sockaddr_storage host_;
};
} // net
#endif
