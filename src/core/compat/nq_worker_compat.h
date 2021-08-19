#pragma once

#if defined(NQ_CHROMIUM_BACKEND)

#include "core/compat/chromium/nq_packet_reader.h"

namespace net {
class NqWorkerCompat {
 public:
  NqWorkerCompat() : reader_()  {}

  //get/set
  inline NqPacketReader &reader() { return reader_; }

private:
  NqPacketReader reader_;
};
} //namespace net
#else
namespace net {
class NqWorkerCompat {
 public:
  NqWorkerCompat() {}
};
} //namespace net
#endif