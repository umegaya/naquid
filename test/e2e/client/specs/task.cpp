#include "task.h"

using namespace nqtest;

void test_task(Test::Conn &conn) {
	conn.OpenRpc("rpc", [&conn](nq_rpc_t rpc, void **ppctx) {
		auto done = conn.NewLatch();
		int *ctx_ptr = reinterpret_cast<int *>(0x5678);
		*(int **)ppctx = ctx_ptr;
		TASK(rpc, ([rpc,done,ctx_ptr](nq_rpc_t rpc2) {
			done(nq_rpc_equal(rpc, rpc2) && nq_rpc_ctx(rpc2) == ctx_ptr);
		}));
		return true;
	});
}
