#include "interop/naquid_stream.h"

#include "core/length_codec.h"
#include "interop/naquid_loop.h"
#include "interop/naquid_session.h"

namespace net {

NaquidStream::NaquidStream(QuicStreamId id, NaquidSession* nq_session, bool establish_side) : 
  QuicStream(id, nq_session), handler_(nullptr), establish_side_(establish_side) {
}
NaquidSession *NaquidStream::nq_session() { 
  return static_cast<NaquidSession *>(session()); 
}
bool NaquidStream::set_protocol(const std::string &name) {
  if (!establish_side()) {
    ASSERT(false); //should be establish side
    return false; 
  }
  buffer_ = name;
  handler_ = std::unique_ptr<NaquidStreamHandler>(CreateStreamHandler(name));
  return handler_ != nullptr;
}
NaquidStreamHandler *NaquidStream::CreateStreamHandler(const std::string &name) {
  auto he = nq_session()->handler_map()->Find(name);
  if (he == nullptr) {
    ASSERT(false);
    return nullptr;
  }
  NaquidStreamHandler *s;
  switch (he->type) {
  case nq::HandlerMap::FACTORY: {
    s = (NaquidStreamHandler *)nq_closure_call(he->factory, create_stream, nq_session()->conn());
  } break;
  case nq::HandlerMap::STREAM: {
    if (nq_closure_is_empty(he->stream.stream_reader)) {
      s = new NaquidSimpleStreamHandler(this, he->stream.on_stream_record);
    } else {
      s = new NaquidRawStreamHandler(this, he->stream.on_stream_record, 
                                          he->stream.stream_reader, 
                                          he->stream.stream_writer); 
    }
    s->SetLifeCycleCallback(he->stream.on_stream_open, he->stream.on_stream_close);
  } break;
  case nq::HandlerMap::RPC: {
    s = new NaquidSimpleRPCStreamHandler(this, he->rpc.on_rpc_notify);
    s->SetLifeCycleCallback(he->rpc.on_stream_open, he->rpc.on_stream_close);
  } break;
  default:
    ASSERT(false);
    return nullptr;
  }
  return s;
}
void NaquidStream::Disconnect() {
  WriteOrBufferData(QuicStringPiece(), true, nullptr);
}
void NaquidStream::OnClose() {
  handler_->OnClose();
}
void NaquidStream::OnDataAvailable() {
  //greedy read and called back
  struct iovec v[256];
  int n_blocks = sequencer()->GetReadableRegions(v, 256);
  int i = 0;
  if (handler_ == nullptr && !establish_side()) {
    //establishment
    for (;i < n_blocks; i++) {
      buffer_.append(NaquidStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
      size_t idx = buffer_.find('\0');
      if (idx == std::string::npos) {
        continue; //not yet established
      }
      //create handler by initial establish string
      auto name = buffer_.substr(0, idx);
      handler_ = std::unique_ptr<NaquidStreamHandler>(CreateStreamHandler(name));
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
    handler_->OnRecv(NaquidStreamHandler::ToCStr(v[i].iov_base), v[i].iov_len);
  }
}



constexpr size_t len_buff_len = nq::LengthCodec::EncodeLength(sizeof(nq_size_t));
void NaquidSimpleStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //greedy read and called back
	parse_buffer_.append(ToCStr(p), len);
	const char *pstr = parse_buffer_.c_str();
	size_t plen = parse_buffer_.length();
	nq_size_t read_ofs, reclen = nq::LengthCodec::Decode(&read_ofs, pstr, plen);
	if (reclen > 0 && (reclen + read_ofs) <= plen) {
	  nq_closure_call(on_recv_, on_stream_record, CastFrom(this), ToCStr(pstr + read_ofs), reclen);
	  parse_buffer_.erase(0, reclen + read_ofs);
	} else if (reclen == 0 && plen > len_buff_len) {
		//broken payload. should resolve payload length
		stream_->Disconnect();
	}
}
void NaquidSimpleStreamHandler::Send(const void *p, nq_size_t len) {
	char len_buff[len_buff_len];
	auto enc_len = nq::LengthCodec::Encode(len, len_buff, sizeof(len_buff));
	WriteBytes(len_buff, enc_len);
	WriteBytes(ToCStr(p), len);
}



void NaquidRawStreamHandler::OnRecv(const void *p, nq_size_t len) {
  int reclen;
  void *rec = nq_closure_call(reader_, stream_reader, ToCStr(p), len, &reclen);
  if (rec != nullptr) {
    nq_closure_call(on_recv_, on_stream_record, CastFrom(this), rec, reclen);
  } else if (reclen < 0) {
    stream_->Disconnect();    
  }
}
  


void NaquidSimpleRPCStreamHandler::EntryRequest(nq_msgid_t msgid, nq_closure_t cb, uint64_t timeout_duration_us) {
  auto req = new Request(this, msgid, cb);
  req_map_[msgid] = req;
  auto alarm = loop_->CreateAlarm(req);
  alarm->Set(NaquidLoop::ToQuicTime(loop_->NowInUsec() + timeout_duration_us));
}

void NaquidSimpleRPCStreamHandler::OnRecv(const void *p, nq_size_t len) {
  //greedy read and called back
  parse_buffer_.append(ToCStr(p), len);
  const char *pstr = parse_buffer_.c_str();
  size_t plen = parse_buffer_.length();
  nq_size_t read_ofs, reclen = nq::LengthCodec::Decode(&read_ofs, pstr, plen);
  if (reclen > 0 && (reclen + read_ofs) <= len) {
    pstr += read_ofs;
    nq_msgid_t msgid = nq::Syscall::NetbytesToHost16(pstr + 2);
    auto it = req_map_.find(msgid);
    if (it != req_map_.end()) {
      nq_closure_call(it->second->on_data_, on_rpc_result, CastFrom(this), ToPV(pstr + 4), reclen - 4);
    } else if (msgid == 0) {
      //notification
      nq_closure_call(on_recv_, on_rpc_notify, CastFrom(this), nq::Syscall::NetbytesToHost16(pstr), ToPV(pstr + 4), reclen - 4);
    } else {
      //probably timedout
    }
    parse_buffer_.erase(0, reclen + read_ofs);
  } else if (reclen == 0 && len > len_buff_len) {
    //broken payload. should resolve payload length
    stream_->Disconnect();
  }
}
void NaquidSimpleRPCStreamHandler::Send(uint16_t type, const void *p, nq_size_t len) {
  //pack and send buffer
  char len_buff[len_buff_len];
  auto enc_len = nq::LengthCodec::Encode(len + 4, len_buff, sizeof(len_buff));
  WriteBytes(len_buff, enc_len);
  uint16_t ntype = nq::Syscall::HostToNet(type), nmsgid = 0;
  WriteBytes((char *)&ntype, 2);
  WriteBytes((char *)&nmsgid, 2);
  WriteBytes(ToCStr(p), len);  
}
void NaquidSimpleRPCStreamHandler::Send(uint16_t type, const void *p, nq_size_t len, nq_closure_t cb) {
  nq_msgid_t msgid = msgid_factory_.New();
  //pack and send buffer
  char len_buff[len_buff_len];
  auto enc_len = nq::LengthCodec::Encode(len + 4, len_buff, sizeof(len_buff));
  WriteBytes(len_buff, enc_len);
  uint16_t ntype = nq::Syscall::HostToNet(type), nmsgid = nq::Syscall::HostToNet(msgid);
  WriteBytes((char *)&ntype, 2);
  WriteBytes((char *)&nmsgid, 2);
  WriteBytes(ToCStr(p), len);

  EntryRequest(msgid, cb);
}


} //net
