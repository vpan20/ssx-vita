// SSX (Android, com.ea.ssx v0.0.7833) → PS Vita so-loader. Boot order mirrors what
// com.ea.blast.MainActivity / MainThread do on the Java side. See README for the iteration loop.
#include <vitasdk.h>
#include <kubridge.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "so_util.h"
#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX (0x0C20D050)   // not in this VitaSDK's headers; same value so_util.c uses
#endif
#include "jni_patch.h"
#include "stubs.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define LOAD_ADDR_GAME   0x94000000
#define LOAD_ADDR_GNUSTL 0x9C000000   // libgame.so is ~40MB mapped; keep gnustl clear of it
#define MEMORY_NEWLIB_MB 272
#define MEMORY_VITAGL_MB 16          // tune: game streams textures from .big caches

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;
unsigned int sceLibcHeapSize = 8 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;
extern so_default_dynlib default_dynlib[]; extern int default_dynlib_size;

static so_module game_mod, gnustl_mod;

// ---- JNI entry points exported by libgame.so (resolved after load) ----
typedef int  (*fn_JNI_OnLoad)(void *vm, void *reserved);
typedef void (*fn_env_v)(void *env, void *thiz);
typedef void (*fn_env_i)(void *env, void *thiz, int);
typedef void (*fn_env_ii)(void *env, void *thiz, int, int);
typedef void (*fn_env_iii)(void *env, void *thiz, int, int, int);
typedef void (*fn_env_iiff)(void *env, void *thiz, int, int, float, float);

static void fatal(const char *msg) {
  debugPrintf("FATAL: %s\n", msg);
  SceMsgDialogUserMessageParam m = { .buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK, .msg = (const SceChar8 *)msg };
  SceMsgDialogParam p; sceMsgDialogParamInit(&p); p.mode = SCE_MSG_DIALOG_MODE_USER_MSG; p.userMsgParam = &m;
  sceMsgDialogInit(&p); while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) { sceDisplayWaitVblankStart(); }
  sceKernelExitProcess(0);
}




// ---- Hooking with trampolines (the bundled so_util hook has no continuation) ----------------------
// Trampoline = first two original instructions (must be position-independent) + jump back to orig+8.
static uint8_t *tramp_pool; static int tramp_used;
static void *make_trampoline(uintptr_t target) {
  // Use the page-rounding slack at the end of the game's own RX block (memsz 0x166C634, block 0x166D000): ~2.4KB free.
  if (!tramp_pool) tramp_pool = (uint8_t *)(game_mod.text_base + 0x166C640);
  if (tramp_used + 16 > 0x9C0) { debugPrintf("trampoline pool full\n"); return NULL; }
  uint32_t t[4] = { *(uint32_t *)target, *(uint32_t *)(target + 4), 0xe51ff004, (uint32_t)(target + 8) };
  uint8_t *at = tramp_pool + tramp_used; tramp_used += 16;
  kuKernelCpuUnrestrictedMemcpy(at, t, sizeof t);
  kuKernelFlushCaches(at, 16);
  return at;
}
#define HOOK(off, fn, orig_var) do { uintptr_t a = game_mod.text_base + (off); orig_var = make_trampoline(a); if (orig_var) hook_addr(a, (uintptr_t)fn); debugPrintf("hook " #fn " @%p tramp=%p\n", (void *)a, orig_var); } while (0)

// AssetStream::Loader::TranslateStream(parent, asset, stream, flag) — an asset released while still queued reaches
// here with a dead (zero) type pointer and crashes the translator thread. Report it failed (3) instead.
static int (*orig_TranslateStream)(void *, void *, void *, int);
static int hook_TranslateStream(void *parent, void *asset, void *stream, int flag) {
  if (!asset || !*(uint32_t *)asset) {
    uint32_t *w = asset;
    debugPrintf("TranslateStream: dead asset %p: %08X %08X %08X %08X | %08X %08X %08X %08X\n", asset,
      w ? w[0] : 0, w ? w[1] : 0, w ? w[2] : 0, w ? w[3] : 0, w ? w[4] : 0, w ? w[5] : 0, w ? w[6] : 0, w ? w[7] : 0);
    return 3;
  }
  return orig_TranslateStream(parent, asset, stream, flag);
}
// AssetStream::Asset::~Asset() (two variants) — log who destroys assets during the first frames, to find the
// premature release that leaves a dead asset in the translator queue.
static void (*orig_AssetDtor1)(void *); static void (*orig_AssetDtor2)(void *);
static int dtor_logged;
static void log_dtor(void *asset, void *ret) {
  if (dtor_logged++ < 40) {
    uint32_t *w = asset; const char *nm = (const char *)w[8];   // best-effort: name pointer often near +0x20
    debugPrintf("~Asset(%p) from %p (game+0x%X) size=%u name@+0x20=%p\n", asset, ret, (unsigned)((uintptr_t)ret - game_mod.text_base), w[2], nm);
  }
}
static void hook_AssetDtor1(void *asset) { log_dtor(asset, __builtin_return_address(0)); orig_AssetDtor1(asset); }
static void hook_AssetDtor2(void *asset) { log_dtor(asset, __builtin_return_address(0)); orig_AssetDtor2(asset); }

