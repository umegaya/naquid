#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif



// --------------------------
//
// Annotation
//
// --------------------------
//indicate this call only safe before/after main loop running, 
//which means call of nq_server_start or nq_client_poll.
//also not guaranteed to be safe from calling concurrently
#define NQAPI_BOOTSTRAP extern
//indicate this call can be done concurrently, and works correctly
#define NQAPI_THREADSAFE extern
//indicate this call only safe when invoked with nq_conn/rpc/stream_t which passed to
//functions of nq_closure_t. 
#define NQAPI_CLOSURECALL extern
//inline function
#define NQAPI_INLINE static inline



// --------------------------
//
// Base type
//
// --------------------------
typedef uint32_t nq_size_t;

typedef uint64_t nq_cid_t;

typedef uint64_t nq_sid_t;

typedef uint64_t nq_time_t; //nano seconds timestamp

typedef time_t nq_unix_time_t; //place holder for unix timestamp

typedef uint32_t nq_msgid_t;

typedef uint32_t nq_stream_id_t;

typedef struct nq_client_tag *nq_client_t; //NqClientLoop

typedef struct nq_server_tag *nq_server_t; //NqServer

typedef struct nq_hdmap_tag *nq_hdmap_t; //nq::HandlerMap

typedef struct {
  uint64_t data[1];
} nq_serial_t;

typedef struct nq_conn_tag {
    nq_serial_t s; //see NqConnSerialCodec
    void *p;    //NqSession::Delegate
} nq_conn_t;

typedef struct nq_stream_tag {
    nq_serial_t s; //see NqStreamSerialCodec
    void *p;    //NqStream
} nq_stream_t; 
//this is essentially same as nq_stream, but would helpful to prevent misuse of rpc/stream
typedef struct nq_rpc_tag {
    nq_serial_t s; //see NqStreamSerialCodec
    void *p;    //NqStream
} nq_rpc_t; 

typedef struct nq_alarm_tag {
  nq_serial_t s;   //see NqAlarmSerialCodec
  void *p;      //NqAlarm
} nq_alarm_t;

typedef enum {
  NQ_OK = 0,
  NQ_ESYSCALL = -1,
  NQ_ETIMEOUT = -2,
  NQ_EALLOC = -3,
  NQ_ENOTSUPPORT = -4,
  NQ_EGOAWAY = -5,
  NQ_EQUIC = -6,  //quic library error
  NQ_EUSER = -7,  //for rpc, user calls nq_rpc_error to reply
} nq_error_t;
//quic library error
typedef int nq_quic_error_t;
NQAPI_THREADSAFE const char *nq_quic_error_str(nq_quic_error_t code);

typedef enum {
  NQ_HS_START = 0, //client: client is about to send first packet, server: server receive initial packet
  NQ_HS_DONE = 10,  //client: receive SHLO, server: accept CHLO
} nq_handshake_event_t;

typedef struct {
  const char *host, *cert, *key, *ca;
  int port;
} nq_addr_t;

typedef enum {
  NQ_NOT_REACHABLE = 0,
  NQ_REACHABLE_WIFI = 2,
  NQ_REACHABLE_WWAN = 1,
} nq_reachability_t;



// --------------------------
//
// Closure type
//
// --------------------------
/* client */
//receive client handshake progress and done event. note that this usucally called twice to establish connection.
//optionally you can set arbiter pointer via last argument, which can be retrieved via nq_conn_ctx afterward.
//TODO(iyatomi): give more imformation for deciding shutdown connection from nq_conn_t
//TODO(iyatomi): re-evaluate we should call this twice (now mainly because to make open/close callback surely called as pair)
typedef void (*nq_on_client_conn_open_t)(void *, nq_conn_t, nq_handshake_event_t, void **);
//client connection closed. after this called, nq_stream_t/nq_rpc_t created by given nq_conn_t, will be invalid.
//last boolean indicates connection is closed from local or remote. if this function returns positive value, 
//connection automatically reconnect with back off which equals to returned value.
typedef nq_time_t (*nq_on_client_conn_close_t)(void *, nq_conn_t, nq_quic_error_t, const char*, bool);
//client connection finalized. just after this callback is done, memory corresponding to the nq_conn_t, will be freed. 
//because nq_conn_t is already invalidate when this callback invokes, almost nq_conn_* API returns invalid value in this callback.
//so the callback is basically for cleanup user defined resourse, like closure arg pointer (1st arg) or user context (3rd arg).
typedef void (*nq_on_client_conn_finalize_t)(void *, nq_conn_t, void *);


