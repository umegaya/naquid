#pragma once

#include "common.h"

extern void test_reconnect_client(nqtest::Test::Conn &conn);
extern void test_reconnect_server(nqtest::Test::Conn &conn);
extern void test_reconnect_stress(nqtest::Test::Conn &conn);