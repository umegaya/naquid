#pragma once

#include <map>

#include "naquid.h"

namespace nq {
class HandlerMap {
 public:
  typedef enum {
  	STREAM = 0,
  	RPC = 1,
  	FACTORY = 2,
  } HandlerFactoryType;
  typedef struct {
  	HandlerFactoryType type;
  	union {
  	  nq_stream_factory_t factory;
      nq_stream_handler_t stream;
      nq_rpc_handler_t rpc;
  	};
  } HandlerEntry;
 private:
  std::map<std::string, HandlerEntry> map_;
 public:
  HandlerMap() : map_() {}
  inline bool AddEntry(const std::string &name, nq_stream_factory_t factory) {
    if (map_.find(name) != map_.end()) {
      return false;
    }
  	HandlerEntry he = { .type = FACTORY, .factory = factory };
  	map_[name] = he;
    return true;
  }
  inline bool AddEntry(const std::string &name, nq_stream_handler_t stream) {
    if (map_.find(name) != map_.end()) {
      return false;
    }
  	HandlerEntry he = { .type = STREAM, .stream = stream };
  	map_[name] = he;
    return true;
  }
  inline bool AddEntry(const std::string &name, nq_rpc_handler_t rpc) {
    if (map_.find(name) != map_.end()) {
      return false;
    }
  	HandlerEntry he = { .type = RPC, .rpc = rpc };
  	map_[name] = he;
    return true;
  }
  inline HandlerEntry *Find(const std::string &name) {
    auto it = map_.find(name);
    return it == map_.end() ? nullptr : &(it->second);
  }
};
}
