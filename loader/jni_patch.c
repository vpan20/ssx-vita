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
static const char *api_level_str(void) { return "19"; }
static const char *platform_ver(void)  { return "4.4.4"; }
static const char *proc_arch(void)     { return "armeabi-v7a"; }
static const char *locale_str(void)    { return "en_US"; }
static float density(void)          { return 1.0f; }
static int egl_ok(void)             { return 1; }
// Device identity — report as Xperia Z1 (C6903) so the game selects the config EA shipped for this exact device
static const char *dev_model(void)   { return "C6903"; }
static const char *dev_maker(void)   { return "Sony"; }
static const char *dev_chipset(void) { return "MSM8974"; }
static const char *dev_fw(void)      { return "4.4.4"; }
static const char *dev_uid(void)     { return "0123456789abcdef"; }
static const char *dev_fp(void)      { return "neon vfpv3"; }
static const char *app_ver(void)     { return "0.0.7833"; }
static int app_vercode(void)         { return 7833; }
static int audio_write(uintptr_t obj, int *arr, int off, int len) { return len; }  // pretend consumed   // all EGL10.* calls report success; VitaGL owns the context

// ---- Android AssetManager emulation on top of DATA_PATH/obb ----------------------------
#define TAG_ASSET_MGR 0x41534D47
#define TAG_STREAM    0x41535452
typedef struct { int tag; } AssetMgr;
typedef struct { int tag; SceUID fd; long size; long pos; char *mem; int calls; } AssetStream;
static AssetMgr fake_asset_mgr = { TAG_ASSET_MGR };
static int pending_exception;   // set when a Java call "throws" (e.g. FileNotFoundException on AssetManager.open)
void *fake_asset_manager = &fake_asset_mgr;

