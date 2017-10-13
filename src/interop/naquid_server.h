// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <map>
#include <mutex>
#include <condition_variable>

#include "ModdyCamel/concurrentqueue.h"

#include "core/handler_map.h"
#include "interop/naquid_worker.h"

namespace net {
class NaquidServer {
 public:
	typedef moodycamel::ConcurrentQueue<NaquidPacket> PacketQueue;
  struct PortHandler : public nq_svport_t {
    nq::HandlerMap handler_map_;
    nq_addr_t address_;

    PortHandler(const nq_addr_t &a) : nq_svport_t(), address_(a), handler_map_() {
      nq_closure_init(on_open, on_conn_open, NoopOnOpen, nullptr);
      nq_closure_init(on_close, on_conn_close, NoopOnClose, nullptr);
    }
    PortHandler(const nq_addr_t &a, const nq_svport_t *port_config) : 
      nq_svport_t(*port_config), address_(a), handler_map_() {}

    bool NoopOnOpen(void *, nq_conn_t) { return false; }
    nq_time_t NoopOnClose(void *, nq_conn_t, nq_result_t, const char*, bool) { return 0; }
  }; 
 protected:
  bool alive_;
	nq_svconf_t config_;
	PacketQueue *worker_queue_;
	std::map<int, PortHandler> port_handlers_;
  std::map<int, NaquidWorker *> workers_;
  std::mutex mutex_;
  std::condition_variable cond_;

 public:
	NaquidServer(const nq_svconf_t &conf) : alive_(true), config_(*conf), worker_queue_(nullptr) {}
  ~NaquidServer() {}
  nq::HandlerMap *Open(const nq_addr_t *addr, const nq_svconf_t *conf) {
    if (port_handlers_.find(port) != port_handlers_.end()) {
      return nullptr; //already port used
    } 
    auto it = (port_config == nullptr) ? 
      port_handlers_.emplace(*addr) : 
      port_handlers_.emplace(*addr, *port_config);
    return &(it->second.handler_map_);
  }
	int Start(bool block) {
    if (!alive_) { return NQ_OK; }
		worker_queue_ = new PacketQueue[config_.n_worker];
		if (worker_queue_ == nullptr) {
			return NQ_EALLOC;
		}
		for (int i = 0, r = 0; i < config_.n_worker; i++) {
			if ((r = StartWorker(i)) < 0) {
				return r;
			}
		}
    if (block) {
      std::unique_lock<std::mutex> lock(mutex_);
      cond_.wait(lock);
      Stop();
    } else {
      std::thread([this]() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock);
        Stop();        
      });
    }
		return NQ_OK;
	}
  void Join() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cond_.notify_one();
      //here, mutex_ should be acquired inside of Start() call (at blocking or waiter thread)
    }
    { //again wait for same mutex to ensure Stop() call is finished
      std::unique_lock<std::mutex> lock(mutex_);
    }
  }
  PacketQueue &Q4(int idx) { return worker_queue_[idx]; }
  inline bool alive() const { return alive_; }
  inline std::map<int, PortHandler> &port_handlers() { return port_handlers_; }
  inline const nq_svconf_t &config() const { return config_; }
 protected:
  void Stop() {
    //TODO: wait for conditional variable
    alive_ = false;
    for (auto &kv : workers_) {
      kv.second->Join();
    
    if (worker_queue_ != nullptr) {
      delete []worker_queue_;
      worker_queue_ = nullptr;
    }
  }
  int StartWorker(int index) {
    auto l = new NaquidWorker(index, *this);
    workers_[index] = l;
    return l->Start(worker_queue_[index]);
  } 
}
}