#pragma once

#include "nq.h"
#include "basis/defs.h"
#include "basis/timespec.h"
#include "core/compat/nq_protocol_manager.h"

namespace nq {

class NqConfig {
 public:
  virtual void Setup() { ASSERT(false); }
};

class NqClientConfigBase : public NqConfig {
 protected:
  nq_clconf_t client_;
 public:
  NqClientConfigBase() : NqConfig() { memset(&client_, 0, sizeof(client_)); }
  NqClientConfigBase(const nq_clconf_t &conf) : NqConfig(), client_(conf) {
    Setup();
  }
  NqProtocolManager protocol_manager() const { return NqProtocolManager(client_.protocol); }
  const nq_clconf_t &client() const { return client_; }
  //init other variables from client_
  void Setup() override;
};

class NqServerConfigBase : public NqConfig {
 protected:
  nq_addr_t addr_;
  nq_svconf_t server_;
  static const char kDefaultQuicSecret[];
 public:
  NqServerConfigBase(const nq_addr_t &addr) : 
    NqConfig(), addr_(addr) {
    memset(&server_, 0, sizeof(server_));
    nq_closure_init(server_.on_open, NoopOnOpen, nullptr);
    nq_closure_init(server_.on_close, NoopOnClose, nullptr);
    Setup();
  }
  NqServerConfigBase(const nq_addr_t &addr, const nq_svconf_t &conf) : 
    NqConfig(), addr_(addr), server_(conf) {
    Setup();
  }
  const nq_svconf_t &server() const { return server_; }
  //init other variables from server_
  void Setup() override;
 protected:
  static void NoopOnOpen(void *, nq_conn_t, void **) {}
  static void NoopOnClose(void *, nq_conn_t, nq_error_t, const nq_error_detail_t*, bool) {}
};

} //net
