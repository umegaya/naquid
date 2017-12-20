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
NqSessionIndex NqStream::session_index() const { 
  return NqStreamSerialCodec::IsClient(stream_serial_) ? 
    NqStreamSerialCodec::ClientSessionIndex(stream_serial_) : 
    NqStreamSerialCodec::ServerSessionIndex(stream_serial_); 
}
nq_conn_t NqStream::conn() { 
  return nq_session()->conn(); 
}
bool NqStream::OpenHandler(const std::string &name) {
  if (handler_ != nullptr) {
    return establish_side(); //because non establish side never call this twice.
  }
  handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(name));
  if (handler_ == nullptr) {
    ASSERT(false);
    return false;
  }
  buffer_ = name;
  buffer_.append(1, '\0');
  if (!handler_->OnOpen()) {
    buffer_ = "";
    return false;
  }
  return true;
}
NqLoop *NqStream::GetLoop() { 
  return nq_session()->delegate()->GetLoop(); 
}
nq_alarm_t NqStream::NewAlarm() {
  NqAlarm *a = GetBoxer()->NewAlarm();
  TRACE("NqStream::NewAlram %p", a);
  return {
    .p = a,
    .s = a->alarm_serial(),
  };
}
NqStreamHandler *NqStream::CreateStreamHandler(const std::string &name) {
  auto he = nq_session()->handler_map()->Find(name);
  if (he == nullptr) {
    ASSERT(false);
    return nullptr;
  }
  NqStreamHandler *s;
  switch (he->type) {
  case nq::HandlerMap::FACTORY: {
    s = (NqStreamHandler *)nq_closure_call(he->factory, create_stream, nq_session()->conn());
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
  //WriteOrBufferData(QuicStringPiece(), true, nullptr);
  //CloseReadSide(); //prevent further receiving packet
  nq_session()->CloseStream(id());
}
void NqStream::OnClose() {
  TRACE("NqStream::OnClose %p, %llx(%lu)", this, stream_serial_.load(), id());
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
  int i = 0;
  if (!established_) {
    //establishment
    if (establish_side()) {
      established_ = true;
    } else {
      for (;i < n_blocks;) {
        buffer_.append(NqStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
        sequencer()->MarkConsumed(v[i].iov_len);
        i++;
        size_t idx = buffer_.find('\0');
        if (idx == std::string::npos) {
          continue; //not yet established
        }
        //prevent send handshake message to client
        set_proto_sent();
        //create handler by initial establish string
        auto name = buffer_.substr(0, idx);
        if (!OpenHandler(name)) {
          Disconnect();
          return;
        }
        if (buffer_.length() > (idx + 1)) {
          //parse_buffer may contains over-received payload
          handler_->OnRecv(buffer_.c_str() + idx + 1, buffer_.length() - idx - 1);
        }
        established_ = true;
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



NqBoxer *NqClientStream::GetBoxer() {
  return static_cast<NqBoxer *>(static_cast<NqClientLoop *>(stream_allocator()));
}
void NqClientStream::InitSerial() { 
  stream_ptr_ = nq_session()->delegate();
  auto session_serial = nq_session()->delegate()->SessionSerial();
  ASSERT(NqConnSerialCodec::IsClient(session_serial));
  stream_serial_ =  
    NqStreamSerialCodec::ClientEncode(
      NqConnSerialCodec::ClientSessionIndex(session_serial), 
      stream_index_ //need to give stable stream index instead of stream_id, which will change on reconnection.
    ); 
  TRACE("NqClientStream: serial = %llx", stream_serial_.load());
}
void NqClientStream::OnClose() {
  ASSERT(nq_session()->delegate()->IsClient());
  //to initiate new stream creation by sending packet, remove session pointer first
  auto c = static_cast<NqClient *>(nq_session()->delegate());
  c->stream_manager().OnClose(this);
  NqStream::OnClose();
  InvalidateSerial();
}
void **NqClientStream::ContextBuffer() {
  ASSERT(nq_session()->delegate()->IsClient());
  auto c = static_cast<NqClient *>(nq_session()->delegate());  
  auto serial = stream_serial();
  return c->stream_manager().FindContextBuffer(
    NqStreamSerialCodec::ClientStreamIndex(serial));
}
const std::string &NqClientStream::Protocol() const {
  auto c = static_cast<const NqClient *>(nq_session()->delegate());  
  return c->stream_manager().FindStreamName(
    NqStreamSerialCodec::ClientStreamIndex(stream_serial()));
}




NqBoxer *NqServerStream::GetBoxer() {
  return static_cast<NqBoxer *>(static_cast<NqDispatcher *>(stream_allocator()));
}
void NqServerStream::InitSerial(NqStreamIndex idx) {
  stream_ptr_ = nq_session()->delegate();
  auto session_serial = nq_session()->delegate()->SessionSerial();
  ASSERT(!NqConnSerialCodec::IsClient(session_serial));
  stream_serial_ = NqStreamSerialCodec::ServerEncode(
    NqConnSerialCodec::ServerSessionIndex(session_serial), 
    idx, 
    NqConnSerialCodec::ServerWorkerIndex(session_serial)
  );   
  TRACE("NqServerStream: serial = %llx", stream_serial_.load());
}
void NqServerStream::OnClose() {
  ASSERT(!nq_session()->delegate()->IsClient());
  NqStream::OnClose();
  InvalidateSerial();
}



void NqStreamHandler::WriteBytes(const char *p, nq_size_t len) {
  stream_->SendHandshake();
  stream_->WriteOrBufferData(QuicStringPiece(p, len), false, nullptr);
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
	char buffer[len_buff_len + len];
	auto enc_len = nq::LengthCodec::Encode(len, buffer, sizeof(buffer));
  memcpy(buffer + enc_len, p, len);
	WriteBytes(buffer, enc_len + len);
}



void NqRawStreamHandler::OnRecv(const void *p, nq_size_t len) {
  int reclen;
  void *rec = nq_closure_call(reader_, stream_reader, ToCStr(p), len, &reclen);
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
  TRACE("EntryRequest timeout %llu ns", timeout_duration_ts);
  req->Start(loop_, now + timeout_duration_ts);
}
void NqSimpleRPCStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //fprintf(stderr, "stream %llx handler OnRecv %u bytes\n", stream_->stream_serial(), len);
  //greedy read and called back
  parse_buffer_.append(ToCStr(p), len);
  //prepare tmp variables
  const char *pstr = parse_buffer_.c_str();
  size_t plen = parse_buffer_.length(), read_ofs;
  int16_t type; nq_msgid_t msgid; nq_size_t reclen;
  do {
    //decode header
    read_ofs = nq::HeaderCodec::Decode(&type, &msgid, pstr, plen);
    /* tmp_ofs => length of encoded header, reclen => actual payload length */
    auto tmp_ofs = nq::LengthCodec::Decode(&reclen, pstr + read_ofs, plen - read_ofs);
    if (tmp_ofs == 0) { break; }
    read_ofs += tmp_ofs;
    if ((read_ofs + reclen) > plen) {
      //TRACE("short of buffer %u %u %u", reclen, read_ofs, plen);
      break;
    }
    //TRACE("msgid, type = %u %d", msgid, type);
    /*
      type > 0 && msgid != 0 => request
      type <= 0 && msgid != 0 => reply
      type > 0 && msgid == 0 => notify
    */
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
        fprintf(stderr, "stream handler request: idx %u %llu\n", 
          nq::Endian::NetbytesToHost<uint32_t>(pstr), 
          nq::Endian::NetbytesToHost<uint64_t>(pstr + 4));
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

void NqSimpleRPCStreamHandler::Reply(nq_result_t result, nq_msgid_t msgid, const void *p, nq_size_t len) {
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
