#include "resolver.h"
#include "net/quic/platform/api/quic_ip_address.h"

using namespace nqtest;

void test_resolver(Test::Conn &tc) {
  auto cl = tc.t->current_client();
  auto done = tc.NewLatch();  //rpc reply
  auto done2 = tc.NewLatch(); //server stream creation ok
  auto done3 = tc.NewLatch(); //server stream creation ok
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
