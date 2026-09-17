// Fake JNIEnv/JavaVM. libgame.so's Blast layer calls into Java for EGL, AssetManager,
// StorageDirectory, PlayerAndroid, EAActivityArguments, Trust5Facade. Every GetMethodID/
// GetStaticMethodID resolves by NAME against the tables below; unknown names return a
// numbered id and log on first call so you can add them incrementally.
#include <vitasdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "jni_patch.h"
#include "stubs.h"

enum { CLASS_GENERIC = 1, CLASS_ACTIVITY, CLASS_EGL, CLASS_ASSETMGR, CLASS_STORAGE, CLASS_VIDEO, CLASS_TRUST5, CLASS_ARGS };

// ---- return-value helpers -------------------------------------------------
static int  ret0(void)          { return 0; }
static int  ret1(void)          { return 1; }
static void retv(void)          {}
static const char *storage_ext(void){ return DATA_PATH; }          // external storage root
static const char *storage_int(void){ return DATA_PATH "/internal"; }
static const char *storage_obb(void){ return DATA_PATH "/obb"; }
static const char *device_name(void){ return "PS Vita"; }
static const char *lang(void)       { return "en"; }
static int screen_w(void)           { return 960; }
static int screen_h(void)           { return 544; }
static int api_level(void)          { return 14; }
static float density(void)          { return 1.0f; }
static int egl_ok(void)             { return 1; }   // all EGL10.* calls report success; VitaGL owns the context

// ---- Android AssetManager emulation on top of DATA_PATH/obb ----------------------------
#define TAG_ASSET_MGR 0x41534D47
#define TAG_STREAM    0x41535452
typedef struct { int tag; } AssetMgr;
typedef struct { int tag; SceUID fd; long size; long pos; } AssetStream;
static AssetMgr fake_asset_mgr = { TAG_ASSET_MGR };
void *fake_asset_manager = &fake_asset_mgr;

static void asset_path(char *out, size_t n, const char *name) {
  if (!strncmp(name, "appbundle:/", 11)) name += 11;
  while (*name == '/') name++;
  snprintf(out, n, DATA_PATH "/obb/%s", name);
}
static AssetStream *asset_open(const char *name) {
  char p[512]; asset_path(p, sizeof p, name);
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) { debugPrintf("asset open FAIL %s\n", p); return NULL; }
  AssetStream *s = calloc(1, sizeof *s); s->tag = TAG_STREAM; s->fd = fd;
  s->size = sceIoLseek(fd, 0, SCE_SEEK_END); sceIoLseek(fd, 0, SCE_SEEK_SET);
  return s;
}
static int asset_read(AssetStream *s, int *jarr, int off, int len) {
  if (!s || s->tag != TAG_STREAM) return -1;
  if (s->pos >= s->size) return -1;
  int n = sceIoRead(s->fd, (char *)(jarr + 1) + off, len);
  if (n > 0) s->pos += n;
  return n > 0 ? n : -1;
}
static long long asset_skip(AssetStream *s, long long n) {
  if (!s || s->tag != TAG_STREAM) return 0;
  long np = s->pos + (long)n; if (np > s->size) np = s->size;
  sceIoLseek(s->fd, np, SCE_SEEK_SET); long long d = np - s->pos; s->pos = np; return d;
}
static void asset_close(AssetStream *s) {
  if (!s || s->tag != TAG_STREAM) return;
  sceIoClose(s->fd); s->tag = 0; free(s);
}
static int *asset_list(const char *name) {
  char p[512]; asset_path(p, sizeof p, name);
  SceUID d = sceIoDopen(p);
  char *names[512]; int n = 0;
  if (d >= 0) { SceIoDirent e; while (n < 512 && sceIoDread(d, &e) > 0) names[n++] = strdup(e.d_name); sceIoDclose(d); }
  int *arr = calloc(n + 1, sizeof(int)); arr[0] = n; for (int i = 0; i < n; i++) arr[1 + i] = (int)names[i];
  return arr;
}
enum { M_OPEN = 900, M_OPENFD, M_LIST, M_READ, M_SKIP, M_CLOSE, M_GETLENGTH, M_GETASSETS };
static const struct { const char *n; int id; } special[] = {
  { "open", M_OPEN }, { "openFd", M_OPENFD }, { "list", M_LIST }, { "read", M_READ },
  { "skip", M_SKIP }, { "close", M_CLOSE }, { "getLength", M_GETLENGTH }, { "getAssets", M_GETASSETS },
};

