#pragma once

#if defined(NQ_CHROMIUM_BACKEND)
#include "net/quic/core/quic_version_manager.h"

namespace net {
class NqQuicVersionManager : public QuicVersionManager {
public:
  NqQuicVersionManager() : QuicVersionManager(net::AllSupportedVersions()) {}
};
}
#else
namespace net {
class NqQuicVersionManager {
public:
  enum Version {
    NeedNegosiate = 0xbabababa; // reserved version for negosiation
    V1 = 0x0000_0001;
    Draft27 = 0xff00_001b;
    Draft28 = 0xff00_001c;
    Draft29 = 0xff00_001d;
  }
  NqQuicVersionManager() : version_(Version::NeedNegosiate) {}
private:
  Version version_;
};
} // net
#endif
