#include "timeout.h"

using namespace nqtest;

/*
	more to test:
	continue call of close => reset : close
	continue call of reset => close : close
	finalize is called
*/

void test_reconnect_client(Test::Conn &conn) {
	conn.OpenRpc("rpc", [&conn](nq_rpc_t rpc, void **) {
		auto done = conn.NewLatch();
		if (conn.disconnect > 0) {
			done(false);
			return true;
		}
		auto done2 = conn.NewLatch();
		auto done3 = conn.NewLatch();
		auto done4 = conn.NewLatch();

		static int counter = 4/* 3 client disconnect and 1 server disconnect */, close_counter = 0;
		static nq_time_t last_close = 0;

		WATCH_CONN(conn, ConnClose, ([done](
			nq_conn_t c, nq_result_t result, const char *detail, bool from_remote) -> nq_time_t {
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
			nq_conn_t c, nq_handshake_event_t hsev, void *ppctx) {
			counter--;
			*(int **)ppctx = ctx_ptr;
			TRACE("ConnOpen, counter %d %d (%d)", counter, close_counter, hsev);
			if (counter <= 0) {
				done2(close_counter == 4);
				nq_conn_close(c);
			} else {
				nq_time_t now = nq_time_now();
				if (now < last_close && 
					(now - last_close) < nq_time_msec(1100) && 
					(now - last_close) > nq_time_msec(900)) {
					//seems wrong reconnection timing
					done2(false);
				} else {
					nq_conn_reset(c);
				}
			}
		}));
		WATCH_CONN(conn, ConnFinalize, ([done3, ctx_ptr](nq_conn_t c, void *ctx) {
			TRACE("ConnFinalize %u %p %p", close_counter, nq_conn_ctx(c), ctx_ptr);
			//nq_conn_ctx should return nullptr because connection is no more valid.
			//instead of this, variable ctx should have the value which is attached with this conn
			done3(close_counter == 5 && ctx == ctx_ptr && nq_conn_ctx(c) == nullptr);
		}));
		TRACE("test_reconnect_client: call RPC to close connection");
		RPC(rpc, RpcType::Close, "", 0, ([done4](
			nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
			TRACE("test_reconnect_client: reply RPC");
			done4(r == 0);
		}));
		return true;
	});
}

static void send_rpc(nq_rpc_t rpc, nq_time_t now, std::function<void (nq_rpc_t, nq_result_t, const void *, nq_size_t)> cb) {
	char buff[sizeof(now)];
	nq::Endian::HostToNetbytes(now, buff);
	RPC(rpc, RpcType::SetupReject, buff, sizeof(buff), cb);
}

void test_reconnect_server(Test::Conn &conn) {
	auto done = conn.NewLatch();

	static int close_counter = 0, open_counter = 0, stream_close_counter = 0, stream_open_counter = 0, recv_goaway = 0;
	static nq_rpc_t rpcs[8];
	WATCH_CONN(conn, ConnOpen, ([](
		nq_conn_t, nq_handshake_event_t hsev, void **) {
		open_counter++;
		TRACE("ConnOpen: %d %d %d", close_counter, open_counter, stream_close_counter);
		return true;
	}));
	WATCH_CONN(conn, ConnClose, ([](nq_conn_t conn, nq_result_t result, const char *detail, bool from_remote) -> nq_time_t {
		close_counter++;
		TRACE("ConnClose: %d %d %d", close_counter, open_counter, stream_close_counter);
		return nq_time_sec(1);		
	}));
	WATCH_CONN(conn, ConnOpenStream, ([&conn, done](nq_rpc_t rpc, void **){
		TRACE("ConnOpenStream %d %d %d", close_counter, open_counter, stream_close_counter);
		for (int i = 0; i < stream_open_counter; i++) {
			TRACE("check for %d %s", i, nq_rpc_equal(rpc, rpcs[i]) ? "eq" : "ne");
			if (nq_rpc_equal(rpc, rpcs[i])) {
				done(false);
				return false;
			}
		}
		rpcs[stream_open_counter++] = rpc;
		auto now = nq_time_now();
		send_rpc(rpc, now, [done, now](nq_rpc_t, nq_result_t r, const void *data, nq_size_t){
			//finally can execute rpc correctly and connection closed as we expected
			TRACE("close_counter = %d %d %d %d", r, close_counter, open_counter, stream_close_counter);
			if (r == 0) {
				auto sent = nq::Endian::NetbytesToHost<nq_time_t>(data);
				done(sent == now && close_counter == 2 && recv_goaway == stream_close_counter && recv_goaway == 3);
			} else if (r != NQ_EGOAWAY) {
				done(false);
			} else {
				recv_goaway++;
			}
		});
		return true;
	}));
	WATCH_CONN(conn, ConnCloseStream, ([&conn, done](nq_rpc_t rpc){
		stream_close_counter++;
		TRACE("ConnCloseStream %d %d %d", close_counter, open_counter, stream_close_counter);
		if (!nq_rpc_equal(rpc, rpcs[stream_open_counter - 1])) {
			done(false);
			return;
		}
		//create new stream
		conn.OpenRpc("rpc", [&conn](nq_rpc_t, void **) { return true; });
	}));
	conn.OpenRpc("rpc", [&conn](nq_rpc_t, void **) { return true; });
}

