#### status
- now basic client/server communication done. more test for ensure stability and performance improvement is required

#### remain tasks for 0.1.0
- [x] rpc: reduce frame number (unify length header/msgid/type to one frame)
- [x] rpc: add header byte to indicate msgid/type size
- [ ] conn: client side proof verification (import ProofVerifierChromium)
- [ ] conn: study is there a way to send stream name without sending over stream (cmsghdr can do this somehow?)
- [ ] conn: changing to batch send by default, with preparing new API to flush batch buffer
- [ ] conn: make user able to read property of NqSession::Delegate via nq_conn_t
- [ ] test: high freqent reconnection test
- [ ] test: higher concurrency test (around 10k client connection)
- [ ] test: server side stream initiation
- [ ] test: stream disconnection using on open callback 
- [ ] test: client conn reconnection or finalization using on open callback
- [ ] test: stream handle send/recv test
- [ ] bench: ensure scalability with number of thread
- [ ] bench: latency, throughput, compare with mrs, which is gaming specific udp network library

#### remain tasks for future
- [ ] stream/rpc: unreliable packet sending
- [ ] stream/rpc: unencrypted packet sending 
- [ ] conn: optional faster network stack by by-passing kernel (like dpdk)