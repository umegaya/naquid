#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// --------------------------
//
// Base type
//
// --------------------------
typedef int32_t nq_result_t;

typedef uint32_t nq_size_t;

typedef uint64_t nq_cid_t;

typedef uint64_t nq_time_t;

typedef uint32_t nq_msgid_t;

typedef uint32_t nq_stream_id_t;

typedef struct nq_client_tag *nq_client_t; //NaquidClientLoop

typedef struct nq_server_tag *nq_server_t; //NaquidServer

typedef struct nq_conn_tag *nq_conn_t; //NaquidSession::Delegate

typedef struct nq_hdmap_tag *nq_hdmap_t; //nq::HandlerMap

typedef struct nq_stream_tag *nq_stream_t; //NaquidStreamHandler

typedef struct nq_rpc_tag *nq_rpc_t; //NaquidSimpleRpcStreamHandler

typedef enum {
	NQ_OK = 0,
	NQ_ESYSCALL = -1,
	NQ_ETIMEOUT = -2,
	NQ_EALLOC = -3,
	NQ_NOT_SUPPORT = -4,
} nq_error_t;

typedef struct {
	const char *host, *cert, *key, *ca;
	int port;
} nq_addr_t;

typedef void (*nq_logger_t)(const char *, size_t, bool);

//closure
typedef bool (*nq_on_conn_open_t)(void *, nq_conn_t);

typedef nq_time_t (*nq_on_conn_close_t)(void *, nq_conn_t, nq_result_t, const char*, bool);

typedef bool (*nq_on_stream_open_t)(void *, nq_stream_t);

typedef nq_time_t (*nq_on_stream_close_t)(void *, nq_stream_t);

typedef void *(*nq_stream_reader_t)(void *, const char *, nq_size_t, int *);

typedef nq_size_t (*nq_stream_writer_t)(void *, const void *, nq_size_t, nq_stream_t);

typedef void (*nq_on_stream_notify_t)(void *, nq_stream_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_notify_t)(void *, nq_rpc_t, uint16_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_result_t)(void *, nq_rpc_t, const void *, nq_size_t);

typedef nq_stream_t (*nq_create_stream_t)(void *, nq_conn_t);

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
		nq_on_rpc_notify_t on_rpc_notify;
		nq_on_rpc_result_t on_rpc_result;

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
} nq_svconf_t;

//handlers
typedef nq_closure_t nq_stream_factory_t;

typedef struct {
	nq_closure_t on_stream_record, on_stream_open, on_stream_close;
	nq_closure_t stream_reader, stream_writer;
} nq_stream_handler_t;

typedef struct {
	nq_closure_t on_rpc_notify, on_stream_open, on_stream_close;
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
extern nq_conn_t nq_client_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf);
// get handler map of the client. 
extern nq_hdmap_t nq_client_hdmap(nq_client_t cl);



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
extern void nq_conn_close(nq_conn_t conn); 
//this just restart connection, never destroy. but associated stream/rpc all destroyed. (client only)
extern int nq_conn_reset(nq_conn_t conn); 
//check connection is client mode or not.
extern bool nq_conn_is_client(nq_conn_t conn);



// --------------------------
//
// stream API 
//
// --------------------------
//create single stream from conn, which has type specified by "name".
extern nq_stream_t nq_conn_stream(nq_conn_t conn, const char *name);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
extern void nq_stream_close(nq_stream_t s);
//send arbiter byte array/arbiter object to stream peer. 
extern void nq_stream_send(nq_stream_t s, const void *data, nq_size_t datalen);



// --------------------------
//
// rpc API
//
// --------------------------
//create single rpc stream from conn, which has type specified by "name".
extern nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
extern void nq_rpc_close(nq_rpc_t rpc);
//send arbiter byte array or object to stream peer. 
extern void nq_rpc_call(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen, nq_closure_t on_result);
//rsend arbiter byte array or object to stream peer, without receving reply
extern void nq_rpc_notify(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen);



#if defined(__cplusplus)
}
#endif
