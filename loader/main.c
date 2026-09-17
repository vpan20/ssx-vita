// SSX (Android, com.ea.ssx v0.0.7833) → PS Vita so-loader. Boot order mirrors what
// com.ea.blast.MainActivity / MainThread do on the Java side. See README for the iteration loop.
#include <vitasdk.h>
#include <kubridge.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "so_util.h"
#include "jni_patch.h"
#include "stubs.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define LOAD_ADDR_GAME   0x94000000
#define LOAD_ADDR_GNUSTL 0x9C000000   // libgame.so is ~40MB mapped; keep gnustl clear of it
#define MEMORY_NEWLIB_MB 160
#define MEMORY_VITAGL_MB 64          // tune: game streams textures from .big caches

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
  int touching = 0;
  for (;;) {
    SceTouchData td; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (Touch) {
      if (td.reportNum > 0) { float x = td.report[0].x / 1920.0f * SCREEN_W, y = td.report[0].y / 1088.0f * SCREEN_H;
        Touch(fake_env, NULL, 0, touching ? 2 /*MOVE*/ : 0 /*DOWN*/, x, y); touching = 1; }
      else if (touching) { Touch(fake_env, NULL, 0, 1 /*UP*/, 0, 0); touching = 0; }
    }
    // TODO(step 4): map SceCtrl buttons → Blast key codes (Android KEYCODE_DPAD_*, BUTTON_A…) via Key/KeyUp once the
    // game's gamepad path is confirmed; the console-origin code very likely has a full pad path behind it.
    DrawFrame(fake_env, NULL, 0,0,0,0,0,0);
    vglSwapBuffers(GL_FALSE);
  }
  return 0;
}