/* server */
//server connection opened. same as nq_on_client_conn_open_t.
typedef nq_on_client_conn_open_t nq_on_server_conn_open_t;
//server connection closed. same as nq_on_client_conn_close_t but no reconnection feature
typedef void (*nq_on_server_conn_close_t)(void *, nq_conn_t, nq_quic_error_t, const char*, bool);


/* conn */
//called as 2nd argument nq_conn_valid, when actually given conn is valid.
typedef void (*nq_on_conn_validate_t)(void *, nq_conn_t, const char *);
//called when nq_conn_modify_hdmap invoked with valid nq_conn_t
typedef void (*nq_on_conn_modify_hdmap_t)(void *, nq_hdmap_t);


/* stream */
//stream opened. return false to reject stream
typedef bool (*nq_on_stream_open_t)(void *, nq_stream_t, void**);
//stream closed. after this called, nq_stream_t which given to this function will be invalid.
typedef void (*nq_on_stream_close_t)(void *, nq_stream_t);

typedef void *(*nq_stream_reader_t)(void *, nq_stream_t, const char *, nq_size_t, int *);
//need to return pointer to serialized byte array via last argument. 
//memory for byte array owned by callee and have to be available until next call of this callback.
typedef nq_size_t (*nq_stream_writer_t)(void *, nq_stream_t, const void *, nq_size_t, void **);

typedef void (*nq_on_stream_record_t)(void *, nq_stream_t, const void *, nq_size_t);

typedef void (*nq_on_stream_task_t)(void *, nq_stream_t);

typedef void (*nq_on_stream_ack_t)(void *, int, nq_time_t);

typedef void (*nq_on_stream_retransmit_t)(void *, int);
//called as 2nd argument nq_stream_valid, when actually given stream is valid.
typedef void (*nq_on_stream_validate_t)(void *, nq_stream_t, const char *);

typedef void *(*nq_create_stream_t)(void *, nq_conn_t);


/* rpc */
//rpc opened. return false to reject rpc
typedef bool (*nq_on_rpc_open_t)(void *, nq_rpc_t, void**);
//rpc closed. after this called, nq_stream_t which given to this function will be invalid.
typedef void (*nq_on_rpc_close_t)(void *, nq_rpc_t);

typedef void (*nq_on_rpc_request_t)(void *, nq_rpc_t, uint16_t, nq_msgid_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_notify_t)(void *, nq_rpc_t, uint16_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_reply_t)(void *, nq_rpc_t, nq_error_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_task_t)(void *, nq_rpc_t);
//called as 2nd argument nq_stream_valid, when actually given stream is valid.
typedef void (*nq_on_rpc_validate_t)(void *, nq_rpc_t, const char *);


/* alarm */
typedef void (*nq_on_alarm_t)(void *, nq_time_t *);

/* reachability */
typedef void (*nq_on_reachability_change_t)(void *, nq_reachability_t);


/* closure */
typedef struct {
  void *arg;
  union {
    void *ptr;
    nq_on_client_conn_open_t on_client_conn_open;
    nq_on_client_conn_close_t on_client_conn_close;
    nq_on_client_conn_finalize_t on_client_conn_finalize;

    nq_on_server_conn_open_t on_server_conn_open;
    nq_on_server_conn_close_t on_server_conn_close;

    nq_on_conn_validate_t on_conn_validate;
    nq_on_conn_modify_hdmap_t on_conn_modify_hdmap;

    nq_on_stream_open_t on_stream_open;
    nq_on_stream_close_t on_stream_close;
    nq_stream_reader_t stream_reader;
    nq_stream_writer_t stream_writer;
    nq_on_stream_record_t on_stream_record;
    nq_on_stream_task_t on_stream_task;
    nq_on_stream_ack_t on_stream_ack;
    nq_on_stream_retransmit_t on_stream_retransmit;
    nq_on_stream_validate_t on_stream_validate;

    nq_on_rpc_open_t on_rpc_open;
    nq_on_rpc_close_t on_rpc_close;
    nq_on_rpc_request_t on_rpc_request;
    nq_on_rpc_reply_t on_rpc_reply;
    nq_on_rpc_notify_t on_rpc_notify;
    nq_on_rpc_task_t on_rpc_task;
    nq_on_rpc_validate_t on_rpc_validate;

    nq_create_stream_t create_stream;

    nq_on_alarm_t on_alarm;

    nq_on_reachability_change_t on_reachability_change;
  };
} nq_closure_t;

