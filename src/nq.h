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



// --------------------------
//
// Base type
//
// --------------------------
typedef int16_t nq_result_t;

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

typedef struct nq_conn_tag {
    void *p;    //NqSession::Delegate
    uint64_t s; //session_index (0-30bit) | reserved (31-63bit)
} nq_conn_t;

typedef struct nq_stream_tag {
    void *p;    //NqSessoin::Delegate(client)/NqServerStream(server)
    uint64_t s; //session_index (0-30bit) | client_flag (1bit) | stream_index (32-63bit)
} nq_stream_t; 

typedef struct nq_rpc_tag { //this is essentially same as nq_stream, but would helpful to prevent misuse of rpc/stream
    void *p;    //NqSessoin::Delegate(client)/NqServerStream(server)
    uint64_t s; //session_index (0-30bit) | client_flag (1bit) | stream_index (32-63bit)
} nq_rpc_t; 

typedef struct nq_alarm_tag {
  void *p;      //NqAlarm
  uint64_t s;   //alarm_index (0-31bit) | reserved (32 - 63bit)
} nq_alarm_t;

//TODO(iyatomi): reduce error code
typedef enum {
  NQ_OK = 0,
  NQ_ESYSCALL = -1,
  NQ_ETIMEOUT = -2,
  NQ_EALLOC = -3,
  NQ_ENOTSUPPORT = -4,
  NQ_EGOAWAY = -5,
} nq_error_t;

typedef enum {
  NQ_HS_START = 0, //client: client send first packet, server: server receive initial packet
  NQ_HS_DONE = 10,  //client: receive SHLO, server: accept CHLO
} nq_handshake_event_t;

typedef struct {
  const char *host, *cert, *key, *ca;
  int port;
} nq_addr_t;

typedef void (*nq_logger_t)(const char *, size_t, bool);

//closure


//receive client handshake progress and done event.
//optionally you can set arbiter pointer via last argument, which can be retrieved via nq_conn_ctx afterward.
//TODO(iyatomi): give more imformation for deciding shutdown connection from nq_conn_t
typedef void (*nq_on_client_conn_open_t)(void *, nq_conn_t, nq_handshake_event_t, void **);
//client connection closed. after this called, nq_stream_t/nq_rpc_t created by given nq_conn_t, will be invalid.
//last boolean indicates connection is closed from local or remote. if this function returns positive value, 
//connection automatically reconnect with back off which equals to returned value.
typedef nq_time_t (*nq_on_client_conn_close_t)(void *, nq_conn_t, nq_result_t, const char*, bool);
//client connection finalized. just after this callback is done, memory corresponding to the nq_conn_t, will be freed. 
//because nq_conn_t is already invalidate when this callback invokes, almost nq_conn_* API returns invalid value in this callback.
//so the callback is basically for cleanup user defined resourse, like closure arg pointer (1st arg) or user context (3rd arg).
typedef void (*nq_on_client_conn_finalize_t)(void *, nq_conn_t, void *);


//server connection opened. same as nq_on_client_conn_open_t.
typedef nq_on_client_conn_open_t nq_on_server_conn_open_t;
//server connection closed. no reconnection feature
typedef void (*nq_on_server_conn_close_t)(void *, nq_conn_t, nq_result_t, const char*, bool);


//stream opened. return false to reject stream
typedef bool (*nq_on_stream_open_t)(void *, nq_stream_t, void **);
//stream closed. after this called, nq_stream_t which given to this function will be invalid.
typedef void (*nq_on_stream_close_t)(void *, nq_stream_t);

typedef void *(*nq_stream_reader_t)(void *, const char *, nq_size_t, int *);
//stores pointer to serialized byte array last argument. memory for byte array owned by callee and 
//should be available for next call of this callback.
typedef nq_size_t (*nq_stream_writer_t)(void *, nq_stream_t, const void *, nq_size_t, void **);

typedef void (*nq_on_stream_record_t)(void *, nq_stream_t, const void *, nq_size_t);


//rpc opened. return false to reject 
typedef bool (*nq_on_rpc_open_t)(void *, nq_rpc_t, void **);
//rpc closed. after this called, nq_stream_t which given to this function will be invalid.
typedef void (*nq_on_rpc_close_t)(void *, nq_rpc_t);

typedef void (*nq_on_rpc_request_t)(void *, nq_rpc_t, uint16_t, nq_msgid_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_notify_t)(void *, nq_rpc_t, uint16_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_reply_t)(void *, nq_rpc_t, nq_result_t, const void *, nq_size_t);

