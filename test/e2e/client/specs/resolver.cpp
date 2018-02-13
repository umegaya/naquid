#include "resolver.h"
#include "net/quic/platform/api/quic_ip_address.h"

using namespace nqtest;

struct context {
  Test::Latch latch;
};
static void on_conn_open(void *, nq_conn_t c, void **ppctx) {
}
static nq_time_t on_conn_close(void *arg, nq_conn_t c, nq_error_t r, const nq_error_detail_t *detail, bool remote) {
  auto ctx = std::unique_ptr<context>((context *)arg);
  if (nq_conn_is_valid(c, nq_closure_empty())) {
    ctx->latch(false);
    return 0;
  }
  if (r != NQ_ERESOLVE) {
    ctx->latch(false);
    return 0;
  }
  if (detail->code != 4) {
    ctx->latch(false);
    return 0;
  }
  if (remote) {
    ctx->latch(false); 
    return 0;
  }
  ctx->latch(true);
  return 0;
}

void test_resolver(Test::Conn &tc) {
  auto cl = tc.t->current_client();
  auto done = tc.NewLatch();  //rpc reply
  auto done2 = tc.NewLatch(); //server stream creation ok
  auto done3 = tc.NewLatch(); //server stream creation ok
  auto c = new context;
  c->latch = tc.NewLatch();
  nq_clconf_t conf;
  nq_closure_init(conf.on_open, on_client_conn_open, on_conn_open, c);
  nq_closure_init(conf.on_close, on_client_conn_close, on_conn_close, c);
  nq_addr_t addr = { "nosuchhost.nowhere2", nullptr, nullptr, nullptr, 8443};
  nq_client_connect(tc.t->current_client(), &addr, &conf);

  RESOLVE(cl, AF_INET, "www.google.com", ([done](nq_error_t r, const nq_error_detail_t *d, const char *p, nq_size_t sz) {
    if (sz != 4 || r != NQ_OK) {
      done(false);
      return;
    }
    net::QuicIpAddress ip;
    if (!ip.FromPackedString(p, sz)) {
      done(false);
      return;
    }
    TRACE("resolve success: as %s", ip.ToString().c_str());
    done(true);
  }));
  RESOLVE(cl, AF_UNSPEC, "nosuchhost.nowhere", ([done2](nq_error_t r, const nq_error_detail_t *d, const char *p, nq_size_t sz) {
    if (r != NQ_ERESOLVE) {
      done2(false);
      return;
    }
    TRACE("resolve failure: by %d(%s)", d->code, d->msg);
    done2(true);
  }));
  RESOLVE(cl, AF_INET6, "www.google.com", ([done3](nq_error_t r, const nq_error_detail_t *d, const char *p, nq_size_t sz) {
    if (sz != 16 || r != NQ_OK) {
      done3(false);
      return;
    }
    net::QuicIpAddress ip;
    if (!ip.FromPackedString(p, sz)) {
      done3(false);
      return;
    }
    TRACE("resolve success: as %s", ip.ToString().c_str());
    done3(true);
  }));
}
