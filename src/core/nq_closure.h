#pragma once

#include "nq.h"

/* closure */
typedef union {
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

  nq_stream_factory_t stream_factory;

  nq_on_alarm_t on_alarm;

  nq_on_reachability_change_t on_reachability_change;

  nq_on_resolve_host_t on_resolve_host;
} nq_closure_t;


#define nq_dyn_closure_init(__pclsr, __type, __cb, __arg) { \
  (__pclsr).__type.arg = (void *)(__arg); \
  (__pclsr).__type.proc = (__cb); \
}

#define nq_dyn_closure_call(__pclsr, __type, ...) ((__pclsr).__type.proc((__pclsr).__type.arg, __VA_ARGS__))

#define nq_dny_closure_empty() { { nullptr, nullptr } }

#define nq_to_dyn_closure(clsr) *((nq_closure_t *)(&(clsr)))