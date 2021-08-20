#pragma once

#include <map>

#include <ares.h>

#include "basis/defs.h"
#include "basis/io_processor.h"

namespace nq {
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
  struct Query {
    NqAsyncResolver *resolver_;
    std::string host_;
    int family_;

    Query() : host_(), family_(AF_UNSPEC) {}
    virtual ~Query() {}

    virtual void OnComplete(int status, int timeouts, struct hostent *hostent) = 0;  
    static void OnComplete(void *arg, int status, int timeouts, struct hostent *hostent) {
      auto q = (Query *)arg;
      if (q->family_ == AF_INET && (status == ARES_ENOTFOUND || status == ARES_ENODATA)) {
        q->family_ = AF_INET6;
        q->resolver_->StartResolve(q);
        return;
      }
      q->OnComplete(status, timeouts, hostent);
      delete q;
    }
  };
  typedef ares_host_callback Callback;  
  typedef ares_channel Channel;
 protected:
  typedef Fd Fd;
  typedef IoProcessor::Event Event;
  class IoRequest : public IoProcessor {
   private:
    uint32_t current_flags_;
    bool alive_;
    Channel channel_;
    Fd fd_;
   public:
    IoRequest(Channel channel, Fd fd, uint32_t flags) : 
      current_flags_(flags), alive_(true), channel_(channel), fd_(fd) {}
    ~IoRequest() override {}
    // implements IoProcessor
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
  std::vector<Query*> queries_;
 public:
  NqAsyncResolver() : channel_(nullptr), io_requests_() {}
  bool Initialize(const Config &config);
  void StartResolve(Query *q) { q->resolver_ = this; queries_.push_back(q); }
  void Resolve(const char *host, int family, Callback cb, void *arg);
  void Poll(NqLoop *l);
  static inline int PtoN(const std::string &host, int *af, void *buff) {
    *af = host.find(':') == std::string::npos ?  AF_INET : AF_INET6;
    if (ares_inet_pton(*af, host.c_str(), buff) >= 0) {
      return Syscall::GetIpAddrLen(*af);
    } else {
      return -1;
    }
  }
  static inline int NtoP(const void *src, nq_size_t srclen, char *dst, nq_size_t dstlen) {
    const char *converted = nullptr;
    if (srclen == Syscall::GetIpAddrLen(AF_INET)) {
      converted = ares_inet_ntop(AF_INET, src, dst, dstlen);
    } else if (srclen == Syscall::GetIpAddrLen(AF_INET6)) {
      converted = ares_inet_ntop(AF_INET6, src, dst, dstlen);
    } else {
      TRACE("invalid srclen:%u", srclen);
      ASSERT(false);
      return -1;
    }
    if (converted != nullptr) {
      return 0;
    } else {
      int eno = Syscall::Errno();
      TRACE("failure ntop: %d", eno);
      return -1;
    }
  }
};
}
