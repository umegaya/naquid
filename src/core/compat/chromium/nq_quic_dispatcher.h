#pragma once

#include "net/tools/quic/quic_dispatcher.h"
#include "core/nq_config.h"
#include "core/nq_server_session.h"
#include "core/compat/chromium/nq_packet_reader.h"

namespace net {
class NqDispatcherCompat;
class NqWorker;
class NqQuicDispatcher : public QuicDispatcher,
                         public QuicCryptoServerStream::Helper {
 public:
  NqQuicDispatcher(
    NqDispatcherCompat &dispatcher, QuicCryptoServerConfig *crypto_config, 
    const NqServerConfig& config, NqWorker &worker
  );

  //get/set
  inline QuicBufferedPacketStore &buffered_packets() { return QuicDispatcher::buffered_packets(); }
  inline NqPacketReader &reader() { return reader_; }
  inline QuicPacketWriter* writer() { return QuicDispatcher::writer(); }

  //operation
  inline QuicPacketWriter* CreatePerConnectionWriterPublic() { return CreatePerConnectionWriter(); }
  const QuicVersionVector& GetSupportedVersionsPublic() { return GetSupportedVersions(); }
  inline NqServerSession *FindByConnectionId(QuicConnectionId cid) {
    auto it = session_map().find(cid);
    if (it != session_map().end()) {
      return static_cast<NqServerSession*>(const_cast<QuicSession *>(it->second.get()));
    }
    return nullptr;
  }
  inline void Process(NqPacket *p) {
    {
      //get NqServerSession's mutex, which is corresponding to this packet's connection id
#if !defined(USE_DIRECT_WRITE)
      //TRACE("packet from %s(%u=>%u)%u", p->client_address().ToString().c_str(), p->reader_index(), index_, p->length());
      ProcessPacket(p->server_address(), p->client_address(), *p);        
#else
      auto cid = p->ConnectionId();
      auto s = FindByConnectionId(cid);
      if (s != nullptr) {
        std::unique_lock<std::mutex> session_lock(s->static_mutex());
        loop_.LockSession(s->session_index());
        ProcessPacket(p->server_address(), p->client_address(), *p);
        loop_.UnlockSession();
      } else {
        ProcessPacket(p->server_address(), p->client_address(), *p);        
      }
#endif
    }
    reader_.Pool(const_cast<char *>(p->data()), p);
  }  
  
  //implements QuicDispatcher
  QuicSession* CreateQuicSession(
    QuicConnectionId connection_id,
    const NqQuicSocketAddress& client_address,
    QuicStringPiece alpn) override;
  void OnConnectionClosed(QuicConnectionId connection_id,
                          QuicErrorCode error,
                          const std::string& error_details) override;

  //implements QuicCryptoServerStream::Helper
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const override;
  // Returns true if |message|, which was received on |self_address| is
  // acceptable according to the visitor's policy. Otherwise, returns false
  // and populates |error_details|.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const NqQuicSocketAddress& self_address,
                            std::string* error_details) const override;

 private:
  NqDispatcherCompat &dispatcher_;
  NqPacketReader &reader_;
};
}