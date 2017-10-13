#pragma once

#include <map>
#include <thread>

#include "net/tools/quic/quic_dispatcher.h"

#include "interop/naquid_worker.h"

namespace net {
class NaquidWorker;
class NaquidDispatcher : public QuicDispatcher, 
                         public nq::IoProcessor {
  int port_;
  const NaquidServer &server_;
  NaquidServerLoop &loop_;
  NaquidPacketReader &reader_;
  std::map<QuicConnectionId, std::unique_ptr<QuicConnection>> conn_map_;
 public:
  NaquidDispatcher(int port, const NaquidWorker &worker);
  void Process(NaquidPacket *p) {
    ProcessPacket(p->server_address(), p->client_address(), *p);
    reader_.Pool(p->data(), p);
  }
  //implements nq::IoProcessor
  void OnEvent(Fd fd, const Event &e) override;
  void OnClose(Fd fd) override {}
	int OnOpen(Fd fd) override;

  //implement NaquidPacketReader::Delegate
  void OnRecv(NaquidPacket *packet);

 protected:
  //implements QuicDispatcher
  QuicSession* CreateQuicSession(
    QuicConnectionId connection_id,
    const QuicSocketAddress& client_address,
    QuicStringPiece alpn) override;
}
}