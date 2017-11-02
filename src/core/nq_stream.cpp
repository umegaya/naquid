#include "core/nq_stream.h"

#include "basis/length_codec.h"
#include "basis/endian.h"
#include "core/nq_loop.h"
#include "core/nq_session.h"
#include "core/nq_client.h"

namespace net {

NqStream::NqStream(QuicStreamId id, NqSession* nq_session, 
                   bool establish_side, SpdyPriority priority) : 
  QuicStream(id, nq_session), 
  handler_(nullptr), 
  priority_(priority), 
  establish_side_(establish_side) {
  nq_session->RegisterStreamPriority(id, priority);
}
void NqStream::InitHandle() { 
  handle_ = nq_session()->delegate()->GetBoxer()->Box(this); 
}
NqLoop *NqStream::GetLoop() { 
  return nq_session()->delegate()->GetLoop(); 
}
NqSession *NqStream::nq_session() { 
  return static_cast<NqSession *>(session()); 
}
const NqSession *NqStream::nq_session() const { 
  return static_cast<const NqSession *>(session()); 
}
NqSessionIndex NqStream::session_index() const { 
  return nq_session()->delegate()->SessionIndex(); 
}
nq_conn_t NqStream::conn() { 
  return nq_session()->conn(); 
}
bool NqStream::set_protocol(const std::string &name) {
  if (!establish_side()) {
    ASSERT(false); //should be establish side
    return false; 
  }
  buffer_ = name;
  handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(name));
  return handler_ != nullptr;
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
    s = new NqSimpleRPCStreamHandler(this, he->rpc.on_rpc_request, he->rpc.on_rpc_notify);
    s->SetLifeCycleCallback(he->rpc.on_stream_open, he->rpc.on_stream_close);
  } break;
  default:
    ASSERT(false);
    return nullptr;
  }
  return s;
}
void NqStream::Disconnect() {
  WriteOrBufferData(QuicStringPiece(), true, nullptr);
}
void NqStream::OnClose() {
  handler_->OnClose();
}
void NqStream::OnDataAvailable() {
  //greedy read and called back
  struct iovec v[256];
  int n_blocks = sequencer()->GetReadableRegions(v, 256);
  int i = 0;
  if (handler_ == nullptr && !establish_side()) {
    //establishment
    for (;i < n_blocks; i++) {
      buffer_.append(NqStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
      size_t idx = buffer_.find('\0');
      if (idx == std::string::npos) {
        continue; //not yet established
      }
      //create handler by initial establish string
      auto name = buffer_.substr(0, idx);
      handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(name));
      handler_->ProtoSent();
      if (handler_ == nullptr || !handler_->OnOpen()) { //server side OnOpen
        Disconnect();
        return; //broken payload. stream handler does not exists
      }
      if (buffer_.length() > (idx + 1)) {
        //parse_buffer may contains over-received payload
        handler_->OnRecv(buffer_.c_str() + idx + 1, buffer_.length() - idx - 1);
      }
      buffer_ = std::move(name);
      break;
    }
  }
  for (;i < n_blocks; i++) {
    handler_->OnRecv(NqStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
  }
}



void NqClientStream::OnClose() {
  auto c = static_cast<NqClient *>(nq_session()->delegate());
  c->stream_manager().OnClose(this);
  NqStream::OnClose();
}



void NqStreamHandler::WriteBytes(const char *p, nq_size_t len) {
  if (!proto_sent_) { //TODO: use unlikely
    const auto& name = stream_->protocol();
    stream_->WriteOrBufferData(QuicStringPiece(name.c_str(), name.length()), false, nullptr);
    OnOpen(); //client stream side OnOpen
    proto_sent_ = true;
  }
  stream_->WriteOrBufferData(QuicStringPiece(p, len), false, nullptr);
}



constexpr size_t len_buff_len = nq::LengthCodec::EncodeLength(sizeof(nq_size_t));
void NqSimpleStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //greedy read and called back
	parse_buffer_.append(ToCStr(p), len);
	const char *pstr = parse_buffer_.c_str();
	size_t plen = parse_buffer_.length();
	nq_size_t read_ofs, reclen = nq::LengthCodec::Decode(&read_ofs, pstr, plen);
	if (reclen > 0 && (reclen + read_ofs) <= plen) {
	  nq_closure_call(on_recv_, on_stream_record, stream_->ToHandle<nq_stream_t>(), pstr + read_ofs, reclen);
	  parse_buffer_.erase(0, reclen + read_ofs);
	} else if (reclen == 0 && plen > len_buff_len) {
		//broken payload. should resolve payload length
		stream_->Disconnect();
	}
}
void NqSimpleStreamHandler::Send(const void *p, nq_size_t len) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
	char len_buff[len_buff_len];
	auto enc_len = nq::LengthCodec::Encode(len, len_buff, sizeof(len_buff));
	WriteBytes(len_buff, enc_len);
	WriteBytes(ToCStr(p), len);
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
  


