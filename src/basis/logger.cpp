#include "logger.h"
#include <mutex>
#include <MoodyCamel/concurrentqueue.h>


namespace nq {
namespace logger {
  const std::string log_level_[level::max] = {
      "T",
      "D",
      "I",
      "W",
      "E",
      "F",
      "R",
  };
  static void default_writer(const char *buf, size_t len, bool) {
    fwrite(buf, 1, len, stderr);
  }
  static writer_cb_t writer_ = default_writer;
  static std::string id_ = "nq";
  static std::mutex mtx_;
  static bool manual_flush_ = false;
    void configure(writer_cb_t cb, const std::string &id, bool manual_flush) {
      if (cb != nullptr) {
      writer_ = cb;
    }
    if (id.length() > 0) {
      id_ = id;
    }
    manual_flush_ = manual_flush;
  }
  const std::string &id() { return id_; }

#if !defined(NO_LOG_WRITE_CALLBACK)
  static moodycamel::ConcurrentQueue<std::string> s_logs;
  void flush() {
//  printf("flush_from_main_thread");
    if (manual_flush_) {
      std::string str;
      while (s_logs.try_dequeue(str)) {
        writer_(str.c_str(), str.length(), true);
      }
    }
  }
#else
  void flush() {
  }
#endif

  void write(const std::string &body, const std::string &footer) {
    mtx_.lock();
#if defined(NO_LOG_WRITE_CALLBACK)
    fwrite(body.c_str(), 1, body.length(), stdout);
    fwrite(footer.c_str(), 1, footer.length(), stdout);
#else
    if (manual_flush_) {
      s_logs.enqueue(body + footer);
    } else {
      writer_(body.c_str(), body.length(), false);
      writer_(footer.c_str(), footer.length(), true);
    }
#endif
    mtx_.unlock();
  }


  void write(const std::string &body) {
    mtx_.lock();
#if defined(NO_LOG_WRITE_CALLBACK)
    fwrite(body.c_str(), 1, body.length(), stdout);
#else
    if (manual_flush_) {
      s_logs.enqueue(body);
    } else {
      writer_(body.c_str(), body.length(), true);     
    }
#endif
    mtx_.unlock();
  }
}
}
