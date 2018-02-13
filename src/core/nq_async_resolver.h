#pragma once

#include <map>

#include <ares.h>

#include "basis/defs.h"
#include "basis/io_processor.h"

namespace net {
class NqLoop;
class NqAsyncResolver {
 public:
  struct Config : ares_options {
    int optmask;
    ares_addr_port_node *server_list;
    Config();
    ~Config();
    const ares_options *options() const { 
      return static_cast<const ares_options*>(this); }
    //no fail methods
    void SetTimeout(nq_time_t timeout);
    void SetRotateDns();
    void SetStayOpen();
    void SetLookup(bool use_hosts, bool use_dns);

    //methods may fail sometimes
    bool SetServerHostPort(const std::string &host, int port = 53);
  };
  typedef ares_host_callback Callback;  
  typedef ares_channel Channel;
 protected:
  typedef nq::Fd Fd;
  typedef nq::IoProcessor::Event Event;
  class IoRequest : public nq::IoProcessor {
   private:
    uint32_t current_flags_;
    bool alive_;
    Channel channel_;
    Fd fd_;
   public:
    IoRequest(Channel channel, Fd fd) : 
      current_flags_(0), alive_(true), channel_(channel), fd_(fd) {}
    ~IoRequest() override {}
    // implements nq::IoProcessor
    void OnEvent(Fd fd, const Event &e) override;
    void OnClose(Fd fd) override {}
    int OnOpen(Fd fd) override { return NQ_OK; }

    uint32_t current_flags() const { return current_flags_; }
    bool alive() const { return alive_; }
    void set_current_flags(uint32_t f) { current_flags_ = f; }
    void set_alive(bool a) { alive_ = a; }
    Fd fd() const { return fd_; }
  };
  Channel channel_;
  std::map<Fd, IoRequest*> io_requests_;
 public:
  NqAsyncResolver() : channel_(nullptr), io_requests_() {}
  bool Initialize(const Config &config);
  void Resolve(const char *host, int family, Callback cb, void *arg);
  void Poll(NqLoop *l);
  static inline int PtoN(const std::string &host, int *af, void *buff) {
    *af = host.find(':') == std::string::npos ?  AF_INET : AF_INET6;
    if (ares_inet_pton(*af, host.c_str(), buff) >= 0) {
      return nq::Syscall::GetIpAddrLen(*af);
    } else {
      return -1;
    }
  }
};
}
