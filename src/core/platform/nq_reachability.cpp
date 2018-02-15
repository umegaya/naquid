#include "core/platform/nq_platform.h"
#if !defined(OS_ANDROID) && !defined(OS_MACOSX)
#include "core/platform/nq_reachability.h"
#include "basis/assert.h"
#include "basis/logger.h"

namespace net {

class NqReachabilityNoop : public NqReachability {
 public:
  bool Start(const std::string &hostname) override {
    nq::logger::fatal("current platform does not support automatic reachability change yet. call nq_conn_reachability_change manally");
    return false;
  }
  void Stop() override {
    nq::logger::fatal("current platform does not support automatic reachability change yet. call nq_conn_reachability_change manally");
  }
  NqReachabilityNoop(nq_on_reachability_change_t cb) : NqReachability(cb) {}
 protected:
  ~NqReachabilityNoop() override {}
};

NqReachability *NqReachability::Create(nq_on_reachability_change_t cb) {
  return new NqReachabilityNoop(cb);
}
}
#endif