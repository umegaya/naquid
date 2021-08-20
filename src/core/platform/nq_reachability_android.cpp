#include "core/platform/nq_platform.h"
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

namespace nq {

class NqReachabilityAndroid : public NqReachability {
 public:
  bool Start(const std::string &hostname) override {
    //TODO(iyatomi): android only provide reachability change via ConnectivityManager.CONNECTIVITY_ACTION intent, 
    //that means we need to find the way to receive intent message without activity, or create activity from JNI code,
    //which seems to be error-prone. for now, I provide API nq_conn_reachability_change and call it from Java side,
    //and wait for someone who is ninja-level android JNI programmer lol
    logger::fatal("android does not support automatic reachability change yet. call nq_conn_reachability_change manally");
    return false;
  }
  void Stop() override {
    logger::fatal("android does not support automatic reachability change yet. call nq_conn_reachability_change manally");
  }
  NqReachabilityAndroid(nq_on_reachability_change_t cb) : NqReachability(cb) {}
 protected:
  ~NqReachabilityAndroid() override {}
};

NqReachability *NqReachability::Create(nq_on_reachability_change_t cb) {
  return new NqReachabilityAndroid(cb);
}
}
#endif