static void asset_path(char *out, size_t n, const char *name) {
  if (!strncmp(name, "appbundle:/", 11)) name += 11;
  while (*name == '/') name++;
  snprintf(out, n, DATA_PATH "/obb/%s", name);
}
// ---- memory.cfg rewrite: Android heap table totals 393MB; Vita budget ~200MB. Rules match by allocator
// name (whitespace-delimited) and replace the whole size=... token, so conditionals like {{pc}?20M:100M} are
// dropped. Applied to the in-memory copy only; the file on the card is untouched.
static const struct { const char *name; const char *size; } memcfg_rules[] = {
  { "AUDIODATA_GEN", "16M" },   { "GAMEPLAY_GEN", "12M" },     { "GAMEWORLD_GEN", "12M" },
  { "GLOBAL_GEN", "24M" },      { "RENDER_GEN", "48M" },      { "STL_GEN", "8M" },           { "FE_GEN", "8M" },
  { "FE_SFGFX_GEN_A", "12M" },  { "FE_SFGFX_GEN_B", "3M" },    { "FE_SFGFX_AS_SBA", "2M" },
  { "FE_SFGFX_ASCRIPT", "6M" }, { "FE_SFGFX_REN_SBA", "4M" },  { "FE_SFGFX_RENDER", "5M" },
  { "FE_UX_GEN", "3M" },        { "GLOBAL_ASSETTMP", "12M" },  { "GAMEWORLD_SLOTALLOC", "12M" },
  { "ASSETSTREAM_READBUF", "3M" }, { "AUDIO_RWAC", "4M" },
};
static int is_ws(char c) { return c == ' ' || c == '\t'; }
static char *rewrite_memcfg(char *buf, long *size) {
  for (unsigned r = 0; r < sizeof(memcfg_rules)/sizeof(memcfg_rules[0]); r++) {
    const char *name = memcfg_rules[r].name; size_t nl = strlen(name); int hits = 0;
    for (long i = 0; i + (long)nl < *size; i++) {
      if (memcmp(buf + i, name, nl) || !(i == 0 || is_ws(buf[i-1])) || !is_ws(buf[i + nl])) continue;
      // find "size=" on this line
      long j = i + nl; while (j < *size && buf[j] != '\n' && memcmp(buf + j, "size=", 5)) j++;
      if (j >= *size || buf[j] == '\n') continue;
      long vs = j + 5, ve = vs; int depth = 0;
      while (ve < *size) { char c = buf[ve]; if (c == '{') depth++; else if (c == '}') depth--; else if (depth == 0 && (c == ',' || c == ']' || c == ' ' || c == '\n')) break; ve++; }
      long ol = ve - vs, tl = strlen(memcfg_rules[r].size);
      char *nb = malloc(*size - ol + tl + 1);
      memcpy(nb, buf, vs); memcpy(nb + vs, memcfg_rules[r].size, tl); memcpy(nb + vs + tl, buf + ve, *size - ve);
      free(buf); buf = nb; *size = *size - ol + tl; hits++; i = vs + tl;
    }
    debugPrintf("memory.cfg: %s -> %s (%d line%s)\n", name, memcfg_rules[r].size, hits, hits == 1 ? "" : "s");
  }
  buf[*size] = 0; return buf;
}
static AssetStream *asset_open(const char *name) {
  char p[512]; asset_path(p, sizeof p, name);
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) {
    size_t pl = strlen(p);
    if (pl > 4 && !strcmp(p + pl - 4, ".fxo")) {   // shader not shipped (debug-only): substitute the engine's placeholder
      fd = sceIoOpen(DATA_PATH "/obb/mobile/shaders/errormissing.fxo", SCE_O_RDONLY, 0);
      debugPrintf("asset open %s missing -> errormissing.fxo (%s)\n", name, fd < 0 ? "FAIL" : "ok");
    }
    if (fd < 0) { debugPrintf("asset open FAIL %s (FileNotFoundException)\n", p); pending_exception = 1; return NULL; }
  }
  AssetStream *s = calloc(1, sizeof *s); s->tag = TAG_STREAM; s->fd = fd;
  s->size = sceIoLseek(fd, 0, SCE_SEEK_END); sceIoLseek(fd, 0, SCE_SEEK_SET);
  debugPrintf("asset open %s (%ld bytes)\n", name, s->size);
  size_t nl = strlen(name);
  if (nl >= 10 && !strcmp(name + nl - 10, "memory.cfg")) {
    char *buf = malloc(s->size + 1); long got = 0;
    while (got < s->size) { int r = sceIoRead(fd, buf + got, s->size - got); if (r <= 0) break; got += r; }
    sceIoClose(fd); s->fd = -1; s->size = got; s->mem = rewrite_memcfg(buf, &s->size);
  }
  return s;
}
static int asset_read(AssetStream *s, int *jarr, int off, int len) {
  if (!s || s->tag != TAG_STREAM) return -1;
  if (s->pos >= s->size) return -1;
  int n;
  if (s->mem) { n = len; if (n > s->size - s->pos) n = s->size - s->pos; memcpy((char *)(jarr + 1) + off, s->mem + s->pos, n); }
  else n = sceIoRead(s->fd, (char *)(jarr + 1) + off, len);
  s->calls++;
  if (n > 0) s->pos += n;
  return n > 0 ? n : -1;
}
static long long asset_skip(AssetStream *s, long long n) {
  if (!s || s->tag != TAG_STREAM) return 0;
  long np = s->pos + (long)n; if (np > s->size) np = s->size;
  if (!s->mem) sceIoLseek(s->fd, np, SCE_SEEK_SET); long long d = np - s->pos; s->pos = np; return d;
}
static void asset_reset(AssetStream *s) {
  if (!s || s->tag != TAG_STREAM) return;
  if (!s->mem) sceIoLseek(s->fd, 0, SCE_SEEK_SET); s->pos = 0;
}
static void asset_close(AssetStream *s) {
  if (!s || s->tag != TAG_STREAM) return;
  if (s->mem) free(s->mem); else sceIoClose(s->fd);
  s->tag = 0; free(s);
}
static int *asset_list(const char *name) {
  char p[512]; asset_path(p, sizeof p, name);
  SceUID d = sceIoDopen(p);
  char *names[512]; int n = 0;
  if (d >= 0) { SceIoDirent e; while (n < 512 && sceIoDread(d, &e) > 0) names[n++] = strdup(e.d_name); sceIoDclose(d); }
  int *arr = calloc(n + 1, sizeof(int)); arr[0] = n; for (int i = 0; i < n; i++) arr[1 + i] = (int)names[i];
  return arr;
}
enum { M_OPEN = 900, M_OPENFD, M_LIST, M_READ, M_SKIP, M_CLOSE, M_GETLENGTH, M_AVAILABLE, M_RESET, M_MARK, M_GETASSETS };
static const struct { const char *n; int id; } special[] = {
  { "open", M_OPEN }, { "openFd", M_OPENFD }, { "list", M_LIST }, { "read", M_READ },
  { "skip", M_SKIP }, { "close", M_CLOSE }, { "getLength", M_GETLENGTH }, { "available", M_AVAILABLE },
  { "reset", M_RESET }, { "mark", M_MARK }, { "getAssets", M_GETASSETS },
};


