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

typedef uint64_t nq_time_t;	//nano seconds timestamp

typedef time_t nq_unix_time_t; //place holder for unix timestamp

typedef uint32_t nq_msgid_t;

typedef uint32_t nq_stream_id_t;

typedef struct nq_client_tag *nq_client_t; //NqClientLoop

typedef struct nq_server_tag *nq_server_t; //NqServer

typedef struct nq_hdmap_tag *nq_hdmap_t; //nq::HandlerMap

typedef struct nq_conn_tag {
    void *p;    //NqBoxer
    uint64_t s; 
} nq_conn_t;

typedef struct nq_stream_tag {
    void *p;    //NqBoxer
    uint64_t s; 
} nq_stream_t; 

typedef struct nq_rpc_tag { //this is essentially same as nq_stream, but would helpful to prevent misuse of rpc/stream
    void *p;
    uint64_t s; 
} nq_rpc_t; 

//TODO(iyatomi): reduce error code
typedef enum {
	NQ_OK = 0,
	NQ_ESYSCALL = -1,
	NQ_ETIMEOUT = -2,
	NQ_EALLOC = -3,
	NQ_NOT_SUPPORT = -4,
	NQ_GOAWAY = -5,
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
//connection opening and opened. receive handshake progress (only start now) and done event.
//returns false indicates shutdown connection (both server and client)
//TODO(iyatomi): give more imformation for deciding shutdown connection through 4th paramter
typedef bool (*nq_on_conn_open_t)(void *, nq_conn_t, nq_handshake_event_t, void *);
//connection closed. after this called, nq_stream_t created by given nq_conn_t, will be invalid.
//nq_conn_t itself will be invalid if this callback return 0, 
//otherwise, behavior will be differnt between client and server.
typedef nq_time_t (*nq_on_conn_close_t)(void *, nq_conn_t, nq_result_t, const char*, bool);
//connection finalized, by calling nq_conn_close. just after this callback is done, 
//memory corresponding to the nq_conn_t, will freed. 
typedef void (*nq_on_conn_finalize_t)(void *, nq_conn_t);

typedef bool (*nq_on_stream_open_t)(void *, nq_stream_t, void **);
//stream closed. after this called, nq_stream_t which given to this function will be invalid.
typedef void (*nq_on_stream_close_t)(void *, nq_stream_t);

typedef void *(*nq_stream_reader_t)(void *, const char *, nq_size_t, int *);
//stores pointer to serialized byte array last argument. memory for byte array owned by callee and 
//should be available for next call of this callback.
typedef nq_size_t (*nq_stream_writer_t)(void *, nq_stream_t, const void *, nq_size_t, void **);

typedef void (*nq_on_stream_record_t)(void *, nq_stream_t, const void *, nq_size_t);

typedef bool (*nq_on_rpc_open_t)(void *, nq_rpc_t, void **);

typedef void (*nq_on_rpc_close_t)(void *, nq_rpc_t);

typedef void (*nq_on_rpc_request_t)(void *, nq_rpc_t, uint16_t, nq_msgid_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_notify_t)(void *, nq_rpc_t, uint16_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_reply_t)(void *, nq_rpc_t, nq_result_t, const void *, nq_size_t);

typedef void *(*nq_create_stream_t)(void *, nq_conn_t);

typedef struct {
	void *arg;
	union {
		void *ptr;
		nq_on_conn_open_t on_conn_open;
		nq_on_conn_close_t on_conn_close;
		nq_on_conn_finalize_t on_conn_finalize;

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
	};
} nq_closure_t;

#define nq_closure_is_empty(__pclsr) ((__pclsr).ptr == NULL)

nq_closure_t nq_closure_empty();

#define nq_closure_init(__pclsr, __type, __cb, __arg) { \
	(__pclsr).arg = (void *)(__arg); \
	(__pclsr).__type = (__cb); \
}

#define nq_closure_call(__pclsr, __type, ...) ((__pclsr).__type((__pclsr).arg, __VA_ARGS__))

//config
typedef struct {
	//connection open/close watcher
	nq_closure_t on_open, on_close, on_finalize;

	//set true to ignore proof verification
	bool insecure; 
	
	//total handshake time limit / no input limit. default 1000ms/500ms
	nq_time_t handshake_timeout, idle_timeout; 
} nq_clconf_t;

