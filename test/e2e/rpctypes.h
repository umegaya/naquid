#pragma once

namespace nqtest {
  enum RpcType {
    Ping = 0,
    Raise = 1,
    NotifyText = 2,
    ServerStream = 3,

    ServerRequest = 10000,

    TextNotification = 50000, //to test 2 bytes type code
  };
}