#include "shutdown.h"

using namespace nqtest;


void test_shutdown(nqtest::Test::Conn &conn) {
	conn.OpenRpc("rpc", [&conn](nq_rpc_t rpc, void **) {
		auto done = conn.NewLatch();
		auto done2 = conn.NewLatch();

		WATCH_CONN(conn, ConnClose, ([done](nq_conn_t c, nq_error_t result, const nq_error_detail_t *detail, bool from_remote){
			if (result != NQ_EQUIC) {
				done(false);
			} else if (detail->code != 70) {
				done(false);				
			} else if (!from_remote) {
				done(false);
			} else {
				done(true);
			}
			return 0;
		}));

		TRACE("test_shutdown: call RPC");
		RPC(rpc, RpcType::Shutdown, "", 0, ([rpc, done2](
			nq_rpc_t rpc2, nq_error_t r, const void *data, nq_size_t dlen) {
			TRACE("test_shutdown: reply RPC");
			done2(r >= 0 || r == NQ_EGOAWAY);
		}));

		return true;
	});
}
