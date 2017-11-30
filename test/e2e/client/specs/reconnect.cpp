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
	if (conn.disconnect > 0) {
		done(false);
		return;
	}
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

static void send_rpc(nq_rpc_t rpc, nq_time_t now, std::function<void (nq_rpc_t, nq_result_t, const void *, nq_size_t)> cb) {
	char buff[sizeof(now)];
	nq::Endian::HostToNetbytes(now, buff);
	RPC(rpc, RpcType::SetupReject, buff, sizeof(buff), cb);
}

void test_reconnect_server_conn(Test::Conn &conn) {
	auto rpc = conn.NewRpc("rpc");
	auto done = conn.NewLatch();

	static int close_counter = 0;
	static bool recv_goaway = false;
	WATCH_CONN(conn, ConnClose, ([](nq_conn_t conn, nq_result_t result, const char *detail, bool from_remote) -> nq_time_t {
		close_counter++;
		return nq_time_sec(1);		
	}));
	WATCH_CONN(conn, ConnOpen, ([&conn, rpc, done](
		nq_conn_t, nq_handshake_event_t hsev, void **) {
		auto now = nq_time_now();
		send_rpc(rpc, now, [done, now](nq_rpc_t, nq_result_t r, const void *data, nq_size_t){
			//finally can execute rpc correctly and connection closed as we expected
			TRACE("close_counter = %d, %d", r, close_counter);
			if (r == 0) {
				auto sent = nq::Endian::NetbytesToHost<nq_time_t>(data);
				done(sent == now && close_counter == 2 && recv_goaway);
			} else if (r != NQ_EGOAWAY) {
				done(false);
			} else {
				recv_goaway = true;
			}
		});
		return true;
	}));
}

void test_reconnect_server_stream(Test::Conn &conn) {
	auto rpc = conn.NewRpc("rpc");
	auto done = conn.NewLatch();

	static nq_sid_t sids[8] = {};
	static int open_stream_counter = 0, close_stream_counter = 0;
	static int vals[] = { 0, 100, 200, 300, 400 };
	WATCH_CONN(conn, ConnClose, ([](nq_conn_t conn, nq_result_t result, const char *detail, bool from_remote) -> nq_time_t {
		return nq_time_msec(1);		
	}));
	WATCH_CONN(conn, ConnOpen, ([&conn, rpc, done](
		nq_conn_t, nq_handshake_event_t hsev, void **) {
		//reset stream counter for every conn establishment
		open_stream_counter = 0, close_stream_counter = 0;
		
		WATCH_CONN(conn, ConnOpenStream, ([&conn, rpc, done](nq_rpc_t opened_rpc, void **ppctx) {
			auto opened_sid = nq_rpc_sid(opened_rpc);
			if ((opened_sid % 2) != 1) {
				done(false);
				return false;				
			}
			if (open_stream_counter > 0) {
				auto sid = sids[open_stream_counter - 1];
				if ((opened_sid - sid) != 2) {
					TRACE("%u %u", opened_sid, sid);
					done(false);
					return false;
				}
			}
			*ppctx = vals + open_stream_counter;
			sids[open_stream_counter++] = opened_sid;
			return true;
		}));
		//always can listen event on same rpc handle, because its retained over reconnection
		WATCH_STREAM(conn, rpc, ConnCloseStream, ([done](nq_rpc_t closed_rpc){
			auto sid = sids[open_stream_counter - 1];
			auto closed_sid = nq_rpc_sid(closed_rpc);
			if (closed_sid != sid) {
				TRACE("2");
				done(false);
				return;
			}
			close_stream_counter++;
			if (close_stream_counter == open_stream_counter) {
				TRACE("3");
				done(false);
				return;
			}
			if (*((int *)nq_rpc_ctx(closed_rpc)) != vals[open_stream_counter - 1]) {
				TRACE("4");
				done(false);
				return;
			}
			//this should initiate next stream
			send_rpc(closed_rpc, nq_time_now(), [done](nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen){
				//close_sream_counter + 1 means, last opened stream not closed, so open_steram_counter is 1 more that closed.
				if (open_stream_counter == 4 && open_stream_counter == (close_stream_counter + 1)) {
					done(r == 0);
				} else if (r != NQ_EGOAWAY) {
					done(false);
				}
			});				
		})); //*/
		//kick first rpc to cause stream close
		send_rpc(rpc, nq_time_now(), [done](nq_rpc_t, nq_result_t r, const void *, nq_size_t){
			done(r == NQ_EGOAWAY); //should not reply in here
		});
		return true;
	}));
}
