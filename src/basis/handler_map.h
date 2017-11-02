#pragma once

#include <map>

#include "nq.h"

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
  	HandlerEntry he;
    he.type = FACTORY;
    he.factory = factory;
  	map_[name] = he;
    return true;
  }
  inline bool AddEntry(const std::string &name, nq_stream_handler_t stream) {
    HandlerEntry he;
    he.type = STREAM;
    he.stream = stream;
  	map_[name] = he;
    return true;
  }
  inline bool AddEntry(const std::string &name, nq_rpc_handler_t rpc) {
    HandlerEntry he;
    he.type = RPC;
    he.rpc = rpc;
  	map_[name] = he;
    return true;
  }
  inline const HandlerEntry *Find(const std::string &name) const {
    auto it = map_.find(name);
    return it == map_.end() ? nullptr : &(it->second);
  }

  inline nq_hdmap_t ToHandle() { return (nq_hdmap_t)this; }
  static inline HandlerMap *FromHandle(nq_hdmap_t hdm) { return (HandlerMap *)hdm; }
};
}