typedef void *(*nq_create_stream_t)(void *, nq_conn_t);

typedef void (*nq_on_alarm_t)(void *, nq_time_t *);

typedef struct {
  void *arg;
  union {
    void *ptr;
    nq_on_client_conn_open_t on_client_conn_open;
    nq_on_client_conn_close_t on_client_conn_close;
    nq_on_client_conn_finalize_t on_client_conn_finalize;

    nq_on_server_conn_open_t on_server_conn_open;
    nq_on_server_conn_close_t on_server_conn_close;

    nq_on_stream_open_t on_stream_open;
    nq_on_stream_close_t on_stream_close;
    nq_stream_reader_t stream_reader;
    nq_stream_writer_t stream_writer;
    nq_on_stream_record_t on_stream_record;

    nq_on_rpc_open_t on_rpc_open;
    nq_on_rpc_close_t on_rpc_close;
    nq_on_rpc_request_t on_rpc_request;
    nq_on_rpc_reply_t on_rpc_reply;
    nq_on_rpc_notify_t on_rpc_notify;

    nq_create_stream_t create_stream;
    nq_on_alarm_t on_alarm;
  };
} nq_closure_t;

NQAPI_THREADSAFE bool nq_closure_is_empty(nq_closure_t clsr);

NQAPI_THREADSAFE nq_closure_t nq_closure_empty();

#define nq_closure_init(__pclsr, __type, __cb, __arg) { \
  (__pclsr).arg = (void *)(__arg); \
  (__pclsr).__type = (__cb); \
}

#define nq_closure_call(__pclsr, __type, ...) ((__pclsr).__type((__pclsr).arg, __VA_ARGS__))

//config
typedef struct {
  //connection open/close/finalize watcher
  nq_closure_t on_open, on_close, on_finalize;

  //set true to ignore proof verification
  bool insecure; 

  //NYI: set true to use raw connection, which does not send stream name to specify stream type
  //it just callbacks/sends packet as it is. TODO(iyatomi): implement
  bool raw;
  
  //total handshake time limit / no input limit. default 1000ms/500ms
  nq_time_t handshake_timeout, idle_timeout; 
} nq_clconf_t;

typedef struct {
  //connection open/close watcher
  nq_closure_t on_open, on_close;

  //quic secret. need to specify arbiter (hopefully unique) string
  const char *quic_secret;

  //NYI: set true to use raw connection, which does not accept stream name to specify stream type.
  //it just callbacks/sends packet as it is. TODO(iyatomi): implement
  bool raw;

  //cert cache size. default 16 and how meny sessions accepted per loop. default 1024
  int quic_cert_cache_size, accept_per_loop;

  //allocation hint about max sessoin and max stream
  int max_session_hint, max_stream_hint;

  //total handshake time limit / no input limit. default 1000ms/5000ms
  nq_time_t handshake_timeout, idle_timeout; 
} nq_svconf_t;

//handlers
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

typedef struct {
  nq_closure_t callback;
  nq_time_t timeout;
} nq_rpc_opt_t;



// --------------------------
//
// client API
//
// --------------------------
// create client object which have max_nfd of connection. 
NQAPI_THREADSAFE nq_client_t nq_client_create(int max_nfd, int max_stream_hint);
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
//create server which has n_worker of workers
NQAPI_THREADSAFE nq_server_t nq_server_create(int n_worker);
//listen and returns handler map associated with it. 
// TODO(iyatomi): make it NQAPI_THREADSAFE
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
//setup original stream protocol (client), with 3 pattern
// TODO(iyatomi): make it NQAPI_THREADSAFE
NQAPI_BOOTSTRAP bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler);

NQAPI_BOOTSTRAP bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler);

NQAPI_BOOTSTRAP bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory);



// --------------------------
//
// conn API
//
// --------------------------
//can change handler map of connection, which is usually inherit from nq_client_t or nq_server_t
NQAPI_THREADSAFE nq_hdmap_t nq_conn_hdmap(nq_conn_t conn);
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
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn);
//get reconnect wait duration in us. 0 means does not wait reconnection
NQAPI_THREADSAFE nq_time_t nq_conn_reconnect_wait(nq_conn_t conn);
//get context, which is set at on_conn_open
NQAPI_THREADSAFE void *nq_conn_ctx(nq_conn_t conn);
//check equality of nq_conn_t
static inline bool nq_conn_equal(nq_conn_t c1, nq_conn_t c2) { return c1.p == c2.p && c1.s == c2.s; }