NQAPI_THREADSAFE bool nq_closure_is_empty(nq_closure_t clsr);

NQAPI_THREADSAFE nq_closure_t nq_closure_empty();

#define nq_closure_init(__pclsr, __type, __cb, __arg) { \
  (__pclsr).arg = (void *)(__arg); \
  (__pclsr).__type = (__cb); \
}

#define nq_closure_call(__pclsr, __type, ...) ((__pclsr).__type((__pclsr).arg, __VA_ARGS__))



// --------------------------
//
// client API
//
// --------------------------
typedef struct {
  //connection open/close/finalize watcher
  nq_closure_t on_open, on_close, on_finalize;

  //set true to ignore proof verification
  bool insecure; 

  //track reachability to the provide hostname and recreate socket if changed.
  //useful for mobile connection. currently iOS only. use nq_conn_reachability_change for android.
  bool track_reachability;
  
  //total handshake time limit / no input limit. default 1000ms/500ms
  nq_time_t handshake_timeout, idle_timeout; 
} nq_clconf_t;

// create client object which have max_nfd of connection. 
NQAPI_BOOTSTRAP nq_client_t nq_client_create(int max_nfd, int max_stream_hint);
// do actual network IO. need to call periodically
NQAPI_BOOTSTRAP void nq_client_poll(nq_client_t cl);
// close connection and destroy client object. after call this, do not call nq_client_* API.
NQAPI_BOOTSTRAP void nq_client_destroy(nq_client_t cl);
// create conn from client. server side can get from argument of on_accept handler
// return invalid conn on error, can check with nq_conn_is_valid. 
// TODO(iyatomi): make it NQAPI_THREADSAFE
NQAPI_BOOTSTRAP nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf);
// get handler map of the client. 
NQAPI_BOOTSTRAP nq_hdmap_t nq_client_hdmap(nq_client_t cl);
// set thread id that calls nq_client_poll.
// call this if thread which polls this nq_client_t is different from creator thread.
NQAPI_BOOTSTRAP void nq_client_set_thread(nq_client_t cl);



// --------------------------
//
// server API
//
// --------------------------
typedef struct {
  //connection open/close watcher
  nq_closure_t on_open, on_close;

  //quic secret. need to specify arbiter (hopefully unique) string
  const char *quic_secret;

  //cert cache size. default 16 and how meny sessions accepted per loop. default 1024
  int quic_cert_cache_size, accept_per_loop;

  //allocation hint about max sessoin and max stream
  int max_session_hint, max_stream_hint;

  //total handshake time limit / no input limit. default 1000ms/5000ms
  nq_time_t handshake_timeout, idle_timeout; 
} nq_svconf_t;

//create server which has n_worker of workers
NQAPI_BOOTSTRAP nq_server_t nq_server_create(int n_worker);
//listen and returns handler map associated with it. 
NQAPI_BOOTSTRAP nq_hdmap_t nq_server_listen(nq_server_t sv, const nq_addr_t *addr, const nq_svconf_t *config);
//if block is true, nq_server_start blocks until some other thread calls nq_server_join. 
NQAPI_BOOTSTRAP void nq_server_start(nq_server_t sv, bool block);
//request shutdown and wait for server to stop. after calling this API, do not call nq_server_* API
NQAPI_BOOTSTRAP void nq_server_join(nq_server_t sv);



