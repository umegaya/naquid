#include "core/compat/chromium/nq_quic_dispatcher.h"

#include "core/compat/chromium/nq_stub_interface.h"

namespace net {
NqQuicDispatcher::NqQuicDispatcher(
  NqDispatcherCompat &dispatcher, QuicCryptoServerConfig *crypto_config,
  const NqServerConfig& config, NqWorker &worker
) : QuicDispatcher(config.chromium(),
                   crypto_config,
                   new QuicVersionManager(net::AllSupportedVersions()),
                   //TODO(iyatomi): enable to pass worker.loop or this directory to QuicDispatcher ctor. 
                   //main reason to wrap these object now, is QuicDispatcher need to store them with unique_ptr.
                   std::unique_ptr<QuicConnectionHelperInterface>(new NqStubConnectionHelper(worker.loop())), 
                   std::unique_ptr<QuicCryptoServerStream::Helper>(new NqStubCryptoServerStreamHelper(*this)),
                   std::unique_ptr<QuicAlarmFactory>(new NqStubAlarmFactory(worker.loop()))
                  ), dispatcher_(dispatcher), reader_(worker.reader()) {
}


//implements QuicDispatcher
void NqQuicDispatcher::OnConnectionClosed(QuicConnectionId connection_id,
                        QuicErrorCode error,
                        const std::string &error_details) {
  auto it = session_map().find(connection_id);
  if (it != session_map().end()) {
    dispatcher_.OnSessionClosed(static_cast<NqServerSession *>(it->second.get()));
  }
  QuicDispatcher::OnConnectionClosed(connection_id, error, error_details);  
}
QuicSession* NqQuicDispatcher::CreateQuicSession(QuicConnectionId connection_id,
                                                 const NqQuicSocketAddress &client_address,
                                                 QuicStringPiece alpn) {
  return dispatcher_.CreateQuicSession(connection_id, client_address, alpn);
}


//implements QuicCryptoServerStream::Helper
QuicConnectionId NqQuicDispatcher::GenerateConnectionIdForReject(
    QuicConnectionId connection_id) const {
  return dispatcher_.GenerateConnectionIdForReject(connection_id);
}
bool NqQuicDispatcher::CanAcceptClientHello(const CryptoHandshakeMessage& message,
                          const NqQuicSocketAddress& self_address,
                          std::string* error_details) const {
  return dispatcher_.CanAcceptClientHello(message, self_address, error_details);
}
} // namespace net