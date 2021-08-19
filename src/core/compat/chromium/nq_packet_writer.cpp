#include "core/compat/chromium/nq_packet_writer.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string>

#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"

#include "core/compat/nq_quic_types.h"

extern bool chaos_write();

namespace net {
// static
WriteResult NqPacketWriter::WritePacket(
    int fd,
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    bool reachability_tracked) {
  //TRACE("Write %zu bytes to %d from %s", buf_len, fd, self_address.ToString().c_str());
  sockaddr_storage raw_address = peer_address.generic_address();
  iovec iov = {const_cast<char*>(buffer), buf_len};

  msghdr hdr;
  hdr.msg_name = &raw_address;
  hdr.msg_namelen = raw_address.ss_family == AF_INET ? sizeof(sockaddr_in)
                                                     : sizeof(sockaddr_in6);
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  hdr.msg_flags = 0;

  const int kSpaceForIpv4 = CMSG_SPACE(sizeof(in_pktinfo));
  const int kSpaceForIpv6 = CMSG_SPACE(sizeof(in6_pktinfo));
  // kSpaceForIp should be big enough to hold both IPv4 and IPv6 packet info.
  const int kSpaceForIp =
      (kSpaceForIpv4 < kSpaceForIpv6) ? kSpaceForIpv6 : kSpaceForIpv4;
  char cbuf[kSpaceForIp];
  if (!self_address.IsInitialized()) {
    hdr.msg_control = nullptr;
    hdr.msg_controllen = 0;
  } else {
    hdr.msg_control = cbuf;
    hdr.msg_controllen = kSpaceForIp;
    cmsghdr* cmsg = CMSG_FIRSTHDR(&hdr);
    QuicSocketUtils::SetIpInfoInCmsg(self_address, cmsg);
    hdr.msg_controllen = cmsg->cmsg_len;
  }

#if defined(DEBUG)
  static int n_count = 0;
  if (chaos_write()) {
    n_count++;
  }
  if (n_count < 11) {
    int rc;
    do {
      rc = sendmsg(fd, &hdr, 0);
    } while (rc < 0 && errno == EINTR);
    if (rc >= 0) {
      return WriteResult(WRITE_STATUS_OK, rc);
    }
    fprintf(stderr, "%d: fail to send: %s(%d:%s)\n", fd, strerror(errno), errno, 
      nq::Syscall::WriteMayBlocked(errno, reachability_tracked) ? "blocked" : "error");
    return WriteResult(nq::Syscall::WriteMayBlocked(errno, reachability_tracked)
                           ? WRITE_STATUS_BLOCKED
                           : WRITE_STATUS_ERROR,
                       errno);
  } 
  if (n_count >= 20) {
    n_count = 0;
  }
  return WriteResult(WRITE_STATUS_ERROR, 49);
#else
  int rc;
  do {
    rc = sendmsg(fd, &hdr, 0);
  } while (rc < 0 && errno == EINTR);
  if (rc >= 0) {
    return WriteResult(WRITE_STATUS_OK, rc);
  }
  fprintf(stderr, "%d: fail to send: %s(%d:%s)\n", fd, strerror(errno), errno, 
    nq::Syscall::WriteMayBlocked(errno, reachability_tracked) ? "blocked" : "error");
  return WriteResult(nq::Syscall::WriteMayBlocked(errno, reachability_tracked)
                         ? WRITE_STATUS_BLOCKED
                         : WRITE_STATUS_ERROR,
                     errno);
#endif
}

WriteResult NqPacketWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  DCHECK(!IsWriteBlocked());
  DCHECK(nullptr == options)
      << "QuicDefaultPacketWriter does not accept any options.";
  WriteResult result = WritePacket(fd(), buffer, buf_len,
                                   self_address, peer_address, reachability_tracked_);
  if (result.status == WRITE_STATUS_BLOCKED) {
    set_write_blocked(true);
  }
  return result;
}
}
