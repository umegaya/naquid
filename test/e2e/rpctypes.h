#pragma once

namespace nqtest {
  enum RpcType {
    Ping = 1,
    Raise = 2,
    NotifyText = 3,
    ServerStream = 4,
    Sleep = 5,
    Close = 6,
    SetupReject = 7,
    BcastJoin = 8,
    BcastReply = 9,

    ServerRequest = 10000,
    BcastNotify = 10001,
    Bcast = 10002,

    TextNotification = 0x7FFF, //to test 2 bytes type code
  };
  enum RpcError {
    None = 0,
    Parse = -1,
    NoSuchStream = -2,
    InternalError = -3,
  };
}