// ---- method tables (name → C impl). Names come from strings in libgame.so; extend as the
// log reveals "JNI: unknown method <name>". Return type must match the Call<Type>Method used.
static jni_method methods[] = {
  // com/ea/blast/EglAndroidDelegate + javax/microedition/khronos/egl/EGL10
  { "eglGetDisplay",        (uintptr_t)ret1 }, { "eglInitialize",     (uintptr_t)egl_ok },
  { "eglChooseConfig",      (uintptr_t)egl_ok }, { "eglGetConfigAttrib",(uintptr_t)egl_ok },
  { "eglCreateContext",     (uintptr_t)ret1 }, { "eglCreateWindowSurface",(uintptr_t)ret1 },
  { "eglCreatePbufferSurface",(uintptr_t)ret1 },{ "eglMakeCurrent",   (uintptr_t)egl_ok },
  { "eglSwapBuffers",       (uintptr_t)egl_ok }, { "eglDestroySurface",(uintptr_t)egl_ok },
  { "eglDestroyContext",    (uintptr_t)egl_ok }, { "eglTerminate",     (uintptr_t)egl_ok },
  { "eglQuerySurface",      (uintptr_t)egl_ok }, { "eglGetError",      (uintptr_t)ret0 },
  { "eglQueryString",       (uintptr_t)lang },
  // com/ea/EAMIO/StorageDirectory
  { "GetExternalStorageDirectory",(uintptr_t)storage_ext },{ "GetInternalStorageDirectory",(uintptr_t)storage_int },
  { "GetObbDirectory",      (uintptr_t)storage_obb },{ "GetCacheDirectory",(uintptr_t)storage_int },
  { "IsExternalStorageAvailable",(uintptr_t)ret1 },
  // com/ea/blast/MainActivity / DisplayAndroidDelegate
  { "GetDisplayWidth",      (uintptr_t)screen_w },{ "GetDisplayHeight",(uintptr_t)screen_h },
  { "GetDisplayDensity",    (uintptr_t)density },{ "GetApiLevel",      (uintptr_t)api_level },
  { "GetDeviceName",        (uintptr_t)device_name },{ "GetLanguage",   (uintptr_t)lang },
  { "GetOrientation",       (uintptr_t)ret0 },{ "SetOrientation",   (uintptr_t)retv },
  { "GetBatteryLevel",      (uintptr_t)ret1 },{ "IsPowerConnected",  (uintptr_t)ret1 },
  { "ShowKeyboard",         (uintptr_t)retv },{ "HideKeyboard",      (uintptr_t)retv },
  // com/ea/ssx/trust5/Trust5Facade — IAP/DRM: always offline, always owned
  { "Initialize",           (uintptr_t)retv },{ "IsInitialized",     (uintptr_t)ret1 },
  { "RequestTierInfo",      (uintptr_t)retv },{ "RequestPurchase",   (uintptr_t)retv },
  { "RestorePurchaseItems", (uintptr_t)retv },{ "AuthenticatePurchase",(uintptr_t)retv },
  // com/ea/VideoPlayer/PlayerAndroid — stubbed: report immediate completion
  { "Play",                 (uintptr_t)retv },{ "Stop",              (uintptr_t)retv },
  { "IsPlaying",            (uintptr_t)ret0 },
  // com/ea/EAActivityArguments
  { "GetArgumentCount",     (uintptr_t)ret0 },
};
#define NMETHODS (sizeof(methods)/sizeof(methods[0]))
#define UNKNOWN_BASE 1000
static char *unknown_names[512]; static int unknown_count;

static int FindClass(void *env, const char *name) {
  if (strstr(name, "Egl") || strstr(name, "khronos")) return CLASS_EGL;
  if (strstr(name, "StorageDirectory")) return CLASS_STORAGE;
  if (strstr(name, "PlayerAndroid")) return CLASS_VIDEO;
  if (strstr(name, "Trust5")) return CLASS_TRUST5;
  if (strstr(name, "AssetManager") || strstr(name, "AssetFileDescriptor")) return CLASS_ASSETMGR;
  if (strstr(name, "EAActivityArguments")) return CLASS_ARGS;
  return CLASS_GENERIC;
}
static int GetMethodID(void *env, int clazz, const char *name, const char *sig) {
  for (unsigned i = 0; i < sizeof(special)/sizeof(special[0]); i++) if (!strcmp(special[i].n, name)) return special[i].id;
  for (unsigned i = 0; i < NMETHODS; i++) if (!strcmp(methods[i].name, name)) return i + 1;
  for (int i = 0; i < unknown_count; i++) if (!strcmp(unknown_names[i], name)) return UNKNOWN_BASE + i;
  debugPrintf("JNI: unknown method %s %s (class %d)\n", name, sig, clazz);
  if (unknown_count < 512) unknown_names[unknown_count] = strdup(name);
  return UNKNOWN_BASE + unknown_count++;
}
static uintptr_t lookup(int id) { return (id >= 1 && id <= (int)NMETHODS) ? methods[id-1].func : 0; }