// ---- full Blast delegate surface (extracted from the binary's GetMethodId call sites) ----
static float f_dpi(void)        { return 220.0f; }
static float f_battery(void)    { return 1.0f; }
static int  *empty_str_array(void) { static int a[1] = { 0 }; return a; }
static int   gl_view(void)      { return 1; }
static int   cur_height(void)   { return 544; }
static int   cur_width(void)    { return 960; }
static int   notif_id(void)     { static int n = 1; return n++; }
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
  { "GetPrimaryExternalStorageDirectoryRoot",(uintptr_t)storage_ext },
  { "GetPrimaryExternalStorageDirectory",(uintptr_t)storage_ext },
  { "GetPrimaryExternalStorageState",(uintptr_t)ret1 },
  // android/media/AudioTrack via EAAudioCore Java helper — PCM sink; real SceAudio output comes later
  { "play",                 (uintptr_t)retv }, { "stop",              (uintptr_t)retv },
  { "write",                (uintptr_t)audio_write },
  { "Startup",              (uintptr_t)retv }, { "Shutdown",          (uintptr_t)retv },
  // com/ea/blast/MainActivity / DisplayAndroidDelegate
  { "GetDisplayWidth",      (uintptr_t)screen_w },{ "GetDisplayHeight",(uintptr_t)screen_h },
  { "GetDisplayDensity",    (uintptr_t)density },{ "GetApiLevel",      (uintptr_t)api_level_str },
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
  { "<init>",               (uintptr_t)ret1 },
  // device capability queries (DeviceAndroid delegate)
  { "GetAccelerometerCount", (uintptr_t)ret0 }, { "GetCameraCount", (uintptr_t)ret0 }, { "GetCompassCount", (uintptr_t)ret0 },
  { "GetGyroscopeCount", (uintptr_t)ret0 }, { "GetMicrophoneCount", (uintptr_t)ret0 }, { "GetTouchPadCount", (uintptr_t)ret0 },
  { "GetTouchScreenCount", (uintptr_t)ret1 }, { "GetTrackBallCount", (uintptr_t)ret0 }, { "GetVibratorCount", (uintptr_t)ret0 },
  { "GetApplicationVersionCode", (uintptr_t)app_vercode }, { "GetApplicationVersion", (uintptr_t)app_ver },
  { "GetChipset", (uintptr_t)dev_chipset }, { "GetFirmware", (uintptr_t)dev_fw }, { "GetManufacturer", (uintptr_t)dev_maker },
  { "GetDeviceModel", (uintptr_t)dev_model }, { "GetDeviceUniqueId", (uintptr_t)dev_uid }, { "GetHardwareFloatingPointSupport", (uintptr_t)dev_fp },
  { "GetPlatformVersion", (uintptr_t)platform_ver }, { "GetProcessorArchitecture", (uintptr_t)proc_arch }, { "GetLocale", (uintptr_t)locale_str },
  { "GetTotalMemory", (uintptr_t)ret1 }, { "GetAvailableMemory", (uintptr_t)ret1 },
  // Blast delegates: lifecycle / display / keyboard / notifications / misc — all no-op or sane defaults
  { "ApplyKeepAwake", (uintptr_t)retv }, { "AttachView", (uintptr_t)retv }, { "BringToFront", (uintptr_t)retv },
  { "Cancel", (uintptr_t)retv }, { "CancelAllLocalNotifications", (uintptr_t)ret1 }, { "CancelLocalNotification", (uintptr_t)ret1 },
  { "DetachView", (uintptr_t)retv }, { "EnableAAR", (uintptr_t)retv }, { "Exit", (uintptr_t)retv },
  { "GenerateUniqueNotificationId", (uintptr_t)notif_id }, { "GetBatteryLevel", (uintptr_t)f_battery },
  { "GetCommandLineArguments", (uintptr_t)empty_str_array }, { "GetCurrentHeight", (uintptr_t)cur_height },
  { "GetDefaultWidth", (uintptr_t)retv }, { "GetDisplayOrientationLock", (uintptr_t)ret0 },
  { "GetDpiX", (uintptr_t)f_dpi }, { "GetDpiY", (uintptr_t)f_dpi }, { "GetGLView", (uintptr_t)gl_view },
  { "GetPendingNFC", (uintptr_t)ret0 }, { "GetStdOrientation", (uintptr_t)ret0 }, { "Init", (uintptr_t)ret1 },
  { "IntentView", (uintptr_t)ret0 }, { "IsAvailable", (uintptr_t)ret0 }, { "IsNavigationVisible", (uintptr_t)ret0 },
  { "IsPowerConnected", (uintptr_t)ret1 }, { "IsVisible", (uintptr_t)ret1 },
  { "NotifyPendingBackgroundLocalNotifications", (uintptr_t)ret0 }, { "NotifyPendingBackgroundPushNotifications", (uintptr_t)retv },
  { "NotifyPendingStartupLocalNotifications", (uintptr_t)ret0 }, { "NotifyPendingStartupPushNotifications", (uintptr_t)retv },
  { "OnLifeCycleFocusGained", (uintptr_t)retv }, { "OnPhysicalKeyboardVisibilityChanged", (uintptr_t)retv },
  { "RegisterApplicationForNotifications", (uintptr_t)retv }, { "RegisterUserData", (uintptr_t)retv },
  { "ScheduleLocalNotification", (uintptr_t)ret0 }, { "SetEnabled", (uintptr_t)retv }, { "SetEnterKeyLabel", (uintptr_t)retv },
  { "SetLayout", (uintptr_t)retv }, { "SetMimeType", (uintptr_t)retv }, { "SetShiftEnabled", (uintptr_t)retv },
  { "SetStdOrientation", (uintptr_t)retv }, { "SetUpdateFrequency", (uintptr_t)retv }, { "SetViewFrame", (uintptr_t)retv },
  { "UnregisterApplicationForNotifications", (uintptr_t)retv }, { "UserSetVisible", (uintptr_t)retv },
  { "VerifyUrlLaunch", (uintptr_t)retv }, { "Vibrate", (uintptr_t)retv }, { "IsPhysicalKeyboardVisible", (uintptr_t)ret0 },
  { "IsTouchScreenMultiTouch", (uintptr_t)ret1 }, { "GetDeviceName", (uintptr_t)device_name },
  { "GetDefaultHeight", (uintptr_t)cur_height }, { "GetCurrentWidth", (uintptr_t)cur_width }, { "GetDefaultWidthI", (uintptr_t)cur_width },
  // EglAndroidDelegate object getters — opaque handles, VitaGL owns the real context
  { "GetEgl", (uintptr_t)ret1 }, { "GetEglNoContext", (uintptr_t)ret0 }, { "GetEglNoDisplay", (uintptr_t)ret0 },
  { "GetEglNoSurface", (uintptr_t)ret0 }, { "GetSurface", (uintptr_t)ret1 },
  // Trust5 store (Java side) — offline: nothing purchased, no pending purchases
  { "authenticatePurchases", (uintptr_t)retv }, { "checkWaitingPurchases", (uintptr_t)retv }, { "erasePurchaseState", (uintptr_t)retv },
  { "getPendingId", (uintptr_t)ret0 }, { "requestStoreItems", (uintptr_t)retv }, { "purchaseStoreItem", (uintptr_t)retv },
  { "requestPurchasedItems", (uintptr_t)retv }, { "isItemAlreadyPurchased", (uintptr_t)ret0 }, { "initialize", (uintptr_t)retv },
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
  { static char seen[2048]; if (!strstr(seen, name) && strlen(seen) + strlen(name) + 2 < sizeof seen) { strcat(seen, name); strcat(seen, " "); debugPrintf("JNI: lookup %s %s (class %d)\n", name, sig, clazz); } }
  for (unsigned i = 0; i < NMETHODS; i++) if (!strcmp(methods[i].name, name)) return i + 1;
  for (int i = 0; i < unknown_count; i++) if (!strcmp(unknown_names[i], name)) return UNKNOWN_BASE + i;
  debugPrintf("JNI: unknown method %s %s (class %d)\n", name, sig, clazz);
  if (unknown_count < 512) unknown_names[unknown_count] = strdup(name);
  return UNKNOWN_BASE + unknown_count++;
}
static uintptr_t lookup(int id) { return (id >= 1 && id <= (int)NMETHODS) ? methods[id-1].func : 0; }

