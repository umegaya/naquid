#pragma once

#include "assert.h"
#include <nlohmann/json.hpp>
#include "timespec.h"

namespace nq {
using json = nlohmann::json;
namespace logger {
  class level {
   public:
    enum def {
      trace,
      debug,
      info,
      warn,
      error,
      fatal,            
      report,
      max,
    };
  };

  //non inline methods
  extern const std::string log_level_[level::max];
  typedef void (*writer_cb_t)(const char *, size_t);
  void configure(writer_cb_t cb, const std::string &id, bool manual_flush);
  const std::string &id();
  void write(const json &j);
  void flush();

  // common log properties
  inline void fill_props(level::def lv, json &j) {
    //fill default properties
    long sec, nsec;
    clock::now(sec, nsec);
    char tsbuff[32];
    sprintf(tsbuff, "%ld.%09ld", sec, nsec);
    j["ts"] = tsbuff; //((double)sec) + (((double)nsec) / (1000 * 1000 * 1000));
    j["id"] = id();
    j["lv"] = log_level_[lv];
  }

  inline void fill_props(
    level::def lv,
    const std::string &file, int line, const std::string &func,
    json &j
  ) {
    //fill default properties
    fill_props(lv, j);
    j["file"] = file;
    j["line"] = line;
    j["func"] = func;
  }

  //log variadic funcs
  inline void log(level::def lv, const json &j) {
    ASSERT(j.is_object() || j.is_string());
    if (lv >= level::debug) {
      json &mj = const_cast<json &>(j);
      if (j.is_string()) {
        mj = {
          {"msg", j},
        };
      }
      //fill default properties
      fill_props(lv, mj);
      write(mj);
    } else {
      write(j);
    }
  }

  inline void log(
    level::def lv, const std::string &file, int line, const std::string &func, const json &j
  ) {
    ASSERT(j.is_object() || j.is_string());
    if (lv >= level::debug) {
      json &mj = const_cast<json &>(j);
      if (j.is_string()) {
        mj = {
          {"msg", j},
        };
      }
      //fill default properties
      fill_props(lv, file, line, func, mj);
      write(mj);
    } else {
      write(j);
    }
  }

  template<class... Args>
  inline void trace(
    level::def lv, const std::string &file, int line, const std::string &func, 
    const std::string &fmt, const Args... args
  ) {
      char buffer[1024];
      sprintf(buffer, fmt.c_str(), args...);
      log(lv, file, line, func, buffer);
  }

  inline void trace(
    level::def lv, const std::string &file, int line, const std::string &func, 
    const json &j
  ) {
      log(lv, file, line, func, j);
  }

  //short hands for each severity
  inline void debug(const json &j) { log(level::debug, j); }
  inline void info(const json &j) { log(level::info, j); }
  inline void warn(const json &j) { log(level::warn, j); }
  inline void error(const json &j) { log(level::error, j); }
  inline void fatal(const json &j) { log(level::fatal, j); }
  inline void report(const json &j) { log(level::report, j); }
}
}

#define NQ_LOG(level__, ...) { ::nq::logger::log(::nq::logger::level::level__, __FILE__, __LINE__, __func__, __VA_ARGS__); }
#if defined(VERBOSE) || !defined(NDEBUG)
  #define NQ_VLOG(level__, ...) { ::nq::logger::trace(::nq::logger::level::level__, __FILE__, __LINE__, __func__, __VA_ARGS__); } 
#else
  #define NQ_VLOG(level__, ...)
#endif

#if !defined(TRACE)
  #if defined(DEBUG)
    #define TRACE(...) { ::nq::logger::trace(::nq::logger::level::trace, __FILE__, __LINE__, __func__, __VA_ARGS__); }
  #else
    #define TRACE(...) // fprintf(stderr, __VA_ARGS__)
  #endif
#endif
