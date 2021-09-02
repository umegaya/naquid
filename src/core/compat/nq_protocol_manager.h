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
    QuicV1 = 0x00000001,
    QuicDraft27 = 0xff00001b,
    QuicDraft28 = 0xff00001c,
    QuicDraft29 = 0xff00001d,

    QuicLatest = QuicV1,
  };
  NqProtocolManager() : protocol_(Protocol::QuicNeedNegosiate) {}
  NqProtocolManager(nq_wire_proto_t p) : protocol_(From(p)) {}

  //get/set
  inline Protocol protocol() const { return protocol_; }

  //static
  static inline Protocol From(nq_wire_proto_t p) {
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