static void install_hooks(void) {
  HOOK(0x638408, hook_AssetDtor1, orig_AssetDtor1);
  HOOK(0x639180, hook_AssetDtor2, orig_AssetDtor2);
  HOOK(0x63ddf8, hook_TranslateStream, orig_TranslateStream);
}

static int file_exists(const char *p) { SceIoStat s; return sceIoGetstat(p, &s) >= 0; }

int main(int argc, char *argv[]) {
  sceKernelChangeThreadCpuAffinityMask(0, 0x40000);   // pin main thread; game spawns its own worker threads
  sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  sceIoMkdir(DATA_PATH, 0777); sceIoMkdir(DATA_PATH "/internal", 0777); sceIoMkdir(DATA_PATH "/obb", 0777);
  sceIoRemove(DATA_PATH "/ssx.log");
  debugPrintf("=== SSX Vita boot ===\n");

  if (!file_exists(DATA_PATH "/lib/libgame.so"))  fatal("Missing " DATA_PATH "/lib/libgame.so");
  if (!file_exists(DATA_PATH "/lib/libgnustl_shared.so")) fatal("Missing " DATA_PATH "/lib/libgnustl_shared.so");
  if (!file_exists(DATA_PATH "/obb/EAMCore.ini")) fatal("Missing .big assets in " DATA_PATH "/obb/");

  // 1. gnustl first: libgame imports std::string/rb_tree internals from it.
  if (so_load(&gnustl_mod, DATA_PATH "/lib/libgnustl_shared.so", LOAD_ADDR_GNUSTL) < 0) fatal("so_load gnustl");
  so_relocate(&gnustl_mod);
  so_resolve(&gnustl_mod, default_dynlib, default_dynlib_size, 0);
  so_flush_caches(&gnustl_mod); so_initialize(&gnustl_mod);

  // 2. libgame. default_dynlib_only=0 → unresolved symbols fall through to already-loaded modules (gnustl).
  int r = so_load(&game_mod, DATA_PATH "/lib/libgame.so", LOAD_ADDR_GAME);
  if (r < 0) { debugPrintf("so_load libgame error 0x%08X free=%d\n", r, sceKernelGetFreeMemorySize(NULL)); fatal("so_load libgame"); }
  so_relocate(&game_mod);             debugPrintf("relocate ok\n");
  so_resolve(&game_mod, default_dynlib, default_dynlib_size, 0); debugPrintf("resolve ok\n");
  // TODO(step 4): patch_game() — hook allocator sizes / disable Nimble init / skip vp6 replays once addresses are known from Ghidra.
  install_hooks();
  so_flush_caches(&game_mod);         debugPrintf("flush ok\n");
  so_initialize(&game_mod);           debugPrintf("initialize ok (static constructors ran)\n");

  // 3. Graphics: VitaGL provides the GLES2 symbols in default_dynlib. Game shaders are GLSL ES → need
  //    VitaGL's runtime translator (vglInitWithCustomThreshold + shark) or precompiled CG. See README §Shaders.
  if (!file_exists("ur0:data/libshacccg.suprx")) fatal("libshacccg.suprx missing — run ShaRKBR33D");
  debugPrintf("vitaGL init...\n");
  vglInitWithCustomThreshold(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_MB * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);
  debugPrintf("vitaGL ok\n");

  // 4. JNI boot sequence (order taken from Blast's MainActivity.onCreate / MainThread.run).
  //    Every entry is called with a full 8-word argument set; extras are harmless on ARM.
  jni_init();
  typedef void (*fn_jni)(void *env, void *thiz, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
  #define J(name) ((fn_jni)so_symbol(&game_mod, name))
  #define CALL(name, ...) do { fn_jni f = J(name); if (!f) fatal("missing " name); f(fake_env, NULL, __VA_ARGS__); debugPrintf(name " ok\n"); } while (0)
  fn_JNI_OnLoad JNI_OnLoad = (fn_JNI_OnLoad)so_symbol(&game_mod, "JNI_OnLoad");
  if (!JNI_OnLoad) fatal("missing JNI_OnLoad");
  JNI_OnLoad(fake_vm, NULL);           debugPrintf("JNI_OnLoad ok\n");

  uintptr_t sInternal = (uintptr_t)jni_new_string(DATA_PATH "/internal");
  uintptr_t sExternal = (uintptr_t)jni_new_string(DATA_PATH "/external");
  uintptr_t sObb      = (uintptr_t)jni_new_string(DATA_PATH "/obb");
  uintptr_t am        = (uintptr_t)fake_asset_manager;
  sceIoMkdir(DATA_PATH "/external", 0777);

  static int fake_activity = 0x41435459;   // any non-null object; the game only keeps a global ref to it
  { fn_jni f = J("Java_com_ea_ssx_MainActivity_InitGameApplication"); if (f) { f(fake_env, (void *)&fake_activity, 0,0,0,0,0,0); debugPrintf("InitGameApplication ok\n"); } }
  CALL("Java_com_ea_EAThread_EAThread_Init", 0,0,0,0,0,0);
  CALL("Java_com_ea_EAIO_EAIO_StartupNativeImpl", am, sInternal, sExternal, sObb, 0,0);   // (assetManager, dataDir, externalDir, apkPath)
  CALL("Java_com_ea_EAMIO_StorageDirectory_StartupNativeImpl", 0,0,0,0,0,0);
  CALL("Java_com_ea_EAAudioCore_AndroidEAAudioCore_Init", 44100, 16, 2, 2048, 0,0);      // (sampleRate, bits, channels, bufferFrames) — best guess
  CALL("Java_com_ea_EAMAudio_EAMAudioCoreWrapper_NativeStartup", 0,0,0,0,0,0);
  CALL("Java_com_ea_VideoPlayer_PlayerAndroid_StartupNativeImpl", 0,0,0,0,0,0);
  CALL("Java_com_ea_blast_MainActivity_NativeOnCreate", 0,0,0,0,0,0);
  CALL("Java_com_ea_blast_MainActivity_NativeOnResume", 1,0,0,0,0,0);
  CALL("Java_com_ea_blast_MainActivity_NativeOnWindowFocusChanged", 1,0,0,0,0,0);
  CALL("Java_com_ea_blast_MainThread_NativeOnSurfaceCreated", 1,0,0,0,0,0);
  CALL("Java_com_ea_blast_MainThread_NativeOnSurfaceChanged", SCREEN_W, SCREEN_H, 0,0,0,0);
  fn_jni DrawFrame = J("Java_com_ea_blast_MainThread_NativeOnDrawFrame");
  fn_env_iiff Touch = (fn_env_iiff)so_symbol(&game_mod, "Java_com_ea_blast_TouchSurfaceAndroid_NativeOnPointerEvent");
  debugPrintf("boot chain complete — entering frame loop\n");

  // 5. Frame loop. Android calls NativeOnDrawFrame from GLSurfaceView; we do the same and swap ourselves.
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  int touching = 0; unsigned frame = 0;
  for (;;) {
    if (frame < 5 || frame % 300 == 0) { SceKernelFreeMemorySizeInfo fi = { .size = sizeof fi }; sceKernelGetFreeMemorySize(&fi); debugPrintf("frame %u begin (free user=%dKB cdram=%dKB)\n", frame, fi.size_user / 1024, fi.size_cdram / 1024); }
    frame++;
    SceTouchData td; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (Touch) {
      if (td.reportNum > 0) { float x = td.report[0].x / 1920.0f * SCREEN_W, y = td.report[0].y / 1088.0f * SCREEN_H;
        Touch(fake_env, NULL, 0, touching ? 2 /*MOVE*/ : 0 /*DOWN*/, x, y); touching = 1; }
      else if (touching) { Touch(fake_env, NULL, 0, 1 /*UP*/, 0, 0); touching = 0; }
    }
    // TODO(step 4): map SceCtrl buttons → Blast key codes (Android KEYCODE_DPAD_*, BUTTON_A…) via Key/KeyUp once the
    // game's gamepad path is confirmed; the console-origin code very likely has a full pad path behind it.
    DrawFrame(fake_env, NULL, 0,0,0,0,0,0);
    if (frame <= 5) { GLenum e = glGetError(); if (e) debugPrintf("  glError 0x%X after frame %u\n", e, frame - 1); }
    vglSwapBuffers(GL_FALSE);
    if (frame <= 5) debugPrintf("  frame %u swapped\n", frame - 1);
  }
  return 0;
}