// ---- Call*Method: args are passed via va_list; we forward up to 6 ints (covers every sig seen) ----
#define FWD(f, obj, a) ((RET(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))f)(obj,a[0],a[1],a[2],a[3],a[4],a[5])
#define CALLV(NAME, RET, CAST) \
  static RET NAME##V(void *env, uintptr_t obj, int mid, va_list args) { \
    if (mid >= M_OPEN && mid <= M_GETASSETS) return (RET)special_call(mid, obj, args); \
    uintptr_t f = lookup(mid); \
    if (!f) { if (mid < UNKNOWN_BASE || mid - UNKNOWN_BASE >= unknown_count) return (RET)0; \
              static int warned[512]; if (!warned[mid-UNKNOWN_BASE]++) debugPrintf("JNI: call to unimplemented %s\n", unknown_names[mid-UNKNOWN_BASE]); return (RET)0; } \
    uintptr_t a[6]; for (int i = 0; i < 6; i++) a[i] = va_arg(args, uintptr_t); \
    return FWD(f, obj, a); } \
  static RET NAME(void *env, uintptr_t obj, int mid, ...) { va_list ap; va_start(ap, mid); RET r = NAME##V(env, obj, mid, ap); va_end(ap); return r; } \
  static RET NAME##A(void *env, uintptr_t obj, int mid, uintptr_t *args) { \
    if (mid >= M_OPEN && mid <= M_GETASSETS) return (RET)special_callA(mid, obj, args); \
    uintptr_t f = lookup(mid); if (!f) return (RET)0; return FWD(f, obj, args); }

// special (asset) dispatch: reads the correctly-typed args
static long long special_call(int mid, uintptr_t obj, va_list args) {
  switch (mid) {
    case M_GETASSETS: return (long long)(uintptr_t)fake_asset_manager;
    case M_OPEN: case M_OPENFD: { const char *n = va_arg(args, const char *); return (long long)(uintptr_t)asset_open(n); }
    case M_LIST:  { const char *n = va_arg(args, const char *); return (long long)(uintptr_t)asset_list(n); }
    case M_READ:  { int *arr = va_arg(args, int *); int off = va_arg(args, int); int len = va_arg(args, int); return asset_read((AssetStream *)obj, arr, off, len); }
    case M_SKIP:  { long long n = va_arg(args, long long); return asset_skip((AssetStream *)obj, n); }
    case M_CLOSE: asset_close((AssetStream *)obj); return 0;
    case M_GETLENGTH: { AssetStream *s = (AssetStream *)obj; return (s && s->tag == TAG_STREAM) ? s->size : 0; }
  }
  return 0;
}
static long long special_callA(int mid, uintptr_t obj, uintptr_t *a) {
  switch (mid) {
    case M_GETASSETS: return (long long)(uintptr_t)fake_asset_manager;
    case M_OPEN: case M_OPENFD: return (long long)(uintptr_t)asset_open((const char *)a[0]);
    case M_LIST:  return (long long)(uintptr_t)asset_list((const char *)a[0]);
    case M_READ:  return asset_read((AssetStream *)obj, (int *)a[0], (int)a[1], (int)a[2]);
    case M_SKIP:  { long long n; memcpy(&n, a, 8); return asset_skip((AssetStream *)obj, n); }
    case M_CLOSE: asset_close((AssetStream *)obj); return 0;
    case M_GETLENGTH: { AssetStream *s = (AssetStream *)obj; return (s && s->tag == TAG_STREAM) ? s->size : 0; }
  }
  return 0;
}

