#include "rpc.h"

using namespace nqtest;

static void test_io(nq_stream_t s, Test::Conn &tc) {
	auto done = tc.NewLatch();
	auto sid = nq_stream_sid(s);
	std::string text = "hogehogehoge";
	TRACE("send stream: %u %u bytes", sid, text.length());
	nq_stream_send(s, text.c_str(), text.length());
	WATCH_STREAM(tc, s, StreamRecord, ([sid, done, text](nq_stream_t st, const void *data, nq_size_t dlen) {
		TRACE("recv stream: %u %u bytes", sid, dlen);
		auto text2 = text + text;
		done(MakeString(data, dlen) == text2);
	}));
}

void test_stream(Test::Conn &conn) {
	conn.OpenStream("sst", [&conn](nq_stream_t simple, void **ppctx) {
		test_io(simple, conn);
		return true;
	});
	conn.OpenStream("rst", [&conn](nq_stream_t raw, void **ppctx) {
		test_io(raw, conn);
		return true;
	});
}
