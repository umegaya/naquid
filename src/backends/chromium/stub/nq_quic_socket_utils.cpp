#include "net/tools/quic/platform/impl/quic_socket_utils.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string>

#ifdef OS_LINUX
#include <fcntl.h>
#endif

#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_socket_address.h"

#include "basis/defs.h"
#include "basis/endian.h"
#include "basis/header_codec.h"
#include "basis/syscall.h"

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

#if defined(arraysize)
#undef arraysize
#endif
#define arraysize(array) ((size_t)(sizeof(array)/sizeof(array[0])))

using std::string;

namespace net {

// static
void QuicSocketUtils::GetAddressAndTimestampFromMsghdr(
    struct msghdr* hdr,
    QuicIpAddress* address,
    QuicWallTime* walltimestamp) {
  if (hdr->msg_controllen > 0) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(hdr); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(hdr, cmsg)) {
      char* addr_data = nullptr;
      int len = 0;
#if defined(IPV6_PKTINFO)
      if (cmsg->cmsg_type == IPV6_PKTINFO) {
        in6_pktinfo* info = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
        addr_data = reinterpret_cast<char*>(&info->ipi6_addr);
        len = sizeof(in6_addr);
        address->FromPackedString(addr_data, len);
      } else 
#endif
      if (cmsg->cmsg_type == IP_PKTINFO) {
        in_pktinfo* info = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
        addr_data = reinterpret_cast<char*>(&info->ipi_addr);
        len = sizeof(in_addr);
        address->FromPackedString(addr_data, len);
#if defined(SO_TIMESTAMPING)
      } else if (cmsg->cmsg_level == SOL_SOCKET &&
                 cmsg->cmsg_type == SO_TIMESTAMPING) {
        LinuxTimestamping* lts =
            reinterpret_cast<LinuxTimestamping*>(CMSG_DATA(cmsg));
        timespec* ts = &lts->systime;
        int64_t usec = (static_cast<int64_t>(ts->tv_sec) * 1000 * 1000) +
                       (static_cast<int64_t>(ts->tv_nsec) / 1000);
        *walltimestamp = QuicWallTime::FromUNIXMicroseconds(usec);
#endif        
      }
    }
  }
}

// static
bool QuicSocketUtils::GetOverflowFromMsghdr(struct msghdr* hdr,
                                            QuicPacketCount* dropped_packets) {
  if (hdr->msg_controllen > 0) {
    struct cmsghdr* cmsg;
    for (cmsg = CMSG_FIRSTHDR(hdr); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(hdr, cmsg)) {
      if (cmsg->cmsg_type == SO_RXQ_OVFL) {
        *dropped_packets = *(reinterpret_cast<uint32_t*> CMSG_DATA(cmsg));
        return true;
      }
    }
  }
  return false;
}

// static
bool QuicSocketUtils::GetTtlFromMsghdr(struct msghdr* hdr, int* ttl) {
  if (hdr->msg_controllen > 0) {
    struct cmsghdr* cmsg;
    for (cmsg = CMSG_FIRSTHDR(hdr); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(hdr, cmsg)) {
      if ((cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TTL) ||
#if defined(IPV6_HOPLIMIT)
          (cmsg->cmsg_level == IPPROTO_IPV6 &&
           cmsg->cmsg_type == IPV6_HOPLIMIT)
#else
      	  (false)
#endif
      ){
        *ttl = *(reinterpret_cast<int*>(CMSG_DATA(cmsg)));
        return true;
      }
    }
  }
  return false;
}

// static
int QuicSocketUtils::SetGetAddressInfo(int fd, int address_family) {
#if defined(OS_MACOSX)
  if (address_family == AF_INET6) {
    //for osx, IP_PKTINFO for ipv6 will not work. (at least at Sierra)
    return 0;
  }
#endif
  int get_local_ip = 1;
  int rc = setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                      sizeof(get_local_ip));
#if defined(IPV6_RECVPKTINFO)
  if (rc == 0 && address_family == AF_INET6) {
    rc = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                    sizeof(get_local_ip));
  }
#endif
  return rc;
}

// static
int QuicSocketUtils::SetGetSoftwareReceiveTimestamp(int fd) {
#if defined(SO_TIMESTAMPING) && defined(SOF_TIMESTAMPING_RX_SOFTWARE)
  int timestamping = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
  return setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &timestamping,
                    sizeof(timestamping));
#else
  return -1;
#endif
}

// static
bool QuicSocketUtils::SetSendBufferSize(int fd, size_t size) {
  if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) != 0) {
    LOG(ERROR) << "Failed to set socket send size";
    return false;
  }
  return true;
}

// static
bool QuicSocketUtils::SetReceiveBufferSize(int fd, size_t size) {
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
    LOG(ERROR) << "Failed to set socket recv size";
    return false;
  }
  return true;
}