// ---- Call*Method: args are passed via va_list; we forward up to 6 ints (covers every sig seen) ----
#define FWD(R, f, obj, a) ((R(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))f)(obj,a[0],a[1],a[2],a[3],a[4],a[5])
#define CALLV(NAME, RET, CAST) \
  static RET NAME##V(void *env, uintptr_t obj, int mid, va_list args) { \
    if (mid >= M_OPEN && mid <= M_GETASSETS) return (RET)special_call(mid, obj, args); \
    uintptr_t f = lookup(mid); \
    if (!f) { if (mid < UNKNOWN_BASE || mid - UNKNOWN_BASE >= unknown_count) return (RET)0; \
              static int warned[512]; if (!warned[mid-UNKNOWN_BASE]++) debugPrintf("JNI: call to unimplemented %s\n", unknown_names[mid-UNKNOWN_BASE]); return (RET)0; } \
    uintptr_t a[6]; for (int i = 0; i < 6; i++) a[i] = va_arg(args, uintptr_t); \
    return FWD(RET, f, obj, a); } \
  static RET NAME(void *env, uintptr_t obj, int mid, ...) { va_list ap; va_start(ap, mid); RET r = NAME##V(env, obj, mid, ap); va_end(ap); return r; } \
  static RET NAME##A(void *env, uintptr_t obj, int mid, uintptr_t *args) { \
    if (mid >= M_OPEN && mid <= M_GETASSETS) return (RET)special_callA(mid, obj, args); \
    uintptr_t f = lookup(mid); if (!f) return (RET)0; return FWD(RET, f, obj, args); }

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
    case M_AVAILABLE: { AssetStream *s = (AssetStream *)obj; return (s && s->tag == TAG_STREAM) ? s->size - s->pos : 0; }
    case M_RESET: asset_reset((AssetStream *)obj); return 0;
    case M_MARK: return 0;
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
    case M_AVAILABLE: { AssetStream *s = (AssetStream *)obj; return (s && s->tag == TAG_STREAM) ? s->size - s->pos : 0; }
    case M_RESET: asset_reset((AssetStream *)obj); return 0;
    case M_MARK: return 0;
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
static const char *GetStringUTFChars(void *env, char *s, int *isCopy) { if (isCopy) *isCopy = 0; if ((uintptr_t)s < 0x10000) { if (s) debugPrintf("JNI: GetStringUTFChars on non-string %p\n", s); return ""; } return s; }
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
static int  ExceptionCheck(void *env) { return pending_exception; }
static void ExceptionClear(void *env) { pending_exception = 0; }
static void *ExceptionOccurred(void *env) { return pending_exception ? (void *)&pending_exception : NULL; }
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
static void *env_table[233];
static void **env_ptr = env_table;
void *fake_env = &env_ptr;

