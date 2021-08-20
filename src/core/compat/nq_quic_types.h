#pragma once

#include "basis/syscall.h"
#include "basis/endian.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_types.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/platform/api/quic_socket_address.h"

namespace nq {
using namespace net;
typedef QuicStreamId NqQuicStreamId;
typedef QuicConnectionId NqQuicConnectionId;
class NqQuicSocketAddress : public QuicSocketAddress {
 public:
  NqQuicSocketAddress() : QuicSocketAddress() {}
  NqQuicSocketAddress(QuicIpAddress address, uint16_t port) : QuicSocketAddress(address, port) {}

  //get/set
  inline int family() const { return generic_address().ss_family; }

  //static
  static bool ConvertToIpAddress(const char *data, int len, QuicIpAddress &ip) {
    return ip.FromPackedString(data, len);
  }
  static bool FromPackedString(const char *data, int len, int port, NqQuicSocketAddress &address) {
    QuicIpAddress ip;
    if (!ConvertToIpAddress(data, len, ip)) {
      return false;
    }
    address = NqQuicSocketAddress(ip, port);
    return true;
  }
  static bool FromHostent(struct hostent *entries, int port, NqQuicSocketAddress &address) {
    return FromPackedString(entries->h_addr_list[0], Syscall::GetIpAddrLen(entries->h_addrtype), port, address);
  }
};
typedef QuicCryptoStream NqQuicCryptoStream;
typedef QuicConnection NqQuicConnection;
class NqQuicServerId : public QuicServerId {
public:
  NqQuicServerId(const std::string &host, int port) : QuicServerId(host, port, PRIVACY_MODE_ENABLED) {}
};
class NqPacket : public QuicReceivedPacket {
  QuicSocketAddress client_address_, server_address_;
  int port_;
 public:
  NqPacket(const char* buffer,
          size_t length,
          QuicTime receipt_time,
          int ttl,
          bool ttl_valid, 
          struct sockaddr_storage client_sockaddr, 
          QuicIpAddress &server_ip, int server_port) :
    QuicReceivedPacket(buffer, length, receipt_time, false, ttl, ttl_valid), 
    client_address_(client_sockaddr), server_address_(server_ip, server_port) {}
  inline QuicSocketAddress &server_address() { return server_address_; }
  inline QuicSocketAddress &client_address() { return client_address_; }
  inline void set_port(int port) { port_ = port; }
  inline int port() const { return port_; }
  inline uint64_t ConnectionId() const {
    switch (data()[0] & 0x08) {
      case 0x08:
        return Endian::NetbytesToHost<uint64_t>(data() + 1);
      default:
        //TODO(iyatomi): can we detect connection_id from packet->client_address()? but chromium server sample itself seems to ignore...
        return 0;
    }
  } 
};
} // net
#else
#include <stdint.h>
#include <sys/socket.h>
namespace nq {
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
  NqQuicSocketAddress() : port_(0) {}

  //get/set
  inline sockaddr_storage generic_address() const { return host_; }
  inline int family() const { return generic_address().ss_family; }

 private:
  int port_;
  struct sockaddr_storage host_;
};
class NqPacket : public NqQuicSocketAddress {
 public:
  NqPacket() : NqQuicSocketAddress(), buff_(nullptr), len_(0) {}
  inline int port() const { return port_; }
  inline const char *data() const { return buff_; }
  inline uint64_t ConnectionId() const {
    switch (data()[0] & 0x08) {
      case 0x08:
        return Endian::NetbytesToHost<uint64_t>(data() + 1);
      default:
        //TODO(iyatomi): can we detect connection_id from packet->client_address()? but chromium server sample itself seems to ignore...
        return 0;
    }
  }
 private:
  char *buff_;
  nq_size_t len_;
};
} // net
#endif
