#pragma once

#include <spdlog/inglude/spdlog/spdlog.h>
#include "timespec.h"

namespace nq {
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
  typedef void (*writer_cb_t)(const char *, size_t, bool);
  void configure(writer_cb_t cb, const std::string &id, bool manual_flush);
  const std::string &id();
  void write(const std::string &body, const std::string &footer);
  void write(const std::string &body);
  void flush();

  //log variadic funcs
  static constexpr FOOTER_FORMAT=R"(,"id_":"{}","ts_":{}.{:=9},"lv_":"{}"\n)";
  template <typename... Args> inline void log(level::def lv, const char *fmt, fmt::ArgList args) {
    if (lv < level::debug) {
      std::string body = fmt::format(fmt, args);
      write(body + "\n");
      return;
    }
    long sec, nsec;
    clock::now(sec, nsec);
    std::string footer = fmt::format(FOOTER_FORMAT, id(),sec,nsec,log_level_[lv]);
    std::string body = fmt::format(fmt, args);
    write(body, footer);
  }
  inline void log(level::def lv, const char *body) {
    if (lv < level::debug) {
      write(body + "\n");
      return;
    }
    long sec, nsec;
    clock::now(sec, nsec);
    std::string footer = fmt::format(FOOTER_FORMAT, id(),sec,nsec,log_level_[lv]);
    write(body, footer);        
  }
  FMT_VARIADIC(void, log, level::def, const char *);  

  //format variadic funcs
  inline std::string format(const char *fmt, fmt::ArgList args) { return fmt::format(fmt, args); }
  FMT_VARIADIC(std::string, Format, const char *);
  inline const char *format(const char *fmt) { return fmt; }

  //short hands for each severity
  template <typename... Args> inline void trace(const char *fmt, const Args&... args) { log(level::trace, fmt, args...); }
  template <typename... Args> inline void debug(const char *fmt, const Args&... args) { log(level::debug, fmt, args...); }
  template <typename... Args> inline void info(const char *fmt, const Args&... args) { log(level::info, fmt, args...); }
  template <typename... Args> inline void warn(const char *fmt, const Args&... args) { log(level::warn, fmt, args...); }
  template <typename... Args> inline void error(const char *fmt, const Args&... args) { log(level::error, fmt, args...); }
  template <typename... Args> inline void fatal(const char *fmt, const Args&... args) { log(level::fatal, fmt, args...); }
  template <typename... Args> inline void report(const char *fmt, const Args&... args) { log(level::report, fmt, args...); }
  inline void trace(const char *fmt) { log(level::trace, fmt); }
  inline void debug(const char *fmt) { log(level::debug, fmt); }
  inline void info(const char *fmt) { log(level::info, fmt); }
  inline void warn(const char *fmt) { log(level::warn, fmt); }
  inline void error(const char *fmt) { log(level::error, fmt); }
  inline void fatal(const char *fmt) { log(level::fatal, fmt); }
  inline void report(const char *fmt) { log(level::report, fmt); }
}
}

#define LOG(level__, ...) { ::nq::logger::level__(__VA_ARGS__); } 
#if defined(VERBOSE) || !defined(NDEBUG)
#define VLOG(level__, ...) { ::nq::logger::level__(__VA_ARGS__); } 
#else
#define VLOG(level__, ...)
#endif
#define TRACE(...) VLOG(trace, __VA_ARGS__)
