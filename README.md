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
- [ ] conn: handle connectivity change (if not handled)
- [ ] API: reduce error code (can use app-defined error code almost anywhere)
- [ ] API: more API to thread safe 
- [ ] API: delegate chromium log output to our callback (now LogMessage output logs by itself)
- [ ] API: more detail control of rpc timeout
- [ ] API: raw connection (do not use stream name to select used stream)
  - giving special option to nq_client_connect's nq_clconf_t or nq_svconf_t
- [x] server: QuicAlarm should be more efficient
  - maybe better to more generalize NqLoop's alarm system and enable to directly call delegate object 
- [ ] test: high frequent reconnection test
- [x] test: server side stream initiation
- [ ] test: stream disconnection using on open callback 
- [ ] test: client conn reconnection or finalization using on open callback
- [x] test: stream handle send/recv test
- [ ] conn: timeout test for handshake / rpc call
- [ ] test: robustness for connectivity change
- [ ] bench: higher concurrency test (around 10k client connection)
- [ ] bench: ensure scalability with number of thread
- [x] bench: latency, throughput, compare with mrs, which is ENet based, gaming specific udp network library
  - 20~30% slower than mrs, but it may not big difference for naquid main use case (send/recv small packet very frequently)


#### remain tasks for 1.0.0
- [ ] stream/rpc: unreliable packet sending (based on https://tools.ietf.org/html/draft-tiesel-quic-unreliable-streams-00)
- [ ] stream/rpc: unencrypted packet sending 
- [ ] conn: optional faster network stack by by-passing kernel (like dpdk)


#### YAGNI
- [ ] conn: optinally enable certificate transparency verification 
  - maybe better just expose certificate data to user


#### WIP: docs
- connection establishment
  - unified rule: connection is only active between first open callback and close callback. but object life cycle is a bit different. 
    - mainly because of the nature of mobile connection, that is, greedy reconnection is required to keep MMO-ish connectivity to the server.
  - connection object life cycle
    - client: active
      - client connection object retained over reconnection. so its possible client connection object alive but connection is not active (eg. on reconnection wait)
      - no need to recreate nq_conn_t or nq_stream/rpc_t on reconnection. its automatically rebound to new session delegate or stream, after establishment.
    - server: passive
      - server connection object is the exactly same life cycle with connection itself
      - disconnect == every resouce related with the connection is deleted. 
- stream type
  - rpc
  - stream
  - raw stream 
- stream establishment
  - client
  - server
- thread safety
