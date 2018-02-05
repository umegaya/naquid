#include "core/nq_stream.h"

#include "basis/endian.h"
#include "core/nq_loop.h"
#include "core/nq_session.h"
#include "core/nq_client.h"
#include "core/nq_server_session.h"
#include "core/nq_client_loop.h"
#include "core/nq_dispatcher.h"
#include "core/nq_unwrapper.h"

namespace net {

NqStream::NqStream(QuicStreamId id, NqSession* nq_session, 
                   bool establish_side, SpdyPriority priority) : 
  QuicStream(id, nq_session), 
  buffer_(),
  handler_(nullptr), 
  priority_(priority), 
  establish_side_(establish_side),
  established_(false), proto_sent_(false) {
  nq_session->RegisterStreamPriority(id, priority);
}
NqSession *NqStream::nq_session() { 
  return static_cast<NqSession *>(session()); 
}
const NqSession *NqStream::nq_session() const { 
  return static_cast<const NqSession *>(session()); 
}
bool NqStream::TryOpenRawHandler(bool *p_on_open_result) {
  auto rh = nq_session()->handler_map()->RawHandler();
  if (rh != nullptr) {
    handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(rh));
    proto_sent_ = true;
    established_ = true;
    *p_on_open_result = handler_->OnOpen();
    TRACE("TryOpenRawHandler: established!");
    return true;
  }
  return false;
}
bool NqStream::OpenHandler(const std::string &name, bool update_buffer_with_name) {
  if (handler_ != nullptr) {
    return establish_side(); //because non establish side never call this twice.
  }
  bool on_open_result;
  if (TryOpenRawHandler(&on_open_result)) {
    //should open raw handler but OnOpen fails. 
    //caller behave same as usual handler_->OnOpen failure
    return on_open_result;
  }
  //FYI(iyatomi): name.c_str() will create new string without null terminate
  handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(name.c_str()));
  if (handler_ == nullptr) {
    ASSERT(false);
    return false;
  }
  if (update_buffer_with_name) {
    buffer_ = name;
    buffer_.append(1, '\0');
  }
  if (!handler_->OnOpen()) {
    buffer_ = "";
    return false;
  }
  return true;
}
NqLoop *NqStream::GetLoop() { 
  return nq_session()->delegate()->GetLoop(); 
}
NqStreamHandler *NqStream::CreateStreamHandler(const std::string &name) {
  auto he = nq_session()->handler_map()->Find(name);
  if (he == nullptr) {
    ASSERT(false);
    return nullptr;
  }
  return CreateStreamHandler(he);
}
NqStreamHandler *NqStream::CreateStreamHandler(const nq::HandlerMap::HandlerEntry *he) {
  NqStreamHandler *s;
  switch (he->type) {
  case nq::HandlerMap::FACTORY: {
    s = (NqStreamHandler *)nq_closure_call(he->factory, create_stream, nq_session()->ToHandle());
  } break;
  case nq::HandlerMap::STREAM: {
    if (nq_closure_is_empty(he->stream.stream_reader)) {
      s = new NqSimpleStreamHandler(this, he->stream.on_stream_record);
    } else {
      s = new NqRawStreamHandler(this, he->stream.on_stream_record, 
                                 he->stream.stream_reader, 
                                 he->stream.stream_writer); 
    }
    s->SetLifeCycleCallback(he->stream.on_stream_open, he->stream.on_stream_close);
  } break;
  case nq::HandlerMap::RPC: {
    s = new NqSimpleRPCStreamHandler(this, he->rpc.on_rpc_request, 
                                     he->rpc.on_rpc_notify, 
                                    he->rpc.timeout,
                                    he->rpc.use_large_msgid);
    s->SetLifeCycleCallback(he->rpc.on_rpc_open, he->rpc.on_rpc_close);
  } break;
  default:
    ASSERT(false);
    return nullptr;
  }
  return s;
}
void NqStream::Disconnect() {
  //auto b = NqUnwrapper::UnwrapBoxer(NqStreamSerialCodec::IsClient(stream_serial_), nq_session());
  //b->InvokeStream(stream_serial_, NqBoxer::OpCode::Disconnect, nullptr, nq_session());
  nq_session()->CloseStream(id());
}
void NqStream::OnClose() {
  TRACE("NqStream::OnClose %p, %p, %s(%lu)", this, nq_session()->delegate(), stream_serial_.Dump().c_str(), id());
  if (handler_ != nullptr) {
    handler_->OnClose();
  }
  QuicStream::OnClose();
}
void NqStream::OnDataAvailable() {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  //greedy read and called back
  struct iovec v[256];
  int n_blocks = sequencer()->GetReadableRegions(v, 256);
  int i = 0; bool on_raw_open_result;
  if (!established_) {
    //establishment
    if (establish_side()) {
      established_ = true;
    } else if (TryOpenRawHandler(&on_raw_open_result)) {
      if (!on_raw_open_result) {
        Disconnect();
        return;
      }
      set_proto_sent();
      established_ = true;
    } else {
      for (;i < n_blocks;) {
        const char *vbuf = NqStreamHandler::ToCStr(v[i].iov_base);
        size_t idx = 0;
        for (;idx < v[i].iov_len; idx++) {
          if (vbuf[idx] == 0) {
            //FYI(iyatomi): this adds null terminate also.
            buffer_.append(vbuf, idx + 1); 
            break;
          }
        }
        if (idx >= v[i].iov_len) {
          //FYI(iyatomi): entire buffer points part of string.
          buffer_.append(NqStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
          sequencer()->MarkConsumed(v[i].iov_len);
          i++;
          continue;
        }
        //prevent send handshake message to client
        set_proto_sent();
        //create handler by initial establish string
        if (!OpenHandler(buffer_, false)) {
          Disconnect();
          return;
        }
        if (v[i].iov_len > (idx + 1)) {
          //v[i] may contains over-received payload
          handler_->OnRecv(vbuf + idx + 1, v[i].iov_len - idx - 1);
        }
        established_ = true;
        sequencer()->MarkConsumed(v[i].iov_len);
        i++;
        break;
      }
    }
  }
  size_t consumed = 0;
  for (;i < n_blocks; i++) {
    handler_->OnRecv(NqStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
    consumed += v[i].iov_len;
  }
  sequencer()->MarkConsumed(consumed);  
}



NqBoxer *NqClientStream::boxer() {
  return static_cast<NqBoxer *>(static_cast<NqClientLoop *>(stream_allocator()));
}
void NqClientStream::InitSerial(NqStreamIndex idx) { 
  auto session_serial = nq_session()->delegate()->SessionSerial();
  ASSERT(NqSerial::IsClient(session_serial));
  NqStreamSerialCodec::ClientEncode(stream_serial_, idx); 
  TRACE("NqClientStream: serial = %s", stream_serial_.Dump().c_str());
}
void NqClientStream::OnClose() {
  ASSERT(nq_session()->delegate()->IsClient());
  NqStream::OnClose();
  //remove stream entry after handler_->OnClose called. otherwise callback cannot get context_
  auto c = static_cast<NqClient *>(nq_session()->delegate());
  c->stream_manager().OnClose(this);
  InvalidateSerial();
}
void **NqClientStream::ContextBuffer() {
  ASSERT(nq_session()->delegate()->IsClient());
  auto c = static_cast<NqClient *>(nq_session()->delegate());  
  auto serial = stream_serial();
  return c->stream_manager().FindContextBuffer(
    NqStreamSerialCodec::ClientStreamIndex(serial));
}
void *NqClientStream::Context() {
  ASSERT(nq_session()->delegate()->IsClient());
  auto c = static_cast<NqClient *>(nq_session()->delegate());  
  auto serial = stream_serial();
  return c->stream_manager().FindContext(
    NqStreamSerialCodec::ClientStreamIndex(serial));
}
const std::string &NqClientStream::Protocol() const {
  auto c = static_cast<const NqClient *>(nq_session()->delegate());  
  return c->stream_manager().FindStreamName(
    NqStreamSerialCodec::ClientStreamIndex(stream_serial()));
}
std::mutex &NqClientStream::static_mutex() {
  auto cl = static_cast<NqClientLoop *>(stream_allocator());  
  return cl->stream_allocator().Bss(this)->mutex();
}



NqBoxer *NqServerStream::boxer() {
  return static_cast<NqBoxer *>(static_cast<NqDispatcher *>(stream_allocator()));
}
void NqServerStream::InitSerial(NqStreamIndex idx) {
  auto session_serial = nq_session()->delegate()->SessionSerial();
  ASSERT(!NqSerial::IsClient(session_serial));
  NqStreamSerialCodec::ServerEncode(stream_serial_, idx);
}
void NqServerStream::OnClose() {
  ASSERT(!nq_session()->delegate()->IsClient());
  NqStream::OnClose();
  InvalidateSerial();
}
std::mutex &NqServerStream::static_mutex() {
  auto cl = static_cast<NqDispatcher *>(stream_allocator());  
  return cl->stream_allocator().Bss(this)->mutex();
}



class AckHandler : public QuicAckListenerInterface {
  nq_stream_opt_t opt_;
 public:
  AckHandler(const nq_stream_opt_t &opt) : opt_(opt) {}
  //implements QuicAckListenerInterface

  // Called when a packet is acked.  Called once per packet.
  // |acked_bytes| is the number of data bytes acked.
  void OnPacketAcked(int acked_bytes,
                             QuicTime::Delta ack_delay_time) override {
    if (nq_closure_is_empty(opt_.on_ack)) { return; }
    nq_closure_call(opt_.on_ack, on_stream_ack, acked_bytes, nq_time_usec(ack_delay_time.ToMicroseconds()));
  }
  // Called when a packet is retransmitted.  Called once per packet.
  // |retransmitted_bytes| is the number of data bytes retransmitted.
  void OnPacketRetransmitted(int retransmitted_bytes) override {
    if (nq_closure_is_empty(opt_.on_retransmit)) { return; }
    nq_closure_call(opt_.on_retransmit, on_stream_retransmit, retransmitted_bytes);
  }
};
void NqStreamHandler::WriteBytes(const char *p, nq_size_t len) {
  stream_->SendHandshake();
  stream_->WriteOrBufferData(QuicStringPiece(p, len), false, nullptr);
}
void NqStreamHandler::WriteBytes(const char *p, nq_size_t len, const nq_stream_opt_t &opt) {
  stream_->SendHandshake();
  //TODO(iyatomi): do we need common ack_callback, which is applied to all stream bytes sent?
  stream_->WriteOrBufferData(QuicStringPiece(p, len), false, 
    QuicReferenceCountedPointer<QuicAckListenerInterface>(new AckHandler(opt)));
}



void NqSimpleStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //greedy read and called back
	parse_buffer_.append(ToCStr(p), len);
	const char *pstr = parse_buffer_.c_str();
	size_t plen = parse_buffer_.length();
	nq_size_t reclen = 0, read_ofs = nq::LengthCodec::Decode(&reclen, pstr, plen);
	if (reclen > 0 && (reclen + read_ofs) <= plen) {
	  nq_closure_call(on_recv_, on_stream_record, stream_->ToHandle<nq_stream_t>(), pstr + read_ofs, reclen);
	  parse_buffer_.erase(0, reclen + read_ofs);
	} else if (reclen == 0 && plen > len_buff_len) { //TODO(iyatomi): use unlikely
		//broken payload. should resolve payload length
		stream_->Disconnect();
	}
}
void NqSimpleStreamHandler::Send(const void *p, nq_size_t len) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  SendCommon(p, len, nullptr);
}
void NqSimpleStreamHandler::SendEx(const void *p, nq_size_t len, const nq_stream_opt_t &opt) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  SendCommon(p, len, &opt);
}



void NqRawStreamHandler::OnRecv(const void *p, nq_size_t len) {
  int reclen;
  void *rec = nq_closure_call(reader_, stream_reader, stream_->ToHandle<nq_stream_t>(), ToCStr(p), len, &reclen);
  if (rec != nullptr) {
    nq_closure_call(on_recv_, on_stream_record, stream_->ToHandle<nq_stream_t>(), rec, reclen);
  } else if (reclen < 0) {
    stream_->Disconnect();    
  }
}
  


void NqSimpleRPCStreamHandler::EntryRequest(nq_msgid_t msgid, nq_closure_t cb, nq_time_t timeout_duration_ts) {
  auto req = new Request(this, msgid, cb);
  req_map_[msgid] = req;
  auto now = nq_time_now();
  req->Start(loop_, now + timeout_duration_ts);
}
void NqSimpleRPCStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //TRACE("stream %llx handler OnRecv %u bytes", stream_->nq_session()->delegate()->SessionSerial().data[0], len);
  //greedy read and called back
  parse_buffer_.append(ToCStr(p), len);
  //prepare tmp variables
  const char *pstr = parse_buffer_.c_str();
  size_t plen = parse_buffer_.length(), read_ofs;
  int16_t type_tmp; nq_msgid_t msgid; nq_size_t reclen;
  nq_error_t type;
  do {
    //decode header
    read_ofs = nq::HeaderCodec::Decode(&type_tmp, &msgid, pstr, plen);
    /* tmp_ofs => length of encoded header, reclen => actual payload length */
    auto tmp_ofs = nq::LengthCodec::Decode(&reclen, pstr + read_ofs, plen - read_ofs);
    if (tmp_ofs == 0) { break; }
    read_ofs += tmp_ofs;
    if ((read_ofs + reclen) > plen) {
      TRACE("short of buffer %u %zu %zu\n", reclen, read_ofs, plen);
      break;
    }
    //TRACE("sid = %llx, msgid, type = %u %d", stream_->nq_session()->delegate()->SessionSerial(), msgid, type_tmp);
    /*
      type > 0 && msgid != 0 => request
      type <= 0 && msgid != 0 => reply
      type > 0 && msgid == 0 => notify
    */
    type = static_cast<nq_error_t>(type_tmp);
    pstr += read_ofs; //move pointer to top of payload
    if (msgid != 0) {
      if (type <= 0) {
        auto it = req_map_.find(msgid);
        if (it != req_map_.end()) {
          auto req = it->second;
          req_map_.erase(it);
          //reply from serve side
          nq_closure_call(req->on_data_, on_rpc_reply, stream_->ToHandle<nq_rpc_t>(), type, ToPV(pstr), reclen);
          req->Destroy(loop_); //cancel firing alarm and free memory for req
        } else {
          //probably timedout. caller should already be received timeout error
          //req object deleted in OnAlarm
          //TRACE("stream handler reply: msgid not found %u", msgid);
        }
      } else {
        //request
        //fprintf(stderr, "stream handler request: idx %u %llu\n", 
          //nq::Endian::NetbytesToHost<uint32_t>(pstr), 
          //nq::Endian::NetbytesToHost<uint64_t>(pstr + 4));
        nq_closure_call(on_request_, on_rpc_request, stream_->ToHandle<nq_rpc_t>(), type, msgid, ToPV(pstr), reclen);
      }
    } else if (type > 0) {
      //notify
      //TRACE("stream handler notify: type %u", type);
      nq_closure_call(on_notify_, on_rpc_notify, stream_->ToHandle<nq_rpc_t>(), type, ToPV(pstr), reclen);
    } else {
      ASSERT(false);
    }
    parse_buffer_.erase(0, reclen + read_ofs);
    pstr = parse_buffer_.c_str();
    plen = parse_buffer_.length();
  } while (parse_buffer_.length() > 0);
}
void NqSimpleRPCStreamHandler::Notify(uint16_t type, const void *p, nq_size_t len) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  ASSERT(type > 0);
  //pack and send buffer
  char buffer[header_buff_len + len_buff_len + len];
  size_t ofs = 0;
  ofs = nq::HeaderCodec::Encode(static_cast<int16_t>(type), 0, buffer, sizeof(buffer));
  ofs += nq::LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
  memcpy(buffer + ofs, p, len);
  WriteBytes(buffer, ofs + len);  
}
void NqSimpleRPCStreamHandler::Call(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) {
  //QuicConnection::ScopedPacketBundler bundler(
    //nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  nq_msgid_t msgid = msgid_factory_.New();
  SendCommon(type, msgid, p, len);
  EntryRequest(msgid, cb, default_timeout_ts_);
}
void NqSimpleRPCStreamHandler::CallEx(uint16_t type, const void *p, nq_size_t len, nq_rpc_opt_t &opt) {
  //QuicConnection::ScopedPacketBundler bundler(
    //nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  nq_msgid_t msgid = msgid_factory_.New();
  SendCommon(type, msgid, p, len);
  EntryRequest(msgid, opt.callback, opt.timeout);
}

void NqSimpleRPCStreamHandler::Reply(nq_error_t result, nq_msgid_t msgid, const void *p, nq_size_t len) {
  //QuicConnection::ScopedPacketBundler bundler(
    //nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  ASSERT(result <= 0);
  //pack and send buffer
  char buffer[header_buff_len + len_buff_len + len];
  size_t ofs = 0;
  ofs = nq::HeaderCodec::Encode(result, msgid, buffer, sizeof(buffer));
  ofs += nq::LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
  memcpy(buffer + ofs, p, len);
  WriteBytes(buffer, ofs + len);  
}



} //net
