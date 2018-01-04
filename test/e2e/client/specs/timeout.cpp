#include "timeout.h"

using namespace nqtest;

void test_timeout(Test::Conn &conn) {
	conn.OpenRpc("rpc", [&conn](nq_rpc_t rpc, void **ppctx) {
		auto done = conn.NewLatch();
		auto done2 = conn.NewLatch();
		auto done3 = conn.NewLatch();
		char buffer[sizeof(nq_time_t)];

		nq::Endian::HostToNetbytes(nq_time_sec(1), buffer);
		TRACE("test_timeout: call RPC with 1sec sleep");
		auto now = nq_time_now();
		RPC(rpc, RpcType::Sleep, buffer, sizeof(buffer), ([done, now](
			nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
			TRACE("test_timeout: reply RPC with 1sec sleep, takes: %llu", nq_time_now() - now);
			done(r == 0);
		}));

		TRACE("test_timeout: call RPC with 3sec sleep");
		nq::Endian::HostToNetbytes(nq_time_sec(3), buffer);
		RPC(rpc, RpcType::Sleep, buffer, sizeof(buffer), ([done2, now](
			nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
			TRACE("test_timeout: reply RPC with 3sec sleep, takes: %llu", nq_time_now() - now);
			done2(r == NQ_ETIMEOUT);
		}));

		TRACE("test_timeout: call RPC with 3sec sleep and 5sec timeout");
		RPCEX(rpc, RpcType::Sleep, buffer, sizeof(buffer), ([done3, now](
			nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
			TRACE("test_timeout: reply RPC with 3sec sleep with 5sec TO takes: %llu", nq_time_now() - now);
			done3(r == 0);
		}), nq_time_sec(5));

		return true;
	});
}
