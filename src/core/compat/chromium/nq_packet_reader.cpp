// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/compat/chromium/nq_packet_reader.h"

#include <errno.h>
#ifndef __APPLE__
// This is a GNU header that is not present in /usr/include on MacOS
#include <features.h>
#endif
#include <string.h>

#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"
#include "net/tools/quic/quic_dispatcher.h"
#include "net/tools/quic/quic_process_packet_interface.h"

#include "basis/syscall.h"
#include "basis/logger.h"

#include "core/compat/nq_quic_types.h"

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

namespace net {

NqPacketReader::Packet::Packet(const char* buffer,
                                   size_t length,
                                   QuicTime receipt_time,
                                   int ttl,
                                   bool ttl_valid, 
                                   struct sockaddr_storage client_sockaddr, 
                                   QuicIpAddress &server_ip, int server_port) : 
                                  QuicReceivedPacket(buffer, length, receipt_time, false, ttl, ttl_valid), 
                                  client_address_(client_sockaddr), server_address_(server_ip, server_port) {

}

NqPacketReader::NqPacketReader() {
  Initialize();
}
NqPacketReader::~NqPacketReader() {}

void NqPacketReader::Initialize() {
#if MMSG_MORE
  // Zero initialize uninitialized memory.
  memset(mmsg_hdr_, 0, sizeof(mmsg_hdr_));

  for (int i = 0; i < kNumPacketsPerReadMmsgCall; ++i) {
    packets_[i].buf = NewBuffer();
    packets_[i].iov.iov_base = packets_[i].buf;
    packets_[i].iov.iov_len = kMaxPacketSize;
    
    msghdr* hdr = &mmsg_hdr_[i].msg_hdr;
    hdr->msg_name = &packets_[i].raw_address;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    hdr->msg_iov = &packets_[i].iov;
    hdr->msg_iovlen = 1;

    hdr->msg_control = packets_[i].cbuf;
    hdr->msg_controllen = QuicSocketUtils::kSpaceForCmsg;
  }
  last_packets_read_ = 0;
#endif
}
bool NqPacketReader::Read(
    int fd,
    int port,
    const QuicClock& clock,
    Delegate *delegate,
    QuicPacketCount* packets_dropped) {
#if MMSG_MORE
  return ReadPacketsMulti(fd, port, clock, delegate, packets_dropped);
#else
  return ReadPackets(fd, port, clock, delegate,
                                     packets_dropped);
#endif
}
bool NqPacketReader::ReadPacketsMulti(
    int fd,
    int port,
    const QuicClock& clock,
    Delegate *delegate,
    QuicPacketCount* packets_dropped) {
#if MMSG_MORE
  // Re-set the length fields in case recvmmsg has changed them.
  for (int i = 0; i < last_packets_read_; ++i) {
    DCHECK_EQ(kMaxPacketSize, packets_[i].iov.iov_len);
    auto buf = NewBuffer();
    packets_[i].buf = buf;
    packets_[i].iov.iov_base = buf;
    msghdr* hdr = &mmsg_hdr_[i].msg_hdr;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    DCHECK_EQ(static_cast<size_t>(1), hdr->msg_iovlen);
    hdr->msg_controllen = QuicSocketUtils::kSpaceForCmsg;
  }

  last_packets_read_ = recvmmsg(fd, mmsg_hdr_, kNumPacketsPerReadMmsgCall, 0, nullptr);
  if (last_packets_read_ <= 0) {
    return false;  // recvmmsg failed
  }
  //printf("last_packets_read_ %d\n", last_packets_read_);

  QuicWallTime fallback_walltimestamp = QuicWallTime::Zero();
  for (int i = 0; i < last_packets_read_; ++i) {
    if (mmsg_hdr_[i].msg_len == 0) {
      continue;
    }

    if (mmsg_hdr_[i].msg_hdr.msg_controllen >= QuicSocketUtils::kSpaceForCmsg) {
      QUIC_BUG << "Incorrectly set control length: "
               << mmsg_hdr_[i].msg_hdr.msg_controllen << ", expected "
               << QuicSocketUtils::kSpaceForCmsg;
      continue;
    }

    QuicIpAddress server_ip;
    QuicWallTime packet_walltimestamp = QuicWallTime::Zero();
    QuicSocketUtils::GetAddressAndTimestampFromMsghdr(
        &mmsg_hdr_[i].msg_hdr, &server_ip, &packet_walltimestamp);
    if (!server_ip.IsInitialized()) {
      QUIC_BUG << "Unable to get server address.";
      continue;
    }

    // This isn't particularly desirable, but not all platforms support socket
    // timestamping.
    if (packet_walltimestamp.IsZero()) {
      if (fallback_walltimestamp.IsZero()) {
        fallback_walltimestamp = QuicWallTime::FromUNIXMicroseconds((clock.Now() - QuicTime::Zero()).ToMicroseconds());
      }
      packet_walltimestamp = fallback_walltimestamp;
    }
    QuicTime timestamp = clock.ConvertWallTimeToQuicTime(packet_walltimestamp);
    int ttl = 0;
    bool has_ttl = QuicSocketUtils::GetTtlFromMsghdr(&mmsg_hdr_[i].msg_hdr, &ttl);
    auto packet = NewPacket(packets_[i].buf,
                              mmsg_hdr_[i].msg_len, timestamp, ttl,
                              has_ttl, packets_[i].raw_address, server_ip, port);
    packet->set_port(port);
    delegate->OnRecv(packet);
  }

  if (packets_dropped != nullptr) {
    QuicSocketUtils::GetOverflowFromMsghdr(&mmsg_hdr_[0].msg_hdr,
                                           packets_dropped);
  }

  // We may not have read all of the packets available on the socket.
  return last_packets_read_ == kNumPacketsPerReadMmsgCall;
#else
  QUIC_LOG(FATAL) << "Unsupported";
  return false;
#endif
}
bool NqPacketReader::ReadPackets(
    int fd,
    int port,
    const QuicClock& clock,
    Delegate *delegate, 
    QuicPacketCount* packets_dropped) {
  char *buf = NewBuffer();

  NqQuicSocketAddress client_address;
  QuicIpAddress server_ip;
  QuicWallTime walltimestamp = QuicWallTime::Zero();
  int bytes_read =
      QuicSocketUtils::ReadPacket(fd, buf, kMaxPacketSize, packets_dropped,
                                  &server_ip, &walltimestamp, &client_address);
  if (bytes_read < 0) {
    buffer_pool_.push(buf);
    return false;  // ReadPacket failed.
  }

  //TRACE("Read %u bytes from %d, my ip: %s", bytes_read, fd, server_ip.ToString().c_str());

  if (!server_ip.IsInitialized()) {
    QUIC_BUG << "Unable to get server address.";
    buffer_pool_.push(buf);
    return false;
  }
  // This isn't particularly desirable, but not all platforms support socket
  // timestamping.
  if (walltimestamp.IsZero()) {
    walltimestamp = QuicWallTime::FromUNIXMicroseconds((clock.Now() - QuicTime::Zero()).ToMicroseconds());
  }
  QuicTime timestamp = clock.ConvertWallTimeToQuicTime(walltimestamp);
  auto packet = NewPacket(buf, bytes_read, timestamp, 0, false,
                          client_address.generic_address(), server_ip, port);

  packet->set_port(port);
  delegate->OnRecv(packet);
  // The socket read was successful, so return true even if packet dispatch
  // failed.
  return true;
}


}  // namespace net
