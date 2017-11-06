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
#define NQ_THREADSAFE



// --------------------------
//
// Base type
//
// --------------------------
typedef int16_t nq_result_t;

typedef uint32_t nq_size_t;

typedef uint64_t nq_cid_t;

typedef uint64_t nq_time_t;	//nano seconds timestamp

typedef time_t nq_unix_time_t; //place holder for unix timestamp

typedef uint16_t nq_msgid_t;

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

typedef enum {
	NQ_OK = 0,
	NQ_ESYSCALL = -1,
	NQ_ETIMEOUT = -2,
	NQ_EALLOC = -3,
	NQ_NOT_SUPPORT = -4,
	NQ_GOAWAY = -5,
} nq_error_t;

typedef struct {
	const char *host, *cert, *key, *ca;
	int port;
} nq_addr_t;

typedef void (*nq_logger_t)(const char *, size_t, bool);

//closure
typedef bool (*nq_on_conn_open_t)(void *, nq_conn_t);
//connection closed. after this called, nq_stream_t created by given nq_conn_t, will be invalid.
//nq_conn_t itself will be invalid if this callback return 0, retained otherwise. 
typedef nq_time_t (*nq_on_conn_close_t)(void *, nq_conn_t, nq_result_t, const char*, bool);

typedef bool (*nq_on_stream_open_t)(void *, nq_stream_t);
//stream closed. after this called, nq_stream_t which given to this function will be invalid.
typedef void (*nq_on_stream_close_t)(void *, nq_stream_t);

typedef void *(*nq_stream_reader_t)(void *, const char *, nq_size_t, int *);

typedef nq_size_t (*nq_stream_writer_t)(void *, const void *, nq_size_t, nq_stream_t);

typedef void (*nq_on_stream_record_t)(void *, nq_stream_t, const void *, nq_size_t);

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

		nq_on_stream_open_t on_stream_open;
		nq_on_stream_close_t on_stream_close;
		nq_stream_reader_t stream_reader;
		nq_stream_writer_t stream_writer;

		nq_on_stream_record_t on_stream_record;
		nq_on_rpc_request_t on_rpc_request;

		nq_on_rpc_reply_t on_rpc_reply;
		nq_on_rpc_notify_t on_rpc_notify;

		nq_create_stream_t create_stream;
	};
} nq_closure_t;

#define nq_closure_is_empty(__pclsr) ((__pclsr).ptr == NULL)

extern nq_closure_t nq_closure_empty();

#define nq_closure_init(__pclsr, __type, __cb, __arg) { \
	(__pclsr).arg = (void *)(__arg); \
	(__pclsr).__type = (__cb); \
}

#define nq_closure_call(__pclsr, __type, ...) ((__pclsr).__type((__pclsr).arg, __VA_ARGS__))

//config
typedef struct {
	nq_closure_t on_open, on_close;
} nq_clconf_t;

typedef struct {
	nq_closure_t on_open, on_close;
	const char *quic_secret;
	int quic_cert_cache_size;
} nq_svconf_t;

//handlers
typedef nq_closure_t nq_stream_factory_t;

typedef struct {
	nq_closure_t on_stream_record, on_stream_open, on_stream_close;
	nq_closure_t stream_reader, stream_writer;
} nq_stream_handler_t;

typedef struct {
	nq_closure_t on_rpc_request, on_rpc_notify, on_stream_open, on_stream_close;
} nq_rpc_handler_t;



// --------------------------
//
// client API
//
// --------------------------
// create client object which have max_nfd of connection. 
extern nq_client_t nq_client_create(int max_nfd);
// do actual network IO. need to call periodically
extern void nq_client_poll(nq_client_t cl);
// close connection and destroy client object. after call this, do not call nq_client_* API.
extern void nq_client_destroy(nq_client_t cl);
// create conn from client. server side can get from argument of on_accept handler
// return invalid conn on error, can check with nq_conn_is_valid. 
extern nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf);
// get handler map of the client. 
extern nq_hdmap_t nq_client_hdmap(nq_client_t cl);
// set thread id that calls nq_client_poll.
// call this if thread which polls this nq_client_t is different from creator thread.
extern void nq_client_set_thread(nq_client_t cl);



