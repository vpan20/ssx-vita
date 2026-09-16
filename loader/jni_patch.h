#pragma once
#include <stdint.h>
typedef struct { const char *name; uintptr_t func; } jni_method;
extern void *fake_env;   // JNIEnv*
extern void *fake_vm;    // JavaVM*
void jni_init(void);
