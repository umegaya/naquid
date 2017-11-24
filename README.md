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
- [ ] conn: more cert check
- [ ] conn: check ```[1122/035845.066728:WARNING:rtt_stats.cc(44)] Ignoring measured send_delta``` log is valid
- [ ] API: reduce error code (can use app-defined error code almost anywhere)
- [ ] API: more API to thread safe 
- [ ] API: delegate chromium log output to our callback (now LogMessage output logs by itself)
- [ ] API: more detail control of rpc timeout
- [ ] server: QuicAlarm should be more efficient
  - maybe better to more generalize NqLoop's alarm system and enable to directly call delegate object 
- [ ] test: high frequent reconnection test
- [x] test: server side stream initiation
- [ ] test: stream disconnection using on open callback 
- [ ] test: client conn reconnection or finalization using on open callback
- [x] test: stream handle send/recv test
- [ ] test: robustness for connectivity change
- [ ] bench: higher concurrency test (around 10k client connection)
- [ ] bench: ensure scalability with number of thread
- [x] bench: latency, throughput, compare with mrs, which is ENet based, gaming specific udp network library
  - 20~30% slower than mrs, but it may not big difference for naquid main use case (send/recv small packet very frequently)


#### remain tasks for 1.0.0
- [ ] stream/rpc: unreliable packet sending
- [ ] stream/rpc: unencrypted packet sending 
- [ ] conn: optional faster network stack by by-passing kernel (like dpdk)


#### YAGNI
- [ ] conn: optinally enable certificate transparency verification 
  - maybe better just expose certificate data to user