// --------------------------
//
// server API
//
// --------------------------
//create server which has n_worker of workers
extern nq_server_t nq_server_create(int n_worker);
//listen and returns handler map associated with it. 
extern nq_hdmap_t nq_server_listen(nq_server_t sv, const nq_addr_t *addr, const nq_svconf_t *config);
//if block is true, nq_server_start blocks until some other thread calls nq_server_join. 
extern void nq_server_start(nq_server_t sv, bool block);
//request shutdown and wait for server to stop. after calling this API, do not call nq_server_* API
extern void nq_server_join(nq_server_t sv);



// --------------------------
//
// hdmap API
//
// --------------------------
//setup original stream protocol (client), with 3 pattern
extern bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler);

extern bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler);

extern bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory);



// --------------------------
//
// conn API
//
// --------------------------
//can change handler map of connection, which is usually inherit from nq_client_t or nq_server_t
extern nq_hdmap_t nq_conn_hdmap(nq_conn_t conn);
//close and destroy conn/associated stream eventually, so never touch conn/stream/rpc after calling this API.
extern NQ_THREADSAFE void nq_conn_close(nq_conn_t conn); 
//this just restart connection, never destroy. but associated stream/rpc all destroyed. (client only)
extern NQ_THREADSAFE void nq_conn_reset(nq_conn_t conn); 
//check connection is client mode or not.
extern NQ_THREADSAFE bool nq_conn_is_client(nq_conn_t conn);
//check conn is valid. invalid means fail to create or closed, or temporary disconnected (will reconnect soon).
extern NQ_THREADSAFE bool nq_conn_is_valid(nq_conn_t conn);
//get reconnect wait duration in us. 0 means does not wait reconnection
extern NQ_THREADSAFE uint64_t nq_conn_reconnect_wait(nq_conn_t conn);



// --------------------------
//
// stream API 
//
// --------------------------
//create single stream from conn, which has type specified by "name". need to use valid conn and call from owner thread of it
//return invalid stream on error
extern NQ_THREADSAFE nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name);
//get parent conn from rpc
extern NQ_THREADSAFE nq_conn_t nq_stream_conn(nq_stream_t s);
//check stream is valid. sugar for nq_conn_is_valid(nq_stream_conn(s));
extern NQ_THREADSAFE bool nq_stream_is_valid(nq_stream_t s);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
extern NQ_THREADSAFE void nq_stream_close(nq_stream_t s);
//send arbiter byte array/arbiter object to stream peer. 
extern NQ_THREADSAFE void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen);



// --------------------------
//
// rpc API
//
// --------------------------
//create single rpc stream from conn, which has type specified by "name". need to use valid conn and call from owner thread of it
//return invalid stream on error
extern NQ_THREADSAFE nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name);
//get parent conn from rpc
extern NQ_THREADSAFE nq_conn_t nq_rpc_conn(nq_rpc_t rpc);
//check rpc is valid. sugar for nq_conn_is_valid(nq_rpc_conn(rpc));
extern NQ_THREADSAFE bool nq_rpc_is_valid(nq_rpc_t rpc);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
extern NQ_THREADSAFE void nq_rpc_close(nq_rpc_t rpc);
//send arbiter byte array or object to stream peer. 
extern NQ_THREADSAFE void nq_rpc_call(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen, nq_closure_t on_reply);
//send arbiter byte array or object to stream peer, without receving reply
extern NQ_THREADSAFE void nq_rpc_notify(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen);
//send reply of specified request. result >= 0, data and datalen is response, otherwise error detail
extern NQ_THREADSAFE void nq_rpc_reply(nq_rpc_t rpc, nq_result_t result, nq_msgid_t msgid, const void *data, nq_size_t datalen);



// --------------------------
//
// time API
//
// --------------------------
static inline NQ_THREADSAFE nq_time_t nq_time_sec(uint64_t n) { return ((n) * 1000 * 1000 * 1000); }

static inline NQ_THREADSAFE nq_time_t nq_time_msec(uint64_t n) { return ((n) * 1000 * 1000); }

static inline NQ_THREADSAFE nq_time_t nq_time_usec(uint64_t n) { return ((n) * 1000); }

static inline NQ_THREADSAFE nq_time_t nq_time_nsec(uint64_t n) { return (n); }

extern NQ_THREADSAFE nq_time_t nq_time_now();

extern NQ_THREADSAFE nq_unix_time_t nq_time_unix();

extern NQ_THREADSAFE nq_time_t nq_time_sleep(nq_time_t d); //ignore EINTR

extern NQ_THREADSAFE nq_time_t nq_time_pause(nq_time_t d); //break with EINTR

#if defined(__cplusplus)
}
#endif
