#include "timeout.h"

using namespace nqtest;

/*
	more to test:
	continue call of close => reset : close
	continue call of reset => close : close
	finalize is called
*/

void test_reconnect_client(Test::Conn &conn) {
	auto rpc = conn.NewRpc("rpc");
	auto done = conn.NewLatch();
	auto done2 = conn.NewLatch();
	auto done3 = conn.NewLatch();
	auto done4 = conn.NewLatch();

	static int counter = 4/* 3 client disconnect and 1 server disconnect */, close_counter = 0;
	static nq_time_t last_close = 0;

	WATCH_CONN(conn, ConnClose, ([done](
		nq_conn_t conn, nq_result_t result, const char *detail, bool from_remote) -> nq_time_t {
		close_counter++;
		TRACE("ConnClose(%d): detail = %s", close_counter, detail);
		switch (close_counter) {
			case 1: //closed by RPC call, server side
				if (!from_remote) {
					done(false);
					return 0;
				}
				break;
			case 2: //closed by nq_conn_reset in close callback
			case 3:
			case 4:
				if (from_remote) {
					done(false);
					return 0;
				}
				break;
			case 5:  //closed by nq_conn_close in close callback
				done(true);
				break;
			default:
				ASSERT(false);
				break;
		}
		last_close = nq_time_now();
		return nq_time_sec(1);
	}));
	int *ctx_ptr = reinterpret_cast<int *>(0x5678);
	WATCH_CONN(conn, ConnOpen, ([done2, ctx_ptr](
		nq_conn_t conn, nq_handshake_event_t hsev, void *ppctx) {
		if (hsev != NQ_HS_DONE) {
			return;
		}
		counter--;
		*(int **)ppctx = ctx_ptr;
		TRACE("ConnOpen, counter %d %d (%d)", counter, close_counter, hsev);
		if (counter <= 0) {
			done2(close_counter == 4);
			nq_conn_close(conn);
		} else {
			nq_time_t now = nq_time_now();
			if (now < last_close && 
				(now - last_close) < nq_time_msec(1100) && 
				(now - last_close) > nq_time_msec(900)) {
				//seems wrong reconnection timing
				done2(false);
			} else {
				nq_conn_reset(conn);
			}
		}
	}));
	WATCH_CONN(conn, ConnFinalize, ([done3, ctx_ptr](nq_conn_t conn, void *ctx) {
		TRACE("ConnFinalize %u %p %p", close_counter, nq_conn_ctx(conn), ctx_ptr);
		//nq_conn_ctx should return nullptr because connection is no more valid.
		done3(close_counter == 5 && ctx == ctx_ptr && nq_conn_ctx(conn) == nullptr);
	}));
	TRACE("test_reconnect_client: call RPC to close connection");
	RPC(rpc, RpcType::Close, "", 0, ([done4](
		nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
		TRACE("test_reconnect_client: reply RPC");
		done4(r == 0);
	}));
}

void test_reconnect_server(Test::Conn &conn) {
	auto rpc = conn.NewRpc("rpc");
	auto done = conn.NewLatch();
	auto done2 = conn.NewLatch();

	static int close_counter = 0, close_stream_counter = 0, open_stream_counter = 0;
	static nq_time_t last_close = 0;

	WATCH_CONN(conn, ConnClose, ([done](
		nq_conn_t conn, nq_result_t result, const char *detail, bool from_remote) {
		TRACE("ConnClose: detail = %s", detail);
		close_counter++;
		last_close = nq_time_now();
		if (close_counter == 1 && !from_remote) {
			done(false);
		}
		return nq_time_sec(1);
	}));
	WATCH_CONN(conn, ConnOpen, ([rpc](
		nq_conn_t conn, nq_handshake_event_t hsev, void *ppctx) {
		TRACE("ConnOpen, close counter %d", close_counter);
		return true;
	}));
	WATCH_STREAM(conn, rpc, ConnOpenStream, ([done2]
		(nq_rpc_t rpc, void *ppctx){
		open_stream_counter++;
		TRACE("ConnOpenStream, counter %d %d %d", close_counter, close_stream_counter, open_stream_counter);
		auto now = nq_time_now();
		char buff[sizeof(now)];
		nq::Endian::HostToNetbytes(now, buff);
		RPC(rpc, RpcType::Ping, buff, sizeof(buff), ([done2](
			nq_rpc_t rpc2, nq_result_t r, const void *data, nq_size_t dlen) {
			done2(close_counter == 3 && open_stream_counter == 3 && close_stream_counter == 3 && r == 0);
		}));
		return true;
	}));
	WATCH_STREAM(conn, rpc, ConnCloseStream, ([](nq_rpc_t rpc){
		close_stream_counter++;
	}));
}
