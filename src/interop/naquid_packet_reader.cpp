// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_packet_reader.h"

#include <errno.h>
#ifndef __APPLE__
// This is a GNU header that is not present in /usr/include on MacOS
#include <features.h>
#endif
#include <string.h>

#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"
#include "net/tools/quic/quic_dispatcher.h"
#include "net/tools/quic/quic_process_packet_interface.h"

#include "core/syscall.h"

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

namespace net {

NaquidPacketReader::Packet::Packet(const char* buffer,
                                   size_t length,
                                   QuicTime receipt_time,
                                   int ttl,
                                   bool ttl_valid, 
                                   struct sockaddr_storage client_sockaddr, 
                                   QuicIpAddress &server_ip, int server_port) : 
                                  QuicReceivedPacket(buffer, length, receipt_time, false, ttl, ttl_valid), 
                                  client_address_(client_sockaddr), server_address(server_ip, server_port) {

}

NaquidPacketReader::NaquidPacketReader() {
  Initialize();
}

void NaquidPacketReader::Initialize() {
#if MMSG_MORE
  // Zero initialize uninitialized memory.
  memset(mmsg_hdr_, 0, sizeof(mmsg_hdr_));

  for (int i = 0; i < kNumPacketsPerReadMmsgCall; ++i) {
    packets_[i].buf = reader_.NewBuffer();
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
#endif
}

NaquidPacketReader::~NaquidPacketReader() {}

int NaquidPacketReader::ReadAndDispatchPackets(
    int fd,
    int port,
    const QuicClock& clock,
    QuicPacketCount* packets_dropped) {
#if MMSG_MORE
  return ReadPacketsMulti(fd, port, clock, packets_dropped);
#else
  return ReadPackets(fd, port, clock, delegate_,
                                     packets_dropped);
#endif
}

bool NaquidPacketReader::ReadPacketsMulti(
    int fd,
    int port,
    const QuicClock& clock,
    QuicPacketCount* packets_dropped) {
#if MMSG_MORE
  // Re-set the length fields in case recvmmsg has changed them.
  for (int i = 0; i < kNumPacketsPerReadMmsgCall; ++i) {
    DCHECK_EQ(kMaxPacketSize, packets_[i].iov.iov_len);
    if (packets_[i].buf == nullptr) { //if packet send to queue or consumed, assign new one.
      packets_[i].buf = reader_.NewBuffer();
      packets_[i].iov.iov_base = p->buffer();
      packets_[i].packet.reset(p);
    }
    msghdr* hdr = &mmsg_hdr_[i].msg_hdr;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    DCHECK_EQ(1, hdr->msg_iovlen);
    hdr->msg_controllen = QuicSocketUtils::kSpaceForCmsg;
  }

  int packets_read = recvmmsg(fd, mmsg_hdr_, kNumPacketsPerReadMmsgCall, 0, nullptr);
  if (packets_read <= 0) {
    return false;  // recvmmsg failed
  }

  QuicWallTime fallback_walltimestamp = QuicWallTime::Zero();
  for (int i = 0; i < packets_read; ++i) {
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
        fallback_walltimestamp = clock.WallNow();
      }
      packet_walltimestamp = fallback_walltimestamp;
    }
    QuicTime timestamp = clock.ConvertWallTimeToQuicTime(packet_walltimestamp);
    int ttl = 0;
    bool has_ttl = QuicSocketUtils::GetTtlFromMsghdr(&mmsg_hdr_[i].msg_hdr, &ttl);
    auto packet = NewPacket(packets_[i].buf.release(),
                              mmsg_hdr_[i].msg_len, timestamp, false, ttl,
                              has_ttl, packets_[i].raw_address, server_ip, port);
    packet->port_ = port;
    delegate_->Process(packet);
  }

  if (packets_dropped != nullptr) {
    QuicSocketUtils::GetOverflowFromMsghdr(&mmsg_hdr_[0].msg_hdr,
                                           packets_dropped);
  }

  // We may not have read all of the packets available on the socket.
  return packets_read == kNumPacketsPerReadMmsgCall;
#else
  QUIC_LOG(FATAL) << "Unsupported";
  return false;
#endif
}

/* static */
bool NaquidPacketReader::ReadAndDispatchSinglePacket(
    int fd,
    int port,
    const QuicClock& clock,
    Delegate *delegate, 
    QuicPacketCount* packets_dropped) {
  char buf[kMaxPacketSize];

  QuicSocketAddress client_address;
  QuicIpAddress server_ip;
  QuicWallTime walltimestamp = QuicWallTime::Zero();
  int bytes_read =
      QuicSocketUtils::ReadPacket(fd, buf, arraysize(buf), packets_dropped,
                                  &server_ip, &walltimestamp, &client_address);
  if (bytes_read < 0) {
    return false;  // ReadPacket failed.
  }

  if (!server_ip.IsInitialized()) {
    QUIC_BUG << "Unable to get server address.";
    return false;
  }
  // This isn't particularly desirable, but not all platforms support socket
  // timestamping.
  if (walltimestamp.IsZero()) {
    walltimestamp = clock.WallNow();
  }
  QuicTime timestamp = clock.ConvertWallTimeToQuicTime(walltimestamp);

  QuicReceivedPacket packet(buf, bytes_read, timestamp, false);
  QuicSocketAddress server_address(server_ip, port);
  processor->ProcessPacket(server_address, client_address, packet);

  // The socket read was successful, so return true even if packet dispatch
  // failed.
  return true;
}


}  // namespace net