typedef struct {
	//connection open/close watcher
	nq_closure_t on_open, on_close;

	//quic secret. need to specify arbiter (hopefully unique) string
	const char *quic_secret;

	//cert cache size. default 16 and how meny sessions accepted per loop. default 1024
	int quic_cert_cache_size, accept_per_loop;

	//total handshake time limit / no input limit. default 1000ms/500ms
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
	bool use_large_msgid; //use 4byte for msgid
} nq_rpc_handler_t;



// --------------------------
//
// client API
//
// --------------------------
// create client object which have max_nfd of connection. 
NQAPI_THREADSAFE nq_client_t nq_client_create(int max_nfd);
// do actual network IO. need to call periodically
NQAPI_BOOTSTRAP void nq_client_poll(nq_client_t cl);
// close connection and destroy client object. after call this, do not call nq_client_* API.
NQAPI_BOOTSTRAP void nq_client_destroy(nq_client_t cl);
// create conn from client. server side can get from argument of on_accept handler
// return invalid conn on error, can check with nq_conn_is_valid. 
// TODO(iyatomi): make it NQAPI_THREADSAFE
NQAPI_BOOTSTRAP nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf);
// get handler map of the client. 
NQAPI_THREADSAFE nq_hdmap_t nq_client_hdmap(nq_client_t cl);
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
// TODO(iyatomi): make it NQAPI_THREADSAFE
NQAPI_THREADSAFE nq_hdmap_t nq_conn_hdmap(nq_conn_t conn);
//close and destroy conn/associated stream eventually, so never touch conn/stream/rpc after calling this API.
NQAPI_THREADSAFE void nq_conn_close(nq_conn_t conn); 
//this just restart connection, never destroy. but associated stream/rpc all destroyed. (client only)
NQAPI_THREADSAFE void nq_conn_reset(nq_conn_t conn); 
//flush buffered packets
NQAPI_THREADSAFE void nq_conn_flush(nq_conn_t conn);
//check connection is client mode or not.
NQAPI_THREADSAFE bool nq_conn_is_client(nq_conn_t conn);
//check conn is valid. invalid means fail to create or closed, or temporary disconnected (will reconnect soon).
NQAPI_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn);
//get reconnect wait duration in us. 0 means does not wait reconnection
NQAPI_THREADSAFE uint64_t nq_conn_reconnect_wait(nq_conn_t conn);
//get QUIC connection id. CAUTION: for client side, only after on_open and before on_close callback called, it returns valid value.
NQAPI_THREADSAFE nq_cid_t nq_conn_cid(nq_conn_t conn);



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
//check stream is valid. sugar for nq_conn_is_valid(nq_stream_conn(s));
NQAPI_THREADSAFE bool nq_stream_is_valid(nq_stream_t s);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
NQAPI_THREADSAFE void nq_stream_close(nq_stream_t s);
//send arbiter byte array/arbiter object to stream peer. 
NQAPI_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen);
//get QUIC stream id, CAUTION: this is not unique among all stream created. if you need global uniqueness, please use 16 byte value of nq_stream_t
NQAPI_THREADSAFE nq_sid_t nq_stream_sid(nq_stream_t s);
//get context, which is set at on_stream_open
NQAPI_THREADSAFE void *nq_stream_ctx(nq_stream_t s);


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
//check rpc is valid. sugar for nq_conn_is_valid(nq_rpc_conn(rpc));
NQAPI_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
NQAPI_THREADSAFE void nq_rpc_close(nq_rpc_t rpc);
//send arbiter byte array or object to stream peer. 
NQAPI_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply);
//send arbiter byte array or object to stream peer, without receving reply
NQAPI_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen);
//send reply of specified request. result >= 0, data and datalen is response, otherwise error detail
NQAPI_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen);
//get QUIC stream id, CAUTION: this is not unique among all rpc created. if you need global uniqueness, please use 16 byte value of nq_rpc_t
NQAPI_THREADSAFE nq_sid_t nq_rpc_sid(nq_rpc_t s);
//get context, which is set at on_stream_open
NQAPI_THREADSAFE void *nq_rpc_ctx(nq_rpc_t s);


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

#if defined(__cplusplus)
}
#endif
