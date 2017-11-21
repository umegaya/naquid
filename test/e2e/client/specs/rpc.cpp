#include "rpc.h"

using namespace nqtest;

static void test_ping(nq_rpc_t rpc, Test::Conn &tc) {
	auto done = tc.NewLatch();
	auto now = nq_time_now();
	char buff[sizeof(now)];
	nq::Endian::HostToNetbytes(now, buff);
	TRACE("test_ping: call RPC");
	RPC(rpc, RpcType::Ping, buff, sizeof(buff), ([rpc, done, now](
		nq_rpc_t rpc2, nq_result_t r, const void *data, nq_size_t dlen) {
		TRACE("test_ping: reply RPC");
		if (dlen != sizeof(now)) {
			done(false);
			return;
		}
		auto sent = nq::Endian::NetbytesToHost<nq_time_t>(data);
		done(nq_rpc_sid(rpc) == nq_rpc_sid(rpc2) && r >= 0 && now == sent);
	}));
}

static void test_raise(nq_rpc_t rpc, Test::Conn &tc) {
	auto done = tc.NewLatch();
	const int32_t code = -999;
	const std::string msg = "test failure reason";
	//pack payload
	char buff[sizeof(int32_t) + msg.length()];
	nq::Endian::HostToNetbytes(code, buff);
	memcpy(buff + sizeof(int32_t), msg.c_str(), msg.length());

	TRACE("test_raise: call RPC");
	RPC(rpc, RpcType::Raise, buff, sizeof(buff), ([done, code, msg](
		nq_rpc_t rpc, nq_result_t r, const void *data, nq_size_t dlen) {
		TRACE("test_raise: reply RPC");
		if (dlen != msg.length()) {
			done(false);
			return;
		}
		done(r == code && memcmp(msg.c_str(), data, msg.length()) == 0);
	}));
}

static void test_notify(nq_rpc_t rpc, Test::Conn &tc) {
	auto done = tc.NewLatch();
	auto done2 = tc.NewLatch();
	const std::string text = "notify this plz";
	WATCH_STREAM(tc, rpc, RpcNotify, ([done, text](
		nq_rpc_t rpc2, uint16_t type, const void *data, nq_size_t dlen) {
		TRACE("test_notify: notified");
		done(MakeString(data, dlen) == text && type == RpcType::TextNotification);
	});
	TRACE("test_notify: call RPC");
	RPC(rpc, RpcType::NotifyText, text.c_str(), text.length(), ([done2](
		nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
		TRACE("test_notify: reply RPC");
		done2(r >= 0 && MakeString(data, dlen) == "notify success");
	})));
}

static void test_server_stream(nq_rpc_t rpc, const std::string &stream_name, Test::Conn &tc) {
	auto done = tc.NewLatch();	//rpc reply
	auto done2 = tc.NewLatch();	//server stream creation
	auto done3 = tc.NewLatch();	//server request
	const std::string text = "create stream";

	WATCH_CONN(tc, ConnOpenStream, ([&tc, done2, done3, text](nq_rpc_t rpc2, void **ppctx) {
		TRACE("test_server_stream: stream creation");
		WATCH_STREAM(tc, rpc2, RpcRequest, ([rpc2, done3, text](
			nq_rpc_t rpc3, uint16_t type, nq_msgid_t msgid, const void *data, nq_size_t dlen){
			TRACE("test_server_stream: server sent request");
			if (nq_rpc_sid(rpc2) != nq_rpc_sid(rpc3)) {
				done3(false);
				return;
			}
			auto reply = (std::string("from client:") + MakeString(data, dlen));
			nq_rpc_reply(rpc3, RpcError::None, msgid, reply.c_str(), reply.length());
			done3((std::string("from server:") + text) == MakeString(data, dlen) && type == RpcType::ServerRequest);
		}));
		done2(true);
		return true;
	}));
	TRACE("test_server_stream: call RPC");
	std::string buffer = (stream_name + "|" + text);
	RPC(rpc, RpcType::ServerStream, buffer.c_str(), buffer.length(), ([done](
		nq_rpc_t, nq_result_t r, const void *data, nq_size_t dlen) {
		TRACE("test_server_stream: reply RPC");
		done(r >= 0 && dlen == 0);
	}));
}

void test_rpc(Test::Conn &conn) {
	auto rpc = conn.NewRpc("rpc");
	auto rpc2 = conn.NewRpc("rpc");
	test_ping(rpc, conn);
	test_raise(rpc, conn);
	test_notify(rpc2, conn);
	test_server_stream(rpc2, "rpc", conn);
}
