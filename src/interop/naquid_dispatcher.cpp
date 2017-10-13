#include "interop/naquid_dispatcher.h"

#include "interop/naquid_client_loop.h"
#include "interop/naquid_server_session.h"
#include "interop/naquid_server.h"

namespace net {
NaquidDispatcher::NaquidDispatcher(int port, const NaquidWorker &worker) : 
	port_(port), server_(worker.server()), loop_(worker.loop()), reader_(worker.reader()), conn_map_() {}

//implements nq::IoProcessor
void NaquidDispatcher::OnEvent(Fd fd, const Event &e) override {
  if (NaquidLoop::Writable(e)) {
    writer()->SetWritable(); //indicate fd become writable
  }
  if (NaquidLoop::Readable(e)) {
    while (reader_.Read(fd, port_, *this, nullptr)) {}
  } 
}
int NaquidDispatcher::OnOpen(Fd fd) override {
  InitializeWithWriter(new QuicDefaultPacketWriter(fd));
  return NQ_OK;
}
void NaquidDispatcher::OnRecv(NaquidPacket *packet) {
  auto idx = packet.WorkerIndex();
  if (index_ == idx) {
    //TODO(iyatomi): if idx is same as current index, directly process packet here
    Process(packet);
  } else {
    //otherwise send other queue
    server_.Q4(idx).enqueue(packet);
  }
}

//implements QuicDispatcher
QuicSession* NaquidDispatcher::CreateQuicSession(QuicConnectionId connection_id,
                                             const QuicSocketAddress& client_address,
                                             QuicStringPiece alpn) {
    QuicConnection* connection = new QuicConnection(
      connection_id, client_address, &loop_, &loop_,
      CreatePerConnectionWriter(),
      /* owns_writer= */ true, Perspective::IS_SERVER, GetSupportedVersions());

    conn_map_[connection_id] = connection;

    return new NaquidServerSession(connection, QuicConfig());
}
}
