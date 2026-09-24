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
  uint32_t i0 = *(uint32_t *)target, i1 = *(uint32_t *)(target + 4);
  uint8_t *at = tramp_pool + tramp_used;
  if ((i0 & 0x0F7F0000) == 0x051F0000) {            // ldr rX, [pc, #imm]: re-emit as a load from an embedded literal
    uint32_t imm = i0 & 0xFFF, up = (i0 >> 23) & 1, lit = *(uint32_t *)(target + 8 + (up ? imm : -imm));
    // w0: ldr rX,[pc,#8] (-> w4)  w1: orig insn1  w2: ldr pc,[pc,#-4] (-> w3)  w3: target+8  w4: literal
    uint32_t t[5] = { (i0 & 0xFFFFF000) | 0x008 | (1u << 23), i1, 0xe51ff004, (uint32_t)(target + 8), lit };
    kuKernelCpuUnrestrictedMemcpy(at, t, sizeof t); kuKernelFlushCaches(at, 20); tramp_used += 20; return at;
  }
  uint32_t t[4] = { i0, i1, 0xe51ff004, (uint32_t)(target + 8) };
  tramp_used += 16;
  kuKernelCpuUnrestrictedMemcpy(at, t, sizeof t);
  kuKernelFlushCaches(at, 16);
  return at;
}
#define HOOK(off, fn, orig_var) do { uintptr_t a = game_mod.text_base + (off); orig_var = make_trampoline(a); if (orig_var) hook_addr(a, (uintptr_t)fn); debugPrintf("hook " #fn " @%p tramp=%p\n", (void *)a, orig_var); } while (0)

// AssetStream::Loader::TranslateStream(parent, asset, stream, flag) — an asset released while still queued reaches
// here with a dead (zero) type pointer and crashes the translator thread. Report it failed (3) instead.
static int (*orig_TranslateStream)(void *, void *, void *, int);
// Idempotence guard: the translator thread can be woken twice for one queue head (removal is done by another thread).
// Remember assets we already translated; on a repeat, skip the work and swallow the extra Release that would free it.
#define DONE_N 64
static struct { void *asset; const char *name; } done[DONE_N]; static int done_i;
static void forget_done(void *asset) { for (int i = 0; i < DONE_N; i++) if (done[i].asset == asset) done[i].asset = NULL; }
static int already_done(void *asset, const char *nm) {
  for (int i = 0; i < DONE_N; i++) if (done[i].asset == asset && done[i].name == nm) return 1;
  return 0;
}
static void (*orig_AssetRelease)(void *, int, int);
static void hook_AssetRelease(void *asset, int b, int state) {
  { static int n; if (n++ < 12) debugPrintf("Release(%p, %d, %d) refs=%u from game+0x%X\n", asset, b, state, (((uint32_t *)asset)[8]) >> 2, (unsigned)((uintptr_t)__builtin_return_address(0) - game_mod.text_base)); }
  orig_AssetRelease(asset, b, state);
}
static int hook_TranslateStream(void *parent, void *asset, void *stream, int flag) {
  if (asset && *(uint32_t *)asset) {
    const char *nm = (const char *)((uint32_t *)asset)[6];
    if (already_done(asset, nm)) {
      // Duplicate wake for the same queue head. TranslateStream must still run (it releases the loader lock on exit),
      // so let it; just add one reference (bits 2..31 of word +0x20) so the follow-up Release cannot free the asset.
      uint32_t *rc = &((uint32_t *)asset)[8]; *rc += 4;
      static int n; if (n++ < 10) debugPrintf("TranslateStream: duplicate for %s — re-running with extra ref (refs now %u)\n", nm ? nm : "?", (*rc) >> 2);
    }
  }
  if (!asset || !*(uint32_t *)asset) {
    uint32_t *w = asset;
    debugPrintf("TranslateStream: dead asset %p: %08X %08X %08X %08X | %08X %08X %08X %08X\n", asset,
      w ? w[0] : 0, w ? w[1] : 0, w ? w[2] : 0, w ? w[3] : 0, w ? w[4] : 0, w ? w[5] : 0, w ? w[6] : 0, w ? w[7] : 0);
    return 3;
  }
  {
    uint32_t *w = asset; const char *nm = (const char *)w[6]; uint32_t *sw = stream;
    static int n; if (n++ < 30) debugPrintf("TranslateStream: %s asset=%p size=%u/%u streamsize=%u flag=%d\n", nm ? nm : "?", asset, w[2], w[3], sw ? sw[4] : 0, flag);
  }
  int r = orig_TranslateStream(parent, asset, stream, flag);
  { static int n2; if (n2++ < 30) debugPrintf("  -> %d\n", r); }
  if (r == 4) { done[done_i].asset = asset; done[done_i].name = (const char *)((uint32_t *)asset)[6]; done_i = (done_i + 1) % DONE_N; }
  return r;
}
// AssetStream::Loader::ChunkifyFail(asset): the loader gave up reading this asset's data
static void (*orig_ChunkifyFail)(void *);
static void hook_ChunkifyFail(void *asset) {
  uint32_t *w = asset; debugPrintf("ChunkifyFail: %s asset=%p\n", w && w[6] ? (const char *)w[6] : "?", asset);
  orig_ChunkifyFail(asset);
}
// AssetStream::Asset::~Asset() (two variants) — log who destroys assets during the first frames, to find the
// premature release that leaves a dead asset in the translator queue.
static void (*orig_AssetDtor1)(void *); static void (*orig_AssetDtor2)(void *);
static void forget_done(void *asset);
static int dtor_logged;
static void log_dtor(void *asset, void *ret) {
  if (dtor_logged++ < 40) {
    uint32_t *w = asset;
    debugPrintf("~Asset(%p) from game+0x%X size=%u\n", asset, (unsigned)((uintptr_t)ret - game_mod.text_base), w[2]);
    // poor man's backtrace: every word on the stack above us that points into game code
    uint32_t *sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
    char line[400] = "   stack:"; int n = 0;
    for (int i = 0; i < 256 && n < 16; i++) {
      uint32_t v = sp[i];
      if (v >= game_mod.text_base && v < game_mod.text_base + 0x166D000) { char b[16]; snprintf(b, sizeof b, " +0x%X", (unsigned)(v - game_mod.text_base)); strcat(line, b); n++; }
    }
    debugPrintf("%s\n", line);
  }
}
static void hook_AssetDtor1(void *asset) { log_dtor(asset, __builtin_return_address(0)); forget_done(asset); orig_AssetDtor1(asset); }
static void hook_AssetDtor2(void *asset) { log_dtor(asset, __builtin_return_address(0)); forget_done(asset); orig_AssetDtor2(asset); }

