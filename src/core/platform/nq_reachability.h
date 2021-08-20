#pragma once

#include <string>

#include "nq.h"

namespace nq {
class NqReachability {
 public:
  typedef nq_reachability_t Status;
 public:
  //property
  inline Status current_state() const { return current_state_; }

  //overridden by platform independent implementation
  virtual bool Start(const std::string &hostname) = 0;
  virtual void Stop() = 0;

  //user must create/destroy instance via this function
  static NqReachability *Create(nq_on_reachability_change_t cb);
  static void Destroy(NqReachability *r) { delete r; }
 protected:
  NqReachability(nq_on_reachability_change_t cb) : observer_(cb) {}
  virtual ~NqReachability() {}

  Status current_state_;
  nq_on_reachability_change_t observer_;
};
}
