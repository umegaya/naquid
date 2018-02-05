#include "rpc.h"

using namespace nqtest;

static void test_io(nq_stream_t s, Test::Conn &tc, bool use_ack_cb, bool raw_stream) {
	auto done = tc.NewLatch();
	auto sid = nq_stream_sid(s);
	std::string text = "hogehogehoge";
	TRACE("send stream: %u %u bytes", sid, text.length());
	if (use_ack_cb) {
		auto ack_done = tc.NewLatch();
		nq_stream_opt_t opt;
		opt.on_ack = (new StreamAckClosureCaller([ack_done, text, raw_stream](int byte, nq_time_t delay) {
			int est_length;
			if (raw_stream) {
				est_length = text.length() + 1; //it should add trailing byte for line feed 
			} else {
				est_length = 1 + text.length(); //add header byte
			}
			if (byte != est_length) {
				ack_done(false);
			} else {
				ack_done(true);
			}
		}))->closure();
		opt.on_retransmit = nq_closure_empty();
		nq_stream_send_ex(s, text.c_str(), text.length(), &opt);
	} else {
		nq_stream_send(s, text.c_str(), text.length());
	}
	WATCH_STREAM(tc, s, StreamRecord, ([sid, done, text](nq_stream_t st, const void *data, nq_size_t dlen) {
		TRACE("recv stream: %u %u bytes", sid, dlen);
		auto text2 = text + text;
		done(MakeString(data, dlen) == text2);
	}));
}

void test_stream(Test::Conn &conn) {
	conn.OpenStream("sst", [&conn](nq_stream_t simple, void **ppctx) {
		test_io(simple, conn, false, false);
		test_io(simple, conn, true, false);
		return true;
	});
	conn.OpenStream("rst", [&conn](nq_stream_t raw, void **ppctx) {
		test_io(raw, conn, false, true);
		test_io(raw, conn, true, true);
		return true;
	});
}