// ---- watchdog: every 4s, log what every registered thread is doing (status / wait type) ----
typedef struct { SceUID uid; char name[32]; } thread_rec;
extern thread_rec thread_registry[64]; extern int thread_registry_n;
void thread_registry_add(SceUID uid, const char *nm);
static const char *wait_name(int wt) {
  switch (wt) { case 1: return "sleep"; case 2: return "delay"; case 3: return "sema"; case 4: return "eventflag"; case 5: return "mutex"; case 6: return "cond"; case 8: return "lwmutex"; case 10: return "lwcond"; case 12: return "msgpipe"; case 13: return "thread-end"; default: return "?"; }
}
static int watchdog_fn(SceSize args, void *argp) {
  for (int tick = 0;; tick++) {
    sceKernelDelayThread(4 * 1000 * 1000);
    char line[900] = "WATCHDOG:"; int n = 0;
    for (int i = 0; i < thread_registry_n && n < 20; i++) {
      SceKernelThreadInfo ti = { .size = sizeof ti };
      if (sceKernelGetThreadInfo(thread_registry[i].uid, &ti) < 0) continue;
      char b[64]; snprintf(b, sizeof b, " [%s:%s%s]", thread_registry[i].name, ti.status == SCE_THREAD_RUNNING ? "run" : ti.status == SCE_THREAD_READY ? "ready" : ti.status == SCE_THREAD_WAITING ? "wait" : ti.status == SCE_THREAD_DORMANT ? "dormant" : "?", ti.status == SCE_THREAD_WAITING ? wait_name(ti.waitType) : "");
      strncat(line, b, sizeof line - strlen(line) - 1); n++;
    }
    debugPrintf("%s\n", line);
  }
  return 0;
}
static void start_watchdog(void) {
  thread_registry_add(sceKernelGetThreadId(), "main");
  SceUID t = sceKernelCreateThread("watchdog", watchdog_fn, 0x10000100, 0x4000, 0, 0, NULL);
  if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

// ---- loader semaphore handshake trace: translator(0x134) / ack(0x124) / loader(0x19c) at gAlloc (game data +0x17ca390)
#define GALLOC (game_mod.text_base + 0x17ca390)
static const char *sem_label(void *s) {
  uintptr_t a = (uintptr_t)s;
  return a == GALLOC + 0x134 ? "TRANSLATOR" : a == GALLOC + 0x124 ? "ACK" : a == GALLOC + 0x19c ? "LOADER" : NULL;
}
static int (*orig_SemPost)(void *, int); static int (*orig_SemWait)(void *, void *);
static int hook_SemPost(void *sem, int n) {
  const char *l = sem_label(sem); int r = orig_SemPost(sem, n);
  if (l) { static int c; if (c++ < 60) debugPrintf("sem POST %s +%d (thread %x)\n", l, n, sceKernelGetThreadId()); }
  return r;
}
static int hook_SemWait(void *sem, void *tt) {
  const char *l = sem_label(sem);
  long long rel = -1;
  if (l && tt) { struct timespec now; clock_gettime(CLOCK_REALTIME, &now); const struct timespec *d = tt; rel = ((long long)d->tv_sec - now.tv_sec) * 1000 + ((long long)d->tv_nsec - now.tv_nsec) / 1000000; }
  int r = orig_SemWait(sem, tt);
  if (l) { static int c; if (c++ < 60) debugPrintf("sem WAIT %s timeout=%lldms -> %d (thread %x)\n", l, rel, r, sceKernelGetThreadId()); }
  return r;
}

static void install_hooks(void) {
  HOOK(0x483318, hook_SemPost, orig_SemPost);
  HOOK(0x482fe0, hook_SemWait, orig_SemWait);
  HOOK(0x638408, hook_AssetDtor1, orig_AssetDtor1);
  HOOK(0x639180, hook_AssetDtor2, orig_AssetDtor2);
  HOOK(0x63ddf8, hook_TranslateStream, orig_TranslateStream);
  HOOK(0x642980, hook_ChunkifyFail, orig_ChunkifyFail);
  HOOK(0x63ae4c, hook_AssetRelease, orig_AssetRelease);
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
  vglSetShaderCachePath(DATA_PATH "/shader_cache");   // compiled shaders persist here after the first run
  sceIoMkdir(DATA_PATH "/shader_cache", 0777);
  debugPrintf("vitaGL init...\n");
  vglInitWithCustomThreshold(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_MB * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);
  debugPrintf("vitaGL ok\n");

  // 4. JNI boot sequence (order taken from Blast's MainActivity.onCreate / MainThread.run).
  //    Every entry is called with a full 8-word argument set; extras are harmless on ARM.
  jni_init();
  start_watchdog();
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
  //    Input goes through the game's own gamepad path (Xperia/console controller support) — no touch emulation.
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  fn_jni PadDown = J("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerKeyDown");
  fn_jni PadUp   = J("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerKeyUp");
  fn_jni PadAxis = J("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerMotion");
  fn_jni PadConn = J("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerConnectState");
  if (PadConn) PadConn(fake_env, NULL, 0, 1, 0,0,0,0);   // device 0 connected
  // Vita button -> Android keycode
  static const struct { unsigned vita; int key; } padmap[] = {
    { SCE_CTRL_UP, 19 }, { SCE_CTRL_DOWN, 20 }, { SCE_CTRL_LEFT, 21 }, { SCE_CTRL_RIGHT, 22 },
    { SCE_CTRL_CROSS, 96 }, { SCE_CTRL_CIRCLE, 97 }, { SCE_CTRL_SQUARE, 99 }, { SCE_CTRL_TRIANGLE, 100 },
    { SCE_CTRL_LTRIGGER, 102 }, { SCE_CTRL_RTRIGGER, 103 }, { SCE_CTRL_START, 108 }, { SCE_CTRL_SELECT, 109 },
  };
  unsigned prev_buttons = 0; float prev_axis[4] = { 0 };
  int touching = 0; unsigned frame = 0;
  for (;;) {
    if (frame < 5 || frame % 300 == 0) { SceKernelFreeMemorySizeInfo fi = { .size = sizeof fi }; sceKernelGetFreeMemorySize(&fi); debugPrintf("frame %u begin (free user=%dKB cdram=%dKB)\n", frame, fi.size_user / 1024, fi.size_cdram / 1024); }
    frame++;
    SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1);
    if (PadDown && PadUp) {
      unsigned changed = pad.buttons ^ prev_buttons;
      for (unsigned i = 0; i < sizeof padmap / sizeof padmap[0]; i++)
        if (changed & padmap[i].vita) (pad.buttons & padmap[i].vita ? PadDown : PadUp)(fake_env, NULL, 0, padmap[i].key, 0,0,0,0);
      prev_buttons = pad.buttons;
    }
    if (PadAxis) {   // Android axes: 0=X 1=Y 11=Z 14=RZ ; value -1..1
      float ax[4] = { (pad.lx - 128) / 128.0f, (pad.ly - 128) / 128.0f, (pad.rx - 128) / 128.0f, (pad.ry - 128) / 128.0f };
      static const int axis_id[4] = { 0, 1, 11, 14 };
      for (int i = 0; i < 4; i++) { if (ax[i] > -0.08f && ax[i] < 0.08f) ax[i] = 0; if (ax[i] != prev_axis[i]) { union { float f; uintptr_t u; } v = { ax[i] }; PadAxis(fake_env, NULL, 0, axis_id[i], v.u, 0,0,0); prev_axis[i] = ax[i]; } }
    }
    SceTouchData td; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (Touch) {
      if (td.reportNum > 0) { float x = td.report[0].x / 1920.0f * SCREEN_W, y = td.report[0].y / 1088.0f * SCREEN_H;
        Touch(fake_env, NULL, 0, touching ? 2 /*MOVE*/ : 0 /*DOWN*/, x, y); touching = 1; }
      else if (touching) { Touch(fake_env, NULL, 0, 1 /*UP*/, 0, 0); touching = 0; }
    }
    DrawFrame(fake_env, NULL, 0,0,0,0,0,0);
    if (frame <= 5) { GLenum e = glGetError(); if (e) debugPrintf("  glError 0x%X after frame %u\n", e, frame - 1); }
    vglSwapBuffers(GL_FALSE);
    if (frame <= 5) debugPrintf("  frame %u swapped\n", frame - 1);
  }
  return 0;
}