CALLV(CallObjectMethod,  uintptr_t, uintptr_t)
CALLV(CallBooleanMethod, int, int)
CALLV(CallIntMethod,     int, int)
CALLV(CallLongMethod,    int64_t, int64_t)
static float CallFloatMethodV(void *env,uintptr_t obj,int mid,va_list args){ uintptr_t f=lookup(mid); return f?((float(*)(uintptr_t))f)(obj):0.0f; }
static float CallFloatMethod(void *env,uintptr_t obj,int mid,...){ va_list ap;va_start(ap,mid);float r=CallFloatMethodV(env,obj,mid,ap);va_end(ap);return r; }
static void  CallVoidMethodV(void *env,uintptr_t obj,int mid,va_list args){ if(mid>=M_OPEN&&mid<=M_GETASSETS){special_call(mid,obj,args);return;} uintptr_t f=lookup(mid); if(f){uintptr_t a[6];for(int i=0;i<6;i++)a[i]=va_arg(args,uintptr_t);((void(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))f)(obj,a[0],a[1],a[2],a[3],a[4],a[5]);} else if(mid>=UNKNOWN_BASE&&mid-UNKNOWN_BASE<unknown_count){static int w[512]; if(!w[mid-UNKNOWN_BASE]++) debugPrintf("JNI: call to unimplemented %s\n", unknown_names[mid-UNKNOWN_BASE]);} }
static void  CallVoidMethod(void *env,uintptr_t obj,int mid,...){ va_list ap;va_start(ap,mid);CallVoidMethodV(env,obj,mid,ap);va_end(ap); }
static void  CallVoidMethodA(void *env,uintptr_t obj,int mid,uintptr_t*a){ if(mid>=M_OPEN&&mid<=M_GETASSETS){special_callA(mid,obj,a);return;} uintptr_t f=lookup(mid); if(f)((void(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))f)(obj,a[0],a[1],a[2],a[3],a[4],a[5]); }

// ---- strings / arrays / refs -------------------------------------------------
static char *NewStringUTF(void *env, const char *s) { return s ? strdup(s) : NULL; }
char *jni_new_string(const char *s) { return strdup(s); }
static const char *GetStringUTFChars(void *env, char *s, int *isCopy) { if (isCopy) *isCopy = 0; return s; }
static void ReleaseStringUTFChars(void *env, char *s, const char *c) {}
static int GetStringUTFLength(void *env, char *s) { return s ? (int)strlen(s) : 0; }
static int GetStringLength(void *env, char *s) { return s ? (int)strlen(s) : 0; }
static void DeleteLocalRef(void *env, void *o) {}
static void *NewGlobalRef(void *env, void *o) { return o; }
static void DeleteGlobalRef(void *env, void *o) {}
static int  GetArrayLength(void *env, void *a) { return a ? *(int *)a : 0; }   // arrays: [len][data...]
static void *GetPrimitiveArrayCritical(void *env, int *a, int *c) { return a + 1; }
static void ReleasePrimitiveArrayCritical(void *env, void *a, void *c, int m) {}
static int *NewIntArray(void *env, int n) { int *a = calloc(n + 1, 4); a[0] = n; return a; }
static int8_t *NewByteArray(void *env, int n) { int *a = calloc(1, n + 4); a[0] = n; return (int8_t *)a; }
static void *GetIntArrayElements(void *env, int *a, int *c) { return a + 1; }
static void ReleaseIntArrayElements(void *env, int *a, void *e, int m) {}
static void *GetByteArrayElements(void *env, int *a, int *c) { return a + 1; }
static void ReleaseByteArrayElements(void *env, int *a, void *e, int m) {}
static void SetByteArrayRegion(void *env, int *a, int s, int l, const void *b) { memcpy((char *)(a + 1) + s, b, l); }
static void GetByteArrayRegion(void *env, int *a, int s, int l, void *b) { memcpy(b, (char *)(a + 1) + s, l); }
static int  PushLocalFrame(void *env, int n) { return 0; }
static void *PopLocalFrame(void *env, void *r) { return r; }
static int  ExceptionCheck(void *env) { return 0; }
static void ExceptionClear(void *env) {}
static void *ExceptionOccurred(void *env) { return NULL; }
static int  GetObjectClass(void *env, int obj) { return CLASS_GENERIC; }
static int  GetStaticFieldID(void *env, int c, const char *n, const char *s) { return 1; }
static int  GetFieldID(void *env, int c, const char *n, const char *s) { return 1; }
static int  GetStaticIntField(void *env, int c, int f) { return 0; }
static int  GetIntField(void *env, int o, int f) { return 0; }
static void *GetStaticObjectField(void *env, int c, int f) { return NULL; }
static void *GetObjectField(void *env, int o, int f) { return NULL; }
static int  RegisterNatives(void *env, int c, void *m, int n) { return 0; }
static int  GetJavaVM(void *env, void **vm) { *vm = fake_vm; return 0; }
static int  GetVersion(void *env) { return 0x00010006; }
static int  NewObject(void *env, int c, int m, ...) { return 1; }
static void *NewObjectArray(void *env, int n, int c, void *i) { return NewIntArray(env, n); }
static void SetObjectArrayElement(void *env, int *a, int i, void *v) { a[1 + i] = (int)v; }
static void *GetObjectArrayElement(void *env, int *a, int i) { return (void *)a[1 + i]; }
static int  IsSameObject(void *env, void *a, void *b) { return a == b; }