// --------------------------
//
// stream API 
//
// --------------------------
//create single stream from conn, which has type specified by "name". need to use valid conn && call from owner thread of it
//return invalid stream on error
NQAPI_THREADSAFE nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name);
//get parent conn from rpc
NQAPI_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s);
//get alarm from stream
NQAPI_THREADSAFE nq_alarm_t nq_stream_alarm(nq_stream_t s);
//check stream is valid. sugar for nq_conn_is_valid(nq_stream_conn(s));
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s);
//send arbiter byte array/arbiter object to stream peer. 
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen);
//get context, which is set at on_stream_open
NQAPI_THREADSAFE void *nq_stream_ctx(nq_stream_t s);
//check equality of nq_stream_t
static inline bool nq_stream_equal(nq_stream_t c1, nq_stream_t c2) { return c1.p == c2.p && c1.s == c2.s; }
//will deprecate
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s);
NQAPI_THREADSAFE const char *nq_stream_name(nq_stream_t s);



// --------------------------
//
// rpc API
//
// --------------------------
//create single rpc stream from conn, which has type specified by "name". need to use valid conn && call from owner thread of it
//return invalid stream on error
NQAPI_THREADSAFE nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name);
//get parent conn from rpc
NQAPI_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc);
//get alarm from stream or rpc
NQAPI_THREADSAFE nq_alarm_t nq_rpc_alarm(nq_rpc_t rpc);
//check rpc is valid. sugar for nq_conn_is_valid(nq_rpc_conn(rpc));
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc);
//send arbiter byte array or object to stream peer. type should be positive
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply);
//same as nq_rpc_call but can specify various options like per call timeout
NQAPI_THREADSAFE void nq_rpc_call_ex(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen, nq_rpc_opt_t *opts);
//send arbiter byte array or object to stream peer, without receving reply. type should be positive
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, int16_t type, const void *data, nq_size_t datalen);
//send reply of specified request. result >= 0, data and datalen is response, otherwise error detail
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen);
//get context, which is set at on_stream_open
NQAPI_THREADSAFE void *nq_rpc_ctx(nq_rpc_t s);
//check equality of nq_rpc_t
static inline bool nq_rpc_equal(nq_rpc_t c1, nq_rpc_t c2) { return c1.p == c2.p && c1.s == c2.s; }
//will deprecate
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t rpc);
NQAPI_THREADSAFE const char *nq_rpc_name(nq_rpc_t rpc);



// --------------------------
//
// time API
//
// --------------------------
static inline nq_time_t nq_time_sec(uint64_t n) { return ((n) * 1000 * 1000 * 1000); }

static inline nq_time_t nq_time_msec(uint64_t n) { return ((n) * 1000 * 1000); }

static inline nq_time_t nq_time_usec(uint64_t n) { return ((n) * 1000); }

static inline nq_time_t nq_time_nsec(uint64_t n) { return (n); }

NQAPI_THREADSAFE nq_time_t nq_time_now();

NQAPI_THREADSAFE nq_unix_time_t nq_time_unix();

NQAPI_THREADSAFE nq_time_t nq_time_sleep(nq_time_t d); //ignore EINTR

NQAPI_THREADSAFE nq_time_t nq_time_pause(nq_time_t d); //break with EINTR

#define STOP_INVOKE_NQ_TIME (0)
//configure alarm to invoke cb after current time exceeds first, 
//at thread which handle receive callback of nq_rpc/stream_t that creates this alarm.
//if you set next invocation timestamp value(>= input value) to 3rd argument of cb, alarm scheduled to run that time, 
//if you set the value to 0(STOP_INVOKE_NQ_TIME), it stopped (still valid and can reactivate with nq_alarm_set). 
//otherwise alarm remove its memory, further use of nq_alarm_t will possibly cause crash
//suitable if you want to create some kind of poll method of your connection.
NQAPI_THREADSAFE void nq_alarm_set(nq_alarm_t a, nq_time_t first, nq_closure_t cb);
//destroy alarm. if you call nq_alarm_set after the alarm already called nq_alarm_destroy, it will possibly crash
NQAPI_THREADSAFE void nq_alarm_destroy(nq_alarm_t a);

#if defined(__cplusplus)
}
#endif