// --------------------------
//
// hdmap API
//
// --------------------------
typedef nq_closure_t nq_stream_factory_t;

typedef struct {
  nq_closure_t on_stream_record, on_stream_open, on_stream_close;
  nq_closure_t stream_reader, stream_writer;
} nq_stream_handler_t;

typedef struct {
  nq_closure_t on_rpc_request, on_rpc_notify, on_rpc_open, on_rpc_close;
  nq_time_t timeout; //call timeout
  bool use_large_msgid; //use 4byte for msgid
} nq_rpc_handler_t;

//setup original stream protocol (client), with 3 pattern
NQAPI_BOOTSTRAP bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler);

NQAPI_BOOTSTRAP bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler);

NQAPI_BOOTSTRAP bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory);
//if you call this API, nq_hdmap_t become "raw mode". any other hdmap settings are ignored, 
//and all incoming/outgoing streams are handled with the handler which is given to this API.
NQAPI_BOOTSTRAP void nq_hdmap_raw_handler(nq_hdmap_t h, nq_stream_handler_t handler);



// --------------------------
//
// conn API
//
// --------------------------
//can modify handler map of connection, which is usually inherit from nq_client_t or nq_server_t
NQAPI_THREADSAFE void nq_conn_modify_hdmap(nq_conn_t conn, nq_closure_t modifier);
//close and destroy conn/associated stream eventually, so never touch conn/stream/rpc after calling this API.
NQAPI_THREADSAFE void nq_conn_close(nq_conn_t conn); 
//this just restart connection, if connection not start, start it, otherwise close connection once, then start again.
//it never destroy connection itself, but associated stream/rpc all destroyed. (client only)
NQAPI_THREADSAFE void nq_conn_reset(nq_conn_t conn); 
//flush buffered packets of all stream
NQAPI_THREADSAFE void nq_conn_flush(nq_conn_t conn);
//check connection is client mode or not.
NQAPI_THREADSAFE bool nq_conn_is_client(nq_conn_t conn);
//check conn is valid. invalid means fail to create or closed, or temporary disconnected (will reconnect soon).
//note that if (nq_conn_is_valid(...)) does not assure any safety of following operation, when multi threaded event loop runs
//you should give cb parameter with filling nq_on_conn_validate member, to operate this conn safety on validation success.
//you can pass nq_closure_empty() for nq_conn_is_valid, if you dont need to callback.
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn, nq_closure_t cb);
//get reconnect wait duration in us. 0 means does not wait reconnection
NQAPI_THREADSAFE nq_time_t nq_conn_reconnect_wait(nq_conn_t conn);
//get context, which is set at on_conn_open
NQAPI_CLOSURECALL void *nq_conn_ctx(nq_conn_t conn);
//check equality of nq_conn_t.
NQAPI_INLINE bool nq_conn_equal(nq_conn_t c1, nq_conn_t c2) { return c1.s.data[0] == c2.s.data[0] && (c1.s.data[0] == 0 || c1.p == c2.p); }
//manually set reachability change for current connection
NQAPI_THREADSAFE void nq_conn_reachability_change(nq_conn_t conn, nq_reachability_t new_status);

NQAPI_THREADSAFE int nq_conn_fd(nq_conn_t conn);


// --------------------------
//
// stream API 
//
// --------------------------
typedef struct {
  nq_closure_t on_ack;
  nq_closure_t on_retransmit;
} nq_stream_opt_t;