static int   JavaVM_GetEnv(void *vm, void **env, int ver) { *env = fake_env; return 0; }
static int   JavaVM_AttachCurrentThread(void *vm, void **env, void *args) { *env = fake_env; return 0; }
static int   JavaVM_DetachCurrentThread(void *vm) { return 0; }
static void *vm_table[8];
static void **vm_ptr = vm_table;
void *fake_vm = &vm_ptr;

// ---- generic fillers for slots not needing real behaviour ----
static int  g_ret0(void) { return 0; }
static void g_void(void) {}
static void *g_self(void *env, void *o) { return o; }          // NewLocalRef / NewWeakGlobalRef / Get*Elements-style identity
static int  g_ret1(void) { return 1; }
static long long g_ret0l(void) { return 0; }
static float g_ret0f(void) { return 0.0f; }
static double g_ret0d(void) { return 0.0; }
// element sizes for typed arrays: layout is [len][data...]
static int *g_newarr(void *env, int n, int esz) { int *a = calloc(1, 4 + n * esz); a[0] = n; return a; }
static int *NewBooleanArray(void *e, int n) { return g_newarr(e, n, 1); }
static int *NewCharArray(void *e, int n)    { return g_newarr(e, n, 2); }
static int *NewShortArray(void *e, int n)   { return g_newarr(e, n, 2); }
static int *NewLongArray(void *e, int n)    { return g_newarr(e, n, 8); }
static int *NewFloatArray(void *e, int n)   { return g_newarr(e, n, 4); }
static int *NewDoubleArray(void *e, int n)  { return g_newarr(e, n, 8); }
static void *g_elems(void *env, int *a, int *isCopy) { if (isCopy) *isCopy = 0; return a + 1; }
static void g_release(void *env, int *a, void *e, int mode) {}
#define REGION_GET(esz) static void g_getregion##esz(void *env, int *a, int start, int len, void *buf) { memcpy(buf, (char *)(a + 1) + start * esz, len * esz); }
#define REGION_SET(esz) static void g_setregion##esz(void *env, int *a, int start, int len, const void *buf) { memcpy((char *)(a + 1) + start * esz, buf, len * esz); }
REGION_GET(1) REGION_GET(2) REGION_GET(4) REGION_GET(8) REGION_SET(1) REGION_SET(2) REGION_SET(4) REGION_SET(8)
static void *NewObjectV(void *env, int c, int m, va_list a) { return (void *)1; }
static void *NewObjectA(void *env, int c, int m, void *a) { return (void *)1; }
static void *NewString(void *env, const unsigned short *s, int len) { char *r = calloc(1, len + 1); for (int i = 0; i < len; i++) r[i] = (char)s[i]; return r; }
static const unsigned short *GetStringChars(void *env, const char *s, int *c) { int n = strlen(s); unsigned short *r = calloc(n + 1, 2); for (int i = 0; i < n; i++) r[i] = (unsigned char)s[i]; if (c) *c = 1; return r; }
static void ReleaseStringChars(void *env, const char *s, const unsigned short *c) { free((void *)c); }
static void GetStringUTFRegion(void *env, const char *s, int start, int len, char *buf) { memcpy(buf, s + start, len); buf[len] = 0; }
static int  Throw(void *env, void *o) { debugPrintf("JNI: Throw\n"); return 0; }
static int  ThrowNew(void *env, int c, const char *m) { debugPrintf("JNI: ThrowNew %s\n", m ? m : ""); return 0; }
static void ExceptionDescribe(void *env) {}
static void FatalError(void *env, const char *m) { debugPrintf("JNI: FatalError %s\n", m ? m : ""); abort(); }
static int  IsInstanceOf(void *env, void *o, int c) { return 1; }
static int  IsAssignableFrom(void *env, int a, int b) { return 1; }

