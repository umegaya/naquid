#pragma once

#include "common.h"

extern void test_reconnect_client(nqtest::Test::Conn &conn);
extern void test_reconnect_server_conn(nqtest::Test::Conn &conn);
extern void test_reconnect_server_stream(nqtest::Test::Conn &conn);
extern void test_reconnect_stress(nqtest::Test::Conn &conn);