//create single stream from conn, which has type specified by "name". need to use valid conn && call from owner thread of it
//return invalid stream on error, ctx will be void **ppctx of open callback of this stream handler
NQAPI_THREADSAFE void nq_conn_stream(nq_conn_t conn, const char *name, void *ctx);
//get parent conn from rpc
NQAPI_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s);
//get alarm from stream
NQAPI_THREADSAFE nq_alarm_t nq_stream_alarm(nq_stream_t s);
//check stream is valid. note that if (nq_stream_is_valid(...)) does not assure any safety of following operation.
//you should give cb parameter with filling nq_on_stream_validate member, to operate this stream safety on validation success.
//you can pass nq_closure_empty() for nq_conn_is_valid, if you dont need to callback.
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s, nq_closure_t cb);
//check stream is outgoing. otherwise incoming. optionally you can get stream is valid, via p_valid. 
//if p_valid returns true, means stream is incoming.
NQAPI_THREADSAFE bool nq_stream_outgoing(nq_stream_t s, bool *p_valid);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s);
//send arbiter byte array/arbiter object to stream peer. if you want ack for each send, use nq_stream_send_ex
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen);
//send arbiter byte array/arbiter object to stream peer, and can receive ack of it.
NQAPI_THREADSAFE void nq_stream_send_ex(nq_stream_t s, const void *data, nq_size_t datalen, nq_stream_opt_t *opt);
//schedule execution of closure which is given to cb, will called with given s.
NQAPI_THREADSAFE void nq_stream_task(nq_stream_t s, nq_closure_t cb);
//check equality of nq_stream_t.
NQAPI_INLINE bool nq_stream_equal(nq_stream_t c1, nq_stream_t c2) { return c1.s.data[0] == c2.s.data[0] && (c1.s.data[0] == 0 || c1.p == c2.p); }
//get stream id. this may change as you re-created stream on reconnection. 
//useful if you need to give special meaning to specified stream_id, like http2 over quic
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s);
//get context, which is set at nq_conn_stream. only safe with nq_stream_t which passed to closure callbacks
NQAPI_CLOSURECALL void *nq_stream_ctx(nq_stream_t s);



// --------------------------
//
// rpc API
//
// --------------------------
typedef struct {
  nq_closure_t callback;
  nq_time_t timeout;
} nq_rpc_opt_t;

//create single rpc stream from conn, which has type specified by "name". need to use valid conn && call from owner thread of it
//return invalid stream on error. ctx will be void **ppctx of open callback of this stream handler
NQAPI_THREADSAFE void nq_conn_rpc(nq_conn_t conn, const char *name, void *ctx);
//get parent conn from rpc
NQAPI_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc);
//get alarm from stream or rpc
NQAPI_THREADSAFE nq_alarm_t nq_rpc_alarm(nq_rpc_t rpc);
//check rpc is valid. note that if (nq_rpc_is_valid(...)) does not assure any safety of following operation.
//you should give cb parameter with filling nq_on_rpc_validate member, to operate this rpc safety on validation success.
//you can pass nq_closure_empty() for nq_conn_is_valid, if you dont need to callback.
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc, nq_closure_t cb);
//check rpc is outgoing. otherwise incoming. optionally you can get stream is valid, via p_valid. 
//if p_valid returns true, means stream is incoming.
NQAPI_THREADSAFE bool nq_rpc_outgoing(nq_rpc_t s, bool *p_valid);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc);
//send arbiter byte array or object to stream peer. type should be positive
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply);
//same as nq_rpc_call but can specify various options like per call timeout
NQAPI_THREADSAFE void nq_rpc_call_ex(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_rpc_opt_t *opts);
//send arbiter byte array or object to stream peer, without receving reply. type should be positive
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen);
//send reply of specified request. result >= 0, data and datalen is response, otherwise error detail
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_msgid_t msgid, const void *data, nq_size_t datalen);
//send error response to specified request. data and datalen is error detail
NQAPI_THREADSAFE void nq_rpc_error(nq_rpc_t rpc, nq_msgid_t msgid, const void *data, nq_size_t datalen);
//schedule execution of closure which is given to cb, will called with given rpc.
NQAPI_THREADSAFE void nq_rpc_task(nq_rpc_t rpc, nq_closure_t cb);
//check equality of nq_rpc_t.
NQAPI_INLINE bool nq_rpc_equal(nq_rpc_t c1, nq_rpc_t c2) { return c1.s.data[0] == c2.s.data[0] && (c1.s.data[0] == 0 || c1.p == c2.p); }
//get rpc id. this may change as you re-created rpc on reconnection.
//useful if you need to give special meaning to specified stream_id, like http2 over quic
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t rpc);
//get context, which is set at nq_conn_rpc. only safe with nq_rpc_t which passed to closure callbacks
NQAPI_CLOSURECALL void *nq_rpc_ctx(nq_rpc_t s);



// --------------------------
//
// time API
//
// --------------------------
NQAPI_INLINE nq_time_t nq_time_sec(uint64_t n) { return ((n) * 1000 * 1000 * 1000); }

NQAPI_INLINE nq_time_t nq_time_msec(uint64_t n) { return ((n) * 1000 * 1000); }

NQAPI_INLINE nq_time_t nq_time_usec(uint64_t n) { return ((n) * 1000); }

NQAPI_INLINE nq_time_t nq_time_nsec(uint64_t n) { return (n); }

NQAPI_THREADSAFE nq_time_t nq_time_now();

NQAPI_THREADSAFE nq_unix_time_t nq_time_unix();
//ignore EINTR
NQAPI_THREADSAFE nq_time_t nq_time_sleep(nq_time_t d);
//break with EINTR
NQAPI_THREADSAFE nq_time_t nq_time_pause(nq_time_t d);



// --------------------------
//
// alarm API
//
// --------------------------
#define STOP_INVOKE_NQ_TIME (0)
//configure alarm to invoke cb after current time exceeds first, 
//at thread which handle receive callback of nq_rpc/stream_t that creates this alarm.
//if you set next invocation timestamp value(>= input value) to 3rd argument of cb, alarm scheduled to run that time, 
//if you set the value to 0(STOP_INVOKE_NQ_TIME), it stopped (still valid and can reactivate with nq_alarm_set). 
//otherwise alarm remove its memory, further use of nq_alarm_t is not possible (silently ignored)
//suitable if you want to create some kind of poll method of your connection.
NQAPI_THREADSAFE void nq_alarm_set(nq_alarm_t a, nq_time_t first, nq_closure_t cb);
//destroy alarm. after call this, any attempt to call nq_alarm_set will be ignored.
NQAPI_THREADSAFE void nq_alarm_destroy(nq_alarm_t a);
//check if alarm is valid
NQAPI_THREADSAFE bool nq_alarm_is_valid(nq_alarm_t a);



// --------------------------
//
// log API
//
// --------------------------
//log handler. 
typedef void (*nq_logger_t)(const char *, size_t);
//log configuration
typedef struct {
  //(possibly) unique identifier of log stream which is created by single process
  const char *id;

  //if set to true, you need to call nq_log_flush() periodically to actually output logs.
  //useful for some environement like Unity Editor, which cannot call logging API from non-main thread.
  //https://fogbugz.unity3d.com/default.asp?949512_dab6v5ranqbebqr5
  bool manual_flush;

  //log handler
  nq_logger_t callback;
} nq_logconf_t;
//log severity
typedef enum {
  NQ_LOGLV_TRACE,
  NQ_LOGLV_DEBUG,
  NQ_LOGLV_INFO,
  NQ_LOGLV_WARN,
  NQ_LOGLV_ERROR,
  NQ_LOGLV_FATAL,            
  NQ_LOGLV_REPORT, //intend to use for msg that is very important, but not error.
  NQ_LOGLV_MAX,
} nq_loglv_t;
//structured log param
typedef enum {
  NQ_LOG_INTEGER,
  NQ_LOG_STRING,
  NQ_LOG_DECIMAL,
  NQ_LOG_BOOLEAN,
} nq_logparam_type_t;
typedef struct {
  const char *key;
  nq_logparam_type_t type;
  union {
    double d;
    uint64_t n;
    const char *s;
    bool b;
  } value;
} nq_logparam_t;

NQAPI_BOOTSTRAP void nq_log_config(const nq_logconf_t *conf);
//write JSON structured log output. 
NQAPI_THREADSAFE void nq_log(nq_loglv_t lv, const char *msg, nq_logparam_t *params, int n_params);
//write JSON Structured log output, with only msg
NQAPI_INLINE void nq_msg(nq_loglv_t lv, const char *msg) { nq_log(lv, msg, NULL, 0); }
//flush cached log. only enable if you configure manual_flush to true. 
//recommend to call from only one thread. otherwise log output order may change from actual order.
NQAPI_THREADSAFE void nq_log_flush(); 



#if defined(__cplusplus)
}
#endif
