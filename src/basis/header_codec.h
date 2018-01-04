#pragma once

#include "nq.h"
#include "endian.h"

namespace nq {
  class LengthCodec {
   public:
    static inline nq_size_t Encode(nq_size_t sz_encode, char *buf, nq_size_t bufsz) {
      nq_size_t idx = 0;
      while (bufsz > idx) {
        buf[idx] = sz_encode & 0x7f;
        sz_encode >>= 7;
        if (sz_encode > 0) {
          idx++;
        } else {
          buf[idx] |= 0x80;
          return idx + 1;
        }
      }
      return 0; //buff length not enough
    }
    constexpr static size_t EncodeLength(size_t length_type_size) {
      return length_type_size * 2;
    }
    static inline nq_size_t Decode(nq_size_t *psz_decoded, const char *buf, nq_size_t bufsz) {
      nq_size_t idx = 0;
      *psz_decoded = 0;
      while (bufsz > idx) {
        *psz_decoded += ((buf[idx] & 0x7f) << (idx * 7));
        if ((buf[idx] & 0x80) == 0) {
          idx++;
        } else {
          return idx + 1;
        }
      }
      return 0; //not enough buffer arrived
    }
  };

  class HeaderCodec {
   public:
    enum Flags {
      MSGID_2BYTE = 1 << 0,
      MSGID_4BYTE = 1 << 1,
      TYPE_1BYTE = 1 << 2,

      EXT_BIT = 1 << 7,
    };
    static inline nq_size_t Encode(int16_t type, nq_msgid_t msgid, char *buf, nq_size_t bufsz) {
      buf[0] = 0;
      nq_size_t ofs = 1;
      auto mask = (((uint16_t)type) & 0xFF00);
      if (mask != 0 && mask != 0xFF00) {
        Endian::HostToNetbytes(type, buf + 1);
        ofs += 2;
      } else {
        buf[0] |= TYPE_1BYTE;
        buf[1] = (char)type;
        ofs += 1;
      }
      if (msgid & 0xFFFF0000) {
        buf[0] |= MSGID_4BYTE;
        Endian::HostToNetbytes(msgid, buf + ofs);
        ofs += 4;
      } else if (msgid != 0) {
        buf[0] |= MSGID_2BYTE;
        Endian::HostToNetbytes((uint16_t)msgid, buf + ofs);
        ofs += 2;
      }
      return ofs;
    }
    static inline nq_size_t Decode(int16_t *type, nq_msgid_t *msgid, const char *buf, nq_size_t bufsz) {
      auto f = buf[0];
      nq_size_t ofs = 1;
      if (f & TYPE_1BYTE) {
        *type = buf[1];
        ofs += 1;
      } else {
        *type = Endian::NetbytesToHost<int16_t>(buf + 1);
        ofs += 2;
      }
      if (f & MSGID_4BYTE) {
        *msgid = Endian::NetbytesToHost<uint32_t>(buf + ofs);
        ofs += 4;
      } else if (f & MSGID_2BYTE) {
        *msgid = Endian::NetbytesToHost<uint16_t>(buf + ofs);
        ofs += 2;
      } else {
        *msgid = 0;
      }
      return ofs;      
    }
  };
}
