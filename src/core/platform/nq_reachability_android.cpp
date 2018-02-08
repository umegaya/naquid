#if defined(OS_ANDROID)
#include "core/platform/nq_reachability.h"
#include "basis/assert.h"
#include "basis/logger.h"

#include "core/platform/nq_jni.h"

static jobject g_connectivity_manager = nullptr

extern bool JNI_OnLoad_InitReachability() {
  auto env = g_env();
  ASSERT(env != nullptr);
  auto klass = FindClass(env, "android/content/Context");
  auto fid = GetStaticFieldID(env, klass, "CONNECTIVITY_SERVICE", "Ljava/lang/String;");
  auto jstr = GetStaticObjectField(env, klass, fid);
  g_connectivity_manager = GetMethodID(env, klass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  return g_connectivity_manager != nullptr;
}

namespace net {

class NqReachabilityAndroid : public NqReachability {
 public:
  bool Start(const std::string &hostname) override {
    nq::logger::fatal("android does not support automatic reachability change yet. call nq_conn_reachability_change manally");
    return false;
  }
  void Stop() override {
    nq::logger::fatal("android does not support automatic reachability change yet. call nq_conn_reachability_change manally");
  }
  NqReachabilityAndroid(nq_closure_t cb) : NqReachability(cb), connectivity_manager_(nullptr) {}
 protected:
  ~NqReachabilityAndroid() override {}
  nq_reachability_t ToNqReachability(NetworkStatus status) {
    switch (status) {
      case NotReachable:
        return NQ_NOT_REACHABLE;
      case ReachableViaWWAN:
        return NQ_REACHABLE_WWAN;
      case ReachableViaWiFi:
        return NQ_REACHABLE_WIFI;
      default:
        ASSERT(false);
        return NQ_NOT_REACHABLE;
    }
  }
};

NqReachability *NqReachability::Create(nq_closure_t cb) {
  return new NqReachabilityAndroid(cb);
}
}
#endif