// static
WriteResult QuicSocketUtils::WritePacket(
    int,
    const char*,
    size_t,
    const QuicIpAddress&,
    const QuicSocketAddress&) {
  ASSERT(false);
  //move to nq_packet_writer.cpp
  return WriteResult(WRITE_STATUS_ERROR, EINVAL);
}
// static
int QuicSocketUtils::ReadPacket(int fd,
                                char* buffer,
                                size_t buf_len,
                                QuicPacketCount* dropped_packets,
                                QuicIpAddress* self_address,
                                QuicWallTime* walltimestamp,
                                QuicSocketAddress* peer_address) {
  DCHECK(peer_address != nullptr);
  char cbuf[kSpaceForCmsg];
  memset(cbuf, 0, arraysize(cbuf));

  iovec iov = {buffer, buf_len};
  struct sockaddr_storage raw_address;
  msghdr hdr;

  hdr.msg_name = &raw_address;
  hdr.msg_namelen = sizeof(sockaddr_storage);
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  hdr.msg_flags = 0;

  struct cmsghdr* cmsg = reinterpret_cast<struct cmsghdr*>(cbuf);
  cmsg->cmsg_len = arraysize(cbuf);
  hdr.msg_control = cmsg;
  hdr.msg_controllen = arraysize(cbuf);

  int bytes_read = recvmsg(fd, &hdr, 0);

  // Return before setting dropped packets: if we get EAGAIN, it will
  // be 0.
  if (bytes_read < 0 && errno != 0) {
    if (errno != EAGAIN) {
      LOG(ERROR) << "Error reading " << strerror(errno);
    }
    return -1;
  }

  if (hdr.msg_controllen >= arraysize(cbuf)) {
    QUIC_BUG << "Incorrectly set control length: " << hdr.msg_controllen
             << ", expected " << arraysize(cbuf);
    return -1;
  }

  if (dropped_packets != nullptr) {
    GetOverflowFromMsghdr(&hdr, dropped_packets);
  }

  QuicIpAddress stack_address;
  if (self_address == nullptr) {
    self_address = &stack_address;
  }

  QuicWallTime stack_walltimestamp = QuicWallTime::FromUNIXMicroseconds(0);
  if (walltimestamp == nullptr) {
    walltimestamp = &stack_walltimestamp;
  }

  GetAddressAndTimestampFromMsghdr(&hdr, self_address, walltimestamp);

  *peer_address = QuicSocketAddress(raw_address);
  return bytes_read;
}

size_t QuicSocketUtils::SetIpInfoInCmsg(const QuicIpAddress& self_address,
                                        cmsghdr* cmsg) {
  string address_string;
  if (self_address.IsIPv4()) {
    cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
    cmsg->cmsg_level = IPPROTO_IP;
    cmsg->cmsg_type = IP_PKTINFO;
    in_pktinfo* pktinfo = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
    memset(pktinfo, 0, sizeof(in_pktinfo));
    pktinfo->ipi_ifindex = 0;
    address_string = self_address.ToPackedString();
    memcpy(&pktinfo->ipi_spec_dst, address_string.c_str(),
           address_string.length());
    return sizeof(in_pktinfo);
#if defined(IPV6_PKTINFO)
  } else if (self_address.IsIPv6()) {
    cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
    cmsg->cmsg_level = IPPROTO_IPV6;
    cmsg->cmsg_type = IPV6_PKTINFO;
    in6_pktinfo* pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
    memset(pktinfo, 0, sizeof(in6_pktinfo));
    address_string = self_address.ToPackedString();
    memcpy(&pktinfo->ipi6_addr, address_string.c_str(),
           address_string.length());
    return sizeof(in6_pktinfo);
#endif
  } else {
    NOTREACHED() << "Unrecognized IPAddress";
    return 0;
  }
}

static bool SetNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    int saved_errno = errno;
    char buf[256];
    QUIC_LOG(FATAL) << "Error " << saved_errno
               << " doing fcntl(" << fd << ", F_GETFL, 0): "
               << strerror_r(saved_errno, buf, sizeof(buf));
    return false;
  }
  if (!(flags & O_NONBLOCK)) {
    int saved_flags = flags;
    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
      // bad.
      int saved_errno = errno;
      char buf[256];
      QUIC_LOG(FATAL) << "Error " << saved_errno
        << " doing fcntl(" << fd << ", F_SETFL, " << saved_flags << "): "
        << strerror_r(saved_errno, buf, sizeof(buf));
	  return false;
    }
  }
  return true;
}

// static
int QuicSocketUtils::CreateUDPSocket(const QuicSocketAddress& address,
                                     bool* overflow_supported) {
  int address_family = address.host().AddressFamilyToInt();
  int fd = socket(address_family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    QUIC_LOG(ERROR) << "socket() failed: " << strerror(errno);
    return -1;
  }
  if (!SetNonblocking(fd)) {
  	return -1;
  }

  int get_overflow = 1;
  int rc = setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &get_overflow,
                      sizeof(get_overflow));
  if (rc < 0) {
    /* QUIC_DLOG(WARNING) << "Socket overflow detection not supported: " << strerror(errno); */
  } else {
    *overflow_supported = true;
  }

  if (!SetReceiveBufferSize(fd, kDefaultSocketReceiveBuffer)) {
    return -1;
  }

  if (!SetSendBufferSize(fd, kDefaultSocketReceiveBuffer)) {
    return -1;
  }

  rc = SetGetAddressInfo(fd, address_family);
  if (rc < 0) {
    LOG(ERROR) << "IP detection not supported: " << strerror(errno);
    return -1;
  }

  rc = SetGetSoftwareReceiveTimestamp(fd);
  if (rc < 0) {
    /*QUIC_LOG(WARNING) << "SO_TIMESTAMPING not supported; using fallback: "
                      << strerror(errno); */
  }

  return fd;
}

} //net