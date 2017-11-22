#include "rpc.h"

using namespace nqtest;

static void test_io(nq_stream_t s, Test::Conn &tc) {
	auto done = tc.NewLatch();
	std::string text = "hogehogehoge";
	nq_stream_send(s, text.c_str(), text.length());
	WATCH_STREAM(tc, s, StreamRecord, ([done, text](nq_stream_t st, const void *data, nq_size_t dlen) {
		auto text2 = text + text;
		done(MakeString(data, dlen) == text2);
	}));
}

void test_stream(Test::Conn &conn) {
	auto simple = conn.NewStream("sst");
	auto raw = conn.NewStream("rst");
	test_io(simple, conn);
	test_io(raw, conn);
}
