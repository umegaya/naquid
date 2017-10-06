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

typedef struct nq_client_tag *nq_client_t; //NaquidClient

typedef struct nq_server_tag *nq_server_t; //NaquidServer

typedef struct nq_conn_tag *nq_conn_t; //NaquidSession

typedef struct nq_hdmap_tag *nq_hdmap_t; //nq::HandlerMap

typedef struct nq_stream_tag *nq_stream_t; //NaquidStreamHandler

typedef struct nq_rpc_tag *nq_rpc_t; //NaquidSimpleRpcStreamHandler

typedef enum {
	NQ_OK = 0,
	NQ_ESYSCALL = -1,
	NQ_ETIMEOUT = -2,
} nq_error_t;

typedef struct {
	const char *url, *cert, *key, *ca;
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

typedef void (*nq_on_stream_record_t)(void *, nq_stream_t, const void *, nq_size_t);

typedef void (*nq_on_rpc_record_t)(void *, nq_rpc_t, uint16_t, const void *, nq_size_t);

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
		nq_on_rpc_record_t on_rpc_record;

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
extern nq_client_t nq_client_create(int max_nfd);

extern void nq_client_poll(nq_client_t cl); //need to call periodically

extern void nq_client_destroy(nq_client_t cl);



// --------------------------
//
// server API
//
// --------------------------
extern nq_server_t nq_server_listen(const nq_addr_t *addr, const nq_svconf_t *conf);

extern void nq_server_join(nq_server_t sv);



// --------------------------
//
// hdmap API
//
// --------------------------
extern nq_hdmap_t nq_client_hdmap(nq_client_t cl);

extern nq_hdmap_t nq_server_hdmap(nq_server_t sv);

extern nq_hdmap_t nq_conn_hdmap(nq_conn_t conn);
//setup original stream protocol (client), with 3 pattern
extern bool nq_hdmap_stream_handler(nq_hdmap_t h, const char *name, nq_stream_handler_t handler);

extern bool nq_hdmap_rpc_handler(nq_hdmap_t h, const char *name, nq_rpc_handler_t handler);

extern bool nq_hdmap_stream_factory(nq_hdmap_t h, const char *name, nq_stream_factory_t factory);



// --------------------------
//
// conn API
//
// --------------------------
//create conn from client. server side can get from argument of on_accept handler
extern nq_conn_t nq_connect(nq_client_t cl, const nq_addr_t *addr, const nq_clconf_t *conf);
//close and destroy conn/associated stream eventually, so never touch conn/stream/rpc after calling this API.
extern void nq_conn_close(nq_conn_t conn); 
//this just restart connection, never destroy. but associated stream/rpc all destroyed. (client only)
extern void nq_conn_reset(nq_conn_t conn); 
//check connection is client mode or not.
extern bool nq_conn_is_client(nq_conn_t conn);



// --------------------------
//
// stream API (NaquidBidiStream and its inheritance/NaquidBidiPluginStream)
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
// rpc API (NaquidRPCStream and its inheritance/NaquidRPCPluginStream)
//
// --------------------------
extern nq_rpc_t nq_conn_rpc(nq_conn_t conn, const char *name);
//close this stream only (conn not closed.) useful if you use multiple stream and only 1 of them go wrong
extern void nq_rpc_close(nq_rpc_t rpc);
//send arbiter byte array/arbiter object to stream peer. 
extern void nq_rpc_call(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen, nq_closure_t on_result);
//send arbiter byte array/arbiter object to stream peer. 
extern void nq_rpc_notify(nq_rpc_t rpc, uint16_t type, const void *data, nq_size_t datalen);



#if defined(__cplusplus)
}
#endif