#define SLOT(n, f) env_table[n] = (void *)f
void jni_init(void) {
  for (int i = 0; i < 233; i++) env_table[i] = (void *)jni_trap;
  SLOT(4, GetVersion);
  SLOT(6, FindClass);
  SLOT(10, g_ret0);
  SLOT(11, IsAssignableFrom);
  SLOT(13, Throw);
  SLOT(14, ThrowNew);
  SLOT(15, ExceptionOccurred);
  SLOT(16, ExceptionDescribe);
  SLOT(17, ExceptionClear);
  SLOT(18, FatalError);
  SLOT(19, PushLocalFrame);
  SLOT(20, PopLocalFrame);
  SLOT(21, NewGlobalRef);
  SLOT(22, DeleteGlobalRef);
  SLOT(23, DeleteLocalRef);
  SLOT(24, IsSameObject);
  SLOT(25, g_self);
  SLOT(26, g_ret0);
  SLOT(27, g_ret1);
  SLOT(28, NewObject);
  SLOT(29, NewObjectV);
  SLOT(30, NewObjectA);
  SLOT(31, GetObjectClass);
  SLOT(32, IsInstanceOf);
  SLOT(33, GetMethodID);
  SLOT(113, GetMethodID);
  SLOT(34, CallObjectMethod);
  SLOT(35, CallObjectMethodV);
  SLOT(36, CallObjectMethodA);
  SLOT(37, CallBooleanMethod);
  SLOT(38, CallBooleanMethodV);
  SLOT(39, CallBooleanMethodA);
  SLOT(40, CallIntMethod);
  SLOT(41, CallIntMethodV);
  SLOT(42, CallIntMethodA);
  SLOT(43, CallIntMethod);
  SLOT(44, CallIntMethodV);
  SLOT(45, CallIntMethodA);
  SLOT(46, CallIntMethod);
  SLOT(47, CallIntMethodV);
  SLOT(48, CallIntMethodA);
  SLOT(49, CallIntMethod);
  SLOT(50, CallIntMethodV);
  SLOT(51, CallIntMethodA);
  SLOT(52, CallLongMethod);
  SLOT(53, CallLongMethodV);
  SLOT(54, CallLongMethodA);
  SLOT(55, CallFloatMethod);
  SLOT(56, CallFloatMethodV);
  SLOT(57, g_ret0f);
  SLOT(58, g_ret0d);
  SLOT(59, g_ret0d);
  SLOT(60, g_ret0d);
  SLOT(61, CallVoidMethod);
  SLOT(62, CallVoidMethodV);
  SLOT(63, CallVoidMethodA);
  SLOT(64, g_ret0);
  SLOT(65, g_ret0);
  SLOT(66, g_ret0);
  SLOT(67, g_ret0);
  SLOT(68, g_ret0);
  SLOT(69, g_ret0);
  SLOT(70, g_ret0);
  SLOT(71, g_ret0);
  SLOT(72, g_ret0);
  SLOT(73, g_ret0);
  SLOT(74, g_ret0);
  SLOT(75, g_ret0);
  SLOT(76, g_ret0);
  SLOT(77, g_ret0);
  SLOT(78, g_ret0);
  SLOT(79, g_ret0);
  SLOT(80, g_ret0);
  SLOT(81, g_ret0);
  SLOT(82, g_ret0);
  SLOT(83, g_ret0);
  SLOT(84, g_ret0);
  SLOT(85, g_ret0);
  SLOT(86, g_ret0);
  SLOT(87, g_ret0);
  SLOT(88, g_ret0);
  SLOT(89, g_ret0);
  SLOT(90, g_ret0);
  SLOT(91, g_ret0);
  SLOT(92, g_ret0);
  SLOT(93, g_ret0);
  SLOT(114, CallObjectMethod);
  SLOT(115, CallObjectMethodV);
  SLOT(116, CallObjectMethodA);
  SLOT(117, CallBooleanMethod);
  SLOT(118, CallBooleanMethodV);
  SLOT(119, CallBooleanMethodA);
  SLOT(120, CallIntMethod);
  SLOT(121, CallIntMethodV);
  SLOT(122, CallIntMethodA);
  SLOT(123, CallIntMethod);
  SLOT(124, CallIntMethodV);
  SLOT(125, CallIntMethodA);
  SLOT(126, CallIntMethod);
  SLOT(127, CallIntMethodV);
  SLOT(128, CallIntMethodA);
  SLOT(129, CallIntMethod);
  SLOT(130, CallIntMethodV);
  SLOT(131, CallIntMethodA);
  SLOT(132, CallLongMethod);
  SLOT(133, CallLongMethodV);
  SLOT(134, CallLongMethodA);
  SLOT(135, CallFloatMethod);
  SLOT(136, CallFloatMethodV);
  SLOT(137, g_ret0f);
  SLOT(138, g_ret0d);
  SLOT(139, g_ret0d);
  SLOT(140, g_ret0d);
  SLOT(141, CallVoidMethod);
  SLOT(142, CallVoidMethodV);
  SLOT(143, CallVoidMethodA);
  SLOT(94, GetFieldID);
  SLOT(144, GetStaticFieldID);
  SLOT(95, g_ret0);
  SLOT(96, g_ret0);
  SLOT(97, g_ret0);
  SLOT(98, g_ret0);
  SLOT(99, g_ret0);
  SLOT(100, g_ret0);
  SLOT(101, g_ret0l);
  SLOT(102, g_ret0f);
  SLOT(103, g_ret0d);
  SLOT(145, g_ret0);
  SLOT(146, g_ret0);
  SLOT(147, g_ret0);
  SLOT(148, g_ret0);
  SLOT(149, g_ret0);
  SLOT(150, g_ret0);
  SLOT(151, g_ret0l);
  SLOT(152, g_ret0f);
  SLOT(153, g_ret0d);
  SLOT(104, g_void);
  SLOT(105, g_void);
  SLOT(106, g_void);
  SLOT(107, g_void);
  SLOT(108, g_void);
  SLOT(109, g_void);
  SLOT(110, g_void);
  SLOT(111, g_void);
  SLOT(112, g_void);
  SLOT(154, g_void);
  SLOT(155, g_void);
  SLOT(156, g_void);
  SLOT(157, g_void);
  SLOT(158, g_void);
  SLOT(159, g_void);
  SLOT(160, g_void);
  SLOT(161, g_void);
  SLOT(162, g_void);
  SLOT(163, NewString);
  SLOT(164, GetStringLength);
  SLOT(165, GetStringChars);
  SLOT(166, ReleaseStringChars);
  SLOT(167, NewStringUTF);
  SLOT(168, GetStringUTFLength);
  SLOT(169, GetStringUTFChars);
  SLOT(170, ReleaseStringUTFChars);
  SLOT(171, GetArrayLength);
  SLOT(172, NewObjectArray);
  SLOT(173, GetObjectArrayElement);
  SLOT(174, SetObjectArrayElement);
  SLOT(175, NewBooleanArray);
  SLOT(176, NewByteArray);
  SLOT(177, NewCharArray);
  SLOT(178, NewShortArray);
  SLOT(179, NewIntArray);
  SLOT(180, NewLongArray);
  SLOT(181, NewFloatArray);
  SLOT(182, NewDoubleArray);
  SLOT(183, g_elems);
  SLOT(191, g_release);
  SLOT(199, g_getregion1);
  SLOT(207, g_setregion1);
  SLOT(184, g_elems);
  SLOT(192, g_release);
  SLOT(200, g_getregion1);
  SLOT(208, g_setregion1);
  SLOT(185, g_elems);
  SLOT(193, g_release);
  SLOT(201, g_getregion2);
  SLOT(209, g_setregion2);
  SLOT(186, g_elems);
  SLOT(194, g_release);
  SLOT(202, g_getregion2);
  SLOT(210, g_setregion2);
  SLOT(187, g_elems);
  SLOT(195, g_release);
  SLOT(203, g_getregion4);
  SLOT(211, g_setregion4);
  SLOT(188, g_elems);
  SLOT(196, g_release);
  SLOT(204, g_getregion8);
  SLOT(212, g_setregion8);
  SLOT(189, g_elems);
  SLOT(197, g_release);
  SLOT(205, g_getregion4);
  SLOT(213, g_setregion4);
  SLOT(190, g_elems);
  SLOT(198, g_release);
  SLOT(206, g_getregion8);
  SLOT(214, g_setregion8);
  SLOT(215, RegisterNatives);
  SLOT(216, g_ret0);
  SLOT(217, g_ret0);
  SLOT(218, g_ret0);
  SLOT(219, GetJavaVM);
  SLOT(220, g_void);
  SLOT(221, GetStringUTFRegion);
  SLOT(222, GetPrimitiveArrayCritical);
  SLOT(223, ReleasePrimitiveArrayCritical);
  SLOT(224, GetStringChars);
  SLOT(225, ReleaseStringChars);
  SLOT(226, g_self);
  SLOT(227, g_void);
  SLOT(228, ExceptionCheck);
  SLOT(229, g_ret0);
  SLOT(230, g_ret0);
  SLOT(231, g_ret0);
  SLOT(232, g_ret1);
  vm_table[4] = (void *)JavaVM_AttachCurrentThread; vm_table[5] = (void *)JavaVM_DetachCurrentThread;
  vm_table[6] = (void *)JavaVM_GetEnv; vm_table[7] = (void *)JavaVM_AttachCurrentThread;
}