// ---- JNINativeInterface: 229-slot function table. Only slots we implement are set; the rest
// point at a logging trap so a crash here is diagnosable instead of a silent jump to 0.
static void jni_trap(void) { debugPrintf("JNI: unimplemented JNIEnv slot called\n"); abort(); }
static void *env_table[229];
static void **env_ptr = env_table;
void *fake_env = &env_ptr;

static int   JavaVM_GetEnv(void *vm, void **env, int ver) { *env = fake_env; return 0; }
static int   JavaVM_AttachCurrentThread(void *vm, void **env, void *args) { *env = fake_env; return 0; }
static int   JavaVM_DetachCurrentThread(void *vm) { return 0; }
static void *vm_table[8];
static void **vm_ptr = vm_table;
void *fake_vm = &vm_ptr;

#define SLOT(n, f) env_table[n] = (void *)f
void jni_init(void) {
  for (int i = 0; i < 229; i++) env_table[i] = (void *)jni_trap;
  SLOT(4, GetVersion);      SLOT(6, FindClass);          SLOT(15, ExceptionOccurred);
  SLOT(17, ExceptionClear); SLOT(19, PushLocalFrame);    SLOT(20, PopLocalFrame);
  SLOT(21, NewGlobalRef);   SLOT(22, DeleteGlobalRef);   SLOT(23, DeleteLocalRef);
  SLOT(24, IsSameObject);   SLOT(28, NewObject);         SLOT(31, GetObjectClass);
  SLOT(33, GetMethodID);
  SLOT(34, CallObjectMethod); SLOT(35, CallObjectMethodV); SLOT(36, CallObjectMethodA);
  SLOT(37, CallBooleanMethod);SLOT(38, CallBooleanMethodV);SLOT(39, CallBooleanMethodA);
  SLOT(49, CallIntMethod);    SLOT(50, CallIntMethodV);    SLOT(51, CallIntMethodA);
  SLOT(52, CallLongMethod);   SLOT(53, CallLongMethodV);   SLOT(54, CallLongMethodA);
  SLOT(55, CallFloatMethod);  SLOT(56, CallFloatMethodV);
  SLOT(61, CallVoidMethod);   SLOT(62, CallVoidMethodV);   SLOT(63, CallVoidMethodA);
  SLOT(94, GetFieldID);       SLOT(95, GetObjectField);    SLOT(100, GetIntField);
  SLOT(113, GetMethodID);     /* GetStaticMethodID → same resolver */
  SLOT(114, CallObjectMethod);SLOT(115, CallObjectMethodV);SLOT(116, CallObjectMethodA);
  SLOT(117, CallBooleanMethod);SLOT(118, CallBooleanMethodV);SLOT(119, CallBooleanMethodA);
  SLOT(129, CallIntMethod);   SLOT(130, CallIntMethodV);   SLOT(131, CallIntMethodA);
  SLOT(132, CallLongMethod);  SLOT(133, CallLongMethodV);  SLOT(134, CallLongMethodA);
  SLOT(135, CallFloatMethod); SLOT(136, CallFloatMethodV);
  SLOT(141, CallVoidMethod);  SLOT(142, CallVoidMethodV);  SLOT(143, CallVoidMethodA);
  SLOT(144, GetStaticFieldID);SLOT(145, GetStaticObjectField); SLOT(150, GetStaticIntField);
  SLOT(164, GetStringLength); SLOT(167, NewStringUTF);     SLOT(168, GetStringUTFLength);
  SLOT(169, GetStringUTFChars);SLOT(170, ReleaseStringUTFChars);
  SLOT(171, GetArrayLength);  SLOT(172, NewObjectArray);   SLOT(173, GetObjectArrayElement);
  SLOT(174, SetObjectArrayElement); SLOT(176, NewByteArray); SLOT(179, NewIntArray);
  SLOT(184, GetByteArrayElements); SLOT(187, GetIntArrayElements);
  SLOT(192, ReleaseByteArrayElements); SLOT(195, ReleaseIntArrayElements);
  SLOT(200, GetByteArrayRegion); SLOT(208, SetByteArrayRegion);
  SLOT(215, RegisterNatives); SLOT(219, GetJavaVM);
  SLOT(222, GetPrimitiveArrayCritical); SLOT(223, ReleasePrimitiveArrayCritical);
  SLOT(228, ExceptionCheck);
  vm_table[4] = (void *)JavaVM_AttachCurrentThread; vm_table[5] = (void *)JavaVM_DetachCurrentThread;
  vm_table[6] = (void *)JavaVM_GetEnv; vm_table[7] = (void *)JavaVM_AttachCurrentThread;
}