void NqSimpleRPCStreamHandler::EntryRequest(nq_msgid_t msgid, nq_closure_t cb, uint64_t timeout_duration_us) {
  auto req = new Request(this, msgid, cb);
  req_map_[msgid] = req;
  auto alarm = loop_->CreateAlarm(req);
  auto now = loop_->NowInUsec();
  alarm->Set(NqLoop::ToQuicTime(now + timeout_duration_us));
  //auto end = (alarm->deadline() - QuicTime::Zero()).ToMicroseconds();
  //TRACE("entry req: start %llu end %llu\n", now, end);
  req->alarm_ = alarm;
}
void NqSimpleRPCStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //greedy read and called back
  parse_buffer_.append(ToCStr(p), len);
  const char *pstr = parse_buffer_.c_str();
  size_t plen = parse_buffer_.length();
  nq_size_t read_ofs, reclen = nq::LengthCodec::Decode(&read_ofs, pstr, plen);
  if (reclen > 0 && (reclen + read_ofs) <= len) {
    /*
      type > 0 && msgid != 0 => request
      type <= 0 && msgid != 0 => reply
      type > 0 && msgid == 0 => notify
    */
    pstr += read_ofs;
    int16_t type = nq::Endian::NetbytesToHostS16(pstr);
    nq_msgid_t msgid = nq::Endian::NetbytesToHost16(pstr + 2);
    if (msgid != 0) {
      if (type <= 0) {
        auto it = req_map_.find(msgid);
        if (it != req_map_.end()) {
          auto req = it->second;
          req_map_.erase(it);
          //reply from serve side
          nq_closure_call(req->on_data_, on_rpc_reply, stream_->ToHandle<nq_rpc_t>(), type, ToPV(pstr + 4), reclen - 4);
          req->alarm_->Cancel(); //cancel firing alarm
          delete req->alarm_; //it deletes req itself
        } else {
          //probably timedout. caller should already be received timeout error
          //req object deleted in OnAlarm
        }
      } else {
        //request
        nq_closure_call(on_request_, on_rpc_request, stream_->ToHandle<nq_rpc_t>(), type, msgid, ToPV(pstr + 4), reclen - 4);
      }
    } else if (type > 0) {
      //notify
      nq_closure_call(on_notify_, on_rpc_notify, stream_->ToHandle<nq_rpc_t>(), type, ToPV(pstr + 4), reclen - 4);
    } else {
      ASSERT(false);
    }
    parse_buffer_.erase(0, reclen + read_ofs);
  } else if (reclen == 0 && len > len_buff_len) {
    //broken payload. should resolve payload length
    stream_->Disconnect();
  }
}
void NqSimpleRPCStreamHandler::Notify(uint16_t type, const void *p, nq_size_t len) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  ASSERT(type > 0);
  //pack and send buffer
  char len_buff[len_buff_len];
  auto enc_len = nq::LengthCodec::Encode(len + 4, len_buff, sizeof(len_buff));
  WriteBytes(len_buff, enc_len);
  uint16_t ntype = nq::Endian::HostToNet(type), nmsgid = 0;
  WriteBytes((char *)&ntype, 2);
  WriteBytes((char *)&nmsgid, 2);
  WriteBytes(ToCStr(p), len);  
}
void NqSimpleRPCStreamHandler::Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  ASSERT(type > 0);
  nq_msgid_t msgid = msgid_factory_.New();
  //pack and send buffer
  char len_buff[len_buff_len];
  auto enc_len = nq::LengthCodec::Encode(len + 4, len_buff, sizeof(len_buff));
  WriteBytes(len_buff, enc_len);
  uint16_t ntype = nq::Endian::HostToNet(type), nmsgid = nq::Endian::HostToNet(msgid);
  WriteBytes((char *)&ntype, 2);
  WriteBytes((char *)&nmsgid, 2);
  WriteBytes(ToCStr(p), len);

  EntryRequest(msgid, cb);
}
void NqSimpleRPCStreamHandler::Reply(nq_result_t result, nq_msgid_t msgid, const void *p, nq_size_t len) {
  QuicConnection::ScopedPacketBundler bundler(
    nq_session()->connection(), QuicConnection::SEND_ACK_IF_QUEUED);
  ASSERT(result <= 0);
  //pack and send buffer
  char len_buff[len_buff_len];
  auto enc_len = nq::LengthCodec::Encode(len + 4, len_buff, sizeof(len_buff));
  WriteBytes(len_buff, enc_len);
  uint16_t nresult = nq::Endian::HostToNet(result), nmsgid = nq::Endian::HostToNet(msgid);
  WriteBytes((char *)&nresult, 2);
  WriteBytes((char *)&nmsgid, 2);
  WriteBytes(ToCStr(p), len);
}



} //net
