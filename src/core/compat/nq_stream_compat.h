#pragma once

#include <string>

#include "basis/header_codec.h"
#include "basis/id_factory.h"
#include "basis/timespec.h"
#include "core/compat/nq_quic_types.h"
#include "core/nq_closure.h"
#include "core/nq_loop.h"
#include "core/nq_alarm.h"
#include "core/nq_serial_codec.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_stream.h"
#include "net/quic/core/quic_alarm.h"
#include "net/quic/core/quic_spdy_stream.h"
namespace net {
class NqSession;
class NqStreamCompat : public QuicStream {
 protected:
  class AckHandler : public QuicAckListenerInterface {
    nq_stream_opt_t opt_;
  public:
    AckHandler(const nq_stream_opt_t &opt) : opt_(opt) {}
    //implements QuicAckListenerInterface

    // Called when a packet is acked.  Called once per packet.
    // |acked_bytes| is the number of data bytes acked.
    void OnPacketAcked(int acked_bytes, QuicTime::Delta ack_delay_time) override {
      if (nq_closure_is_empty(opt_.on_ack)) { return; }
      nq_closure_call(opt_.on_ack, acked_bytes, nq_time_usec(ack_delay_time.ToMicroseconds()));
    }
    // Called when a packet is retransmitted.  Called once per packet.
    // |retransmitted_bytes| is the number of data bytes retransmitted.
    void OnPacketRetransmitted(int retransmitted_bytes) override {
      if (nq_closure_is_empty(opt_.on_retransmit)) { return; }
      nq_closure_call(opt_.on_retransmit, retransmitted_bytes);
    }
  };
 public:
  NqStreamCompat(NqQuicStreamId id,
                 NqSession* nq_session,
                 SpdyPriority priority = kDefaultPriority);

  //get/set
  NqSession *nq_session();
  const NqSession *nq_session() const;

  //operation
  void Send(const char *p, nq_size_t len, bool fin, const nq_stream_opt_t *p_opt);

  //implements QuicStream
  void OnDataAvailable() override;
  //OnClose implemented at NqStream

  // NqStreamCompat interface
  virtual bool OnRecv(const void *p, nq_size_t len) = 0;
  //also required OnClose, which is defined as non-abstract function `QuicStream::OnClose`
};
}  //namespace net
#else
namespace net {
class NqSession;
class NqStreamCompat {
 private:
  NqQuicStreamId id_;
  NqSession *session_;
 public:
  NqStreamCompat(NqQuicStreamId id, 
                 NqSession* nq_session)
    : id_(id), session_(nq_session) {}

  //get/set
  NqSession *nq_session() { return session_; }
  const NqSession *nq_session() const { return session_; }

  //operation
  void Send(const char *p, nq_size_t len, bool fin, const nq_stream_opt_t *p_opt);

  // NqStreamCompat interface
  virtual void OnClose() = 0;
  virtual bool OnRecv(const void *p, nq_size_t len) = 0;
};
} //namespace net
#endif
