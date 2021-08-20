#include "core/platform/nq_platform.h"
//entry point of android jni
#if defined(OS_ANDROID)
#include "core/platform/nq_jni.h"

//global variable
JavaVM *g_vm_p = nullptr;
JNIEnv *g_env_p = nullptr;

//module level jni initialize functions
extern bool JNI_OnLoad_InitReachability();

//this called when the library loaded by JVM (eg. System.LoadLibrary)
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  if (g_vm_p != nullptr) {
    char buffer1[32], buffer2[32];
    sprintf(buffer1, "%p", g_vm_p);
    sprintf(buffer2, "%p", vm);
    logger::error({
      {"msg", "panic: JNI_Onload seems called twice"},
      {"oldjvm_p", buffer1},
      {"newjvm_p", buffer2}
    });
    return -1;
  }
  g_vm_p = vm;
  if (GetEnv(g_vm_p, &g_env_p, JNI_VERSION_1_4) != JNI_OK) {
    logger::error({
      {"msg", "panic: fail to get env"},
    });
    return -1;
  }
  if (!JNI_OnLoad_InitReachability()) {
    return -1;
  }
  return 0;
}

JavaVM *g_vm() { return g_vm_p; }
JNIEnv *g_env() { return g_env_p; }
#endif