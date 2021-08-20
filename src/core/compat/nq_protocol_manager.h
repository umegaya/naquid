#pragma once

#include "nq.h"

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_version_manager.h"

namespace nq {
using namespace net;
class NqProtocolManager : public QuicVersionManager {
 public:
  NqProtocolManager() : QuicVersionManager(net::AllSupportedVersions()) {}
  NqProtocolManager(nq_wire_proto_t p) : QuicVersionManager(net::AllSupportedVersions()) {}

  QuicVersionVector supported_versions() const { return net::AllSupportedVersions(); }
};
}
#else
namespace nq {
class NqProtocolManager {
 public:
  enum Protocol {
    QuicNeedNegosiate = 0xbabababa, // reserved version for quic negosiation
    QuicV1 = 0x0000_0001,
    QuicDraft27 = 0xff00_001b,
    QuicDraft28 = 0xff00_001c,
    QuicDraft29 = 0xff00_001d
  };
  NqProtocolManager() : version_(Version::QuicNeedNegosiate) {}
  NqProtocolManager(nq_wire_proto_t p) : versions_(From(p)) {}

  inline Protocol From(nq_wire_proto_t p) {
    switch (p) {
      case NQ_QUIC_NEGOTIATE:
        return QuicNeedNegosiate;
      case NQ_QUIC_V1:
        return QuicV1;
      default:
        logger::fatal({
          {"msg", "unsupported wire protocol"},
          {"protocol_number", p}
        });
        ASSERT(false);
    }
  }
 private:
  Protocol protocol_;
};
} // net
#endif
