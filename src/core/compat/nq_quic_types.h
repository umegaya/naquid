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
  inline nq_size_t generic_address_length() const { return Syscall::GetSockAddrLen(family()); }


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
const char *NqQuicStrError(nq_error_t error) {
  return QuicErrorCodeToString(static_cast<QuicErrorCode>(code));
}
} // net
#else
#include <netdb.h>
#include <stdint.h>
#include <sys/socket.h>

#include "core/compat/quiche/deps.h"

namespace nq {
typedef std::uint32_t NqQuicStreamId;
typedef uint64_t NqQuicConnectionId;
typedef void *NqQuicCryptoStream;
typedef quiche_conn *NqQuicConnection;
class NqQuicServerId {
 public:
  NqQuicServerId(const std::string &host, int port) : host_(host), port_(port) {}
  inline const std::string &host() const { return host_; }
  inline int port() const { return port_; }
 private:
  std::string host_;
  int port_;
};
class NqQuicSocketAddress {
 public:
  NqQuicSocketAddress() {}
  NqQuicSocketAddress(const char *data, nq_size_t len, int port) {
    set_generic_address(data, len, port);
  }

  //get/set
  inline const sockaddr_storage &generic_address() const { return addr_; }
  inline nq_size_t generic_address_length() const { return Syscall::GetSockAddrLen(family()); }
  inline int family() const { return generic_address().ss_family; }
  inline int port() const { return Syscall::GetSockAddrPort(addr_); }
  inline void set_generic_address(const char *data, nq_size_t len, int port) {
    Syscall::SetSockAddr(addr_, data, len, port);
  }

  //operation
  std::string ToString() const;

  //static
  static bool FromPackedString(const char *data, int len, int port, NqQuicSocketAddress &address);
  static bool FromHostent(struct hostent *entries, int port, NqQuicSocketAddress &address) {
    return FromPackedString(entries->h_addr_list[0], Syscall::GetIpAddrLen(entries->h_addrtype), port, address);
  }

 protected:
  struct sockaddr_storage addr_;
};
class NqPacket : public NqQuicSocketAddress {
 public:
  NqPacket() : NqQuicSocketAddress(), buff_(nullptr), len_(0) {}
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
 protected:
  char *buff_;
  nq_size_t len_;
};
const char *NqQuicStrError(nq_error_t error) {
  ASSERT(false);
  return "TODO:NqQuicStrError";
}
} // net
#endif
