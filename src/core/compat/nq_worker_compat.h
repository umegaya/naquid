#pragma once

#if defined(NQ_CHROMIUM_BACKEND)

#include "core/compat/chromium/nq_packet_reader.h"

namespace nq {
using namespace net;
class NqWorkerCompat {
 public:
  NqWorkerCompat() : reader_()  {}

  //get/set
  inline chromium::NqPacketReader &reader() { return reader_; }

private:
  chromium::NqPacketReader reader_;
};
} //namespace nq
#else
namespace nq {
class NqWorkerCompat {
 public:
  NqWorkerCompat() {}
};
} //namespace nq
#endif