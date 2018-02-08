#pragma once

#include <jni.h>

extern JavaVM *g_vm();
extern jint JNI_OnLoad(JavaVM *vm, void *reserved);