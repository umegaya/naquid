#### status
- now basic client/server communication done. more test for ensure stability and performance improvement is required


#### remain tasks for 0.1.0
- [x] rpc: reduce frame number (unify length header/msgid/type to one frame)
- [x] rpc: add header byte to indicate msgid/type size
- [x] conn: client side proof verification (import ProofVerifierChromium)
- [x] conn: study is there a way to send stream name without sending it over wire (cmsghdr can do this somehow?)
  - seems cannot, we stick to send stream kind over stream for a while.
- [x] conn: changing to batch send by default, with preparing new API to flush batch buffer
- [x] conn: make user able to read property of NqSession::Delegate via nq_conn_t from all thread
- [ ] API: reduce error code (can use app-defined error code almost anywhere)
- [ ] API: more API to thread safe 
- [ ] test: high frequent reconnection test
- [ ] test: server side stream initiation
- [ ] test: stream disconnection using on open callback 
- [ ] test: client conn reconnection or finalization using on open callback
- [ ] test: stream handle send/recv test
- [ ] test: robustness for connectivity change
- [ ] bench: higher concurrency test (around 10k client connection)
- [ ] bench: ensure scalability with number of thread
- [x] bench: latency, throughput, compare with mrs, which is gaming specific udp network library
  - 20~30% slower than mrs


#### remain tasks for 1.0.0
- [ ] stream/rpc: unreliable packet sending
- [ ] stream/rpc: unencrypted packet sending 
- [ ] conn: optional faster network stack by by-passing kernel (like dpdk)


#### YAGNI
- [ ] conn: optinally enable certificate transparency verification 
  - maybe better just expose certificate data to user