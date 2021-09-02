#include "core/compat/nq_client_compat.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/platform/api/quic_ptr_util.h"

#include "core/nq_client_loop.h"
#include "core/compat/chromium/nq_network_helper.h"
#include "core/compat/chromium/nq_stub_interface.h"
#include "core/compat/chromium/nq_network_helper.h"

namespace nq {
NqClientCompat::NqClientCompat(NqQuicSocketAddress server_address,
                           NqClientLoop &loop,
                           const NqQuicServerId &server_id,
                           const NqClientConfig &config)
  : NqClientBase(loop, server_id, config), 
  client_(this, loop, server_id, config) {
  client_.set_server_address(server_address);
}
}  // namespace nq
#else
#include "core/compat/quiche/deps.h"
#include "core/nq_client_loop.h"

namespace nq {
void NqClientCompat::StartConnect() {
  OnInitializeSession();
  client_loop()->random().GetBytes(conn_id_as_bytes_, sizeof(conn_id_));
  // MUST_DO(iyatomi): call quiche_connect
  conn_ = quiche_connect(
    server_id_.host().c_str(), conn_id_as_bytes_, sizeof(conn_id_),
    reinterpret_cast<const struct sockaddr *>(&(server_address_.generic_address())), 
    server_address_.generic_address_length(), config_->operator quiche_config *()
  );
}
}
#endif
