#include "core/compat/nq_quic_types.h"

#if !defined(NQ_CHROMIUM_BACKEND)
#include "core/nq_async_resolver.h"

namespace nq {
/* static */
bool NqQuicSocketAddress::FromPackedString(
  const char *data, int len, int port, NqQuicSocketAddress &address) {
  address = NqQuicSocketAddress(data, len, port);
  return true;
}
std::string NqQuicSocketAddress::ToString() const {
  char buffer[256];
  NqAsyncResolver::NtoP(
    reinterpret_cast<const void *>(&addr_), 
    Syscall::GetSockAddrLen(family()), buffer, sizeof(buffer)
  );
  return std::string(buffer);
}
} //namespace nq
#endif
