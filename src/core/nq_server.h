// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <map>
#include <tuple>
#include <mutex>
#include <condition_variable>

#include "basis/handler_map.h"
#include "core/nq_worker.h"
#include "core/nq_config.h"

namespace net {
class NqServer {
 public:
	typedef NqWorker::PacketQueue PacketQueue;
  typedef NqWorker::InvokeQueue InvokeQueue;
  struct PortConfig : public NqServerConfig {
    nq_addr_t address_;
    nq::HandlerMap handler_map_;

    PortConfig(const nq_addr_t &a) : NqServerConfig(a), address_(a), handler_map_() {}
    PortConfig(const nq_addr_t &a, const nq_svconf_t &port_config) : 
      NqServerConfig(a, port_config), address_(a), handler_map_() {}

    inline const nq::HandlerMap *handler_map() const { return &handler_map_; }
  }; 
 protected:
  bool alive_;
  uint32_t n_worker_;
	std::unique_ptr<PacketQueue[]> worker_queue_;
  std::map<int, std::unique_ptr<InvokeQueue[]>> invoke_queues_list_;
	std::map<int, PortConfig> port_configs_;
  std::map<int, NqWorker*> workers_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::thread shutdown_thread_;
  nq::IdFactory<uint32_t> stream_index_factory_;

 public:
	NqServer(uint32_t n_worker) : 
    alive_(true), n_worker_(n_worker), worker_queue_(nullptr), invoke_queues_list_(), 
    stream_index_factory_(0x7FFFFFFF) {}
  ~NqServer() {}
  nq::HandlerMap *Open(const nq_addr_t *addr, const nq_svconf_t *conf) {
    if (port_configs_.find(addr->port) != port_configs_.end()) {
      return nullptr; //already port used
    } 
    auto it = invoke_queues_list_.find(addr->port);
    if (it == invoke_queues_list_.end()) {
      invoke_queues_list_[addr->port].reset(new InvokeQueue[n_worker_]);
    }
    auto pc = (conf == nullptr) ? 
      port_configs_.emplace(std::piecewise_construct, 
                            std::forward_as_tuple(addr->port), std::forward_as_tuple(*addr)) : 
      port_configs_.emplace(std::piecewise_construct, 
                            std::forward_as_tuple(addr->port), std::forward_as_tuple(*addr, *conf));
    if (!pc.second) {
      ASSERT(false);
      return nullptr;
    }
    auto &pconf = pc.first->second;
    //first is iterator of map<int, PortConfig>
    return &(pconf.handler_map_);
  }
	int Start(bool block) {
    if (!alive_) { return NQ_OK; }
		worker_queue_.reset(new PacketQueue[n_worker_]);
		if (worker_queue_ == nullptr) {
			return NQ_EALLOC;
		}
    int r = 0;
		for (uint32_t i = 0; i < n_worker_; i++) {
			if ((r = StartWorker(i)) < 0) {
				return r;
			}
		}
    if (block) {
      std::unique_lock<std::mutex> lock(mutex_);
      cond_.wait(lock);
      Stop();
    } else {
      shutdown_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock);
        Stop();
      });
    }
		return NQ_OK;
	}
  void Join() {
    cond_.notify_one();
    //here, mutex_ should be acquired inside of Start() call (at blocking or waiter thread)
    std::unique_lock<std::mutex> lock(mutex_); //wait for Stop() call finished
    if (shutdown_thread_.joinable()) {
      shutdown_thread_.join(); //ensure shutdown thread finished
    }
  }
  PacketQueue &Q4(int idx) { return worker_queue_[idx]; }
  inline bool alive() const { return alive_; }
  inline uint32_t n_worker() const { return n_worker_; }
  inline InvokeQueue *InvokeQueuesFromPort(int port) { 
    auto it = invoke_queues_list_.find(port);
    return it != invoke_queues_list_.end() ? it->second.get() : nullptr; 
  }
  inline const std::map<int, PortConfig> &port_configs() const { return port_configs_; }
  inline nq_server_t ToHandle() { return (nq_server_t)this; }
  inline nq::IdFactory<uint32_t> &stream_index_factory() { return stream_index_factory_; }
  static inline NqServer *FromHandle(nq_server_t sv) { return (NqServer *)sv; }

 protected:
  void Stop() {
    //TODO: wait for conditional variable
    alive_ = false;
    for (auto &kv : workers_) {
      kv.second->Join();
    } 
  }
  int StartWorker(int index) {
    auto l = new NqWorker(index, *this);
    workers_[index] = l;
    l->Start(worker_queue_[index]);
    return NQ_OK;
  } 
};
}