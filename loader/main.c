// SSX (Android, com.ea.ssx v0.0.7833) → PS Vita so-loader. Boot order mirrors what
// com.ea.blast.MainActivity / MainThread do on the Java side. See README for the iteration loop.
#include <vitasdk.h>
#include <psp2/power.h>
#include <psp2/motion.h>
#ifndef SSX_ACCEL   // accelerometer feed off until a race verifies axes/signs: Android axes are portrait-relative, the Vita's landscape; raw values would read as a tilt
#define SSX_ACCEL 0
#endif
#include <malloc.h>
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
#include "audio.h"
#include "stubs.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define LOAD_ADDR_GAME   0x94000000
#define LOAD_ADDR_GNUSTL 0x9C000000   // libgame.so is ~40MB mapped; keep gnustl clear of it
#define MEMORY_NEWLIB_MB 294   // +22MB for GameWorld::SlotAlloc 26M (Alps race asks 24.6MB in one block); VitaGL RAM pool ~13MB (uses ~2MB)
#define MEMORY_VITAGL_MB 16          // tune: game streams textures from .big caches

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;
unsigned int sceLibcHeapSize = 8 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;
extern so_default_dynlib default_dynlib[]; extern int default_dynlib_size;

static so_module game_mod, gnustl_mod;
#define GALLOC (game_mod.text_base + 0x17ca390)   // AssetStream::gAlloc

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
  uint32_t ins[2] = { *(uint32_t *)target, *(uint32_t *)(target + 4) }, lit[2] = { 0, 0 }; int anylit = 0;
  uint8_t *at = tramp_pool + tramp_used;
  for (int k = 0; k < 2; k++) {
    uint32_t w = ins[k];
    if ((w & 0x0F7F0000) == 0x051F0000) {            // ldr rX, [pc, #imm] -> ldr rX, [pc, #8]: literal sits 4 words after the instruction
      uint32_t imm = w & 0xFFF, up = (w >> 23) & 1; lit[k] = *(uint32_t *)(target + 4 * k + 8 + (up ? imm : -imm));
      ins[k] = (w & 0xFFFFF000) | 0x008 | (1u << 23); anylit = 1;
    }
  }
  // w0: insn0  w1: insn1  w2: ldr pc,[pc,#-4]  w3: target+8  w4: lit0  w5: lit1
  uint32_t t[6] = { ins[0], ins[1], 0xe51ff004, (uint32_t)(target + 8), lit[0], lit[1] };
  int n = anylit ? 24 : 16;
  kuKernelCpuUnrestrictedMemcpy(at, t, n); kuKernelFlushCaches(at, n); tramp_used += n;
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
  const char *nm = (asset && *(uint32_t *)asset) ? (const char *)((uint32_t *)asset)[6] : NULL;
  if (asset && *(uint32_t *)asset) {
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
  // Large files arrive as several chunks (256KB..512KB each). The translator reads them in sequence from the translate
  // vector, so every chunk of this asset must be present before translation starts. Wait until the sum of the chunk
  // sizes queued for this asset reaches the file size (chunk[6] = total), up to 20s.
  {
    uint32_t total = 0, have = 0, chunks = 0; int waited = 0;
    for (;;) {
      have = 0; chunks = 0; total = 0;
      uint32_t *vec = *(uint32_t **)(GALLOC + 0x30);
      if (vec) { uint32_t *b = (uint32_t *)vec[0], *e = (uint32_t *)vec[1];
        for (uint32_t *p = b; p < e; p++) { uint32_t *c = (uint32_t *)*p; if (c && (void *)c[3] == asset) { have += c[5]; chunks++; if (c[6] > total) total = c[6]; } } }
      if (!chunks || have >= total || have >= 1024 * 1024 || waited >= 4000) break;   // engine streams >1MB files through a 1MB window; don't fight it
      sceKernelDelayThread(500); waited++;
    }
    static int n; if (n++ < 60) debugPrintf("chunks for %s: %u of %u bytes in %u chunk(s) (waited %d ms)\n", nm ? nm : "?", have, total, chunks, waited / 2);
  }
  int r = orig_TranslateStream(parent, asset, stream, flag);
  { static int n2; if (n2++ < 30) debugPrintf("  -> %d\n", r); }
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
static const char *sem_label(void *s) {
  uintptr_t a = (uintptr_t)s;
  return a == GALLOC + 0x134 ? "TRANSLATOR" : a == GALLOC + 0x124 ? "ACK" : a == GALLOC + 0x19c ? "LOADER" : NULL;
}
static int (*orig_SemPost)(void *, int); static int (*orig_SemWait)(void *, void *);
static SceUID translator_tid;
static int hook_SemPost(void *sem, int n) {
  const char *l = sem_label(sem);
  // The translator thread re-posts its own semaphore after each job to look for more. If the queue head is the
  // asset it just finished (nothing popped it yet), that self-post only makes it re-translate the same shader and
  // starve the main thread of the "translated" state. Swallow it; the Unpack thread posts again for new work.
  int r = orig_SemPost(sem, n);
  if (l) { static int c; if (c++ < 400) debugPrintf("sem POST %s +%d (thread %x)\n", l, n, sceKernelGetThreadId()); }
  return r;
}
static int hook_SemWait(void *sem, void *tt) {
  const char *l = sem_label(sem);
  long long rel = -1;
  if (l && tt) { struct timespec now; clock_gettime(CLOCK_REALTIME, &now); const struct timespec *d = tt; rel = ((long long)d->tv_sec - now.tv_sec) * 1000 + ((long long)d->tv_nsec - now.tv_nsec) / 1000000; }
  if ((uintptr_t)sem == GALLOC + 0x134) translator_tid = sceKernelGetThreadId();
  int r = orig_SemWait(sem, tt);
  if ((uintptr_t)sem == GALLOC + 0x134) {
    // dump the loader's request vectors
    static int c;
    if (c++ < 200) {
      char line[300] = "vectors:";
      const unsigned offs[4] = { 0x2c, 0x30, 0x118, 0x11c };
      for (int i = 0; i < 4; i++) {
        uint32_t *v = *(uint32_t **)(GALLOC + offs[i]); char b[80];
        if (!v) { snprintf(b, sizeof b, " [+%X null]", offs[i]); }
        else { uint32_t *bg = (uint32_t *)v[0], *en = (uint32_t *)v[1]; int n = (int)(en - bg);
          snprintf(b, sizeof b, " [+%X n=%d e0=%p]", offs[i], n, n > 0 ? (void *)bg[0] : NULL); }
        strncat(line, b, sizeof line - strlen(line) - 1);
      }
      debugPrintf("%s\n", line);
    }
  }
  if (l) { static int c; if (c++ < 400) debugPrintf("sem WAIT %s timeout=%lldms -> %d (thread %x)\n", l, rel, r, sceKernelGetThreadId()); }
  return r;
}

// ---- producer-stage tracing: LoadingUpdate / UnpackUpdate entry and exit with mailbox states ----
static void mailbox_state(char *out, size_t n) {
  const unsigned offs[4] = { 0x2c, 0x30, 0x118, 0x11c }; out[0] = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t *v = *(uint32_t **)(GALLOC + offs[i]); char b[64];
    if (!v) snprintf(b, sizeof b, " [+%X null]", offs[i]);
    else { uint32_t *bg = (uint32_t *)v[0], *en = (uint32_t *)v[1]; int cnt = (int)(en - bg); snprintf(b, sizeof b, " [+%X n=%d e0=%p]", offs[i], cnt, bg ? (void *)bg[0] : NULL); }
    strncat(out, b, n - strlen(out) - 1);
  }
}
static void (*orig_LoadingUpdate)(void); static void (*orig_UnpackUpdate)(void);
static int stage_log;
static void hook_LoadingUpdate(void) {
  char s[300]; if (stage_log < 200) { mailbox_state(s, sizeof s); debugPrintf("LoadingUpdate in %s\n", s); }
  orig_LoadingUpdate();
  if (stage_log++ < 200) { mailbox_state(s, sizeof s); debugPrintf("LoadingUpdate out%s\n", s); }
}
static void hook_UnpackUpdate(void) {
  char s[300]; if (stage_log < 200) { mailbox_state(s, sizeof s); debugPrintf("UnpackUpdate in %s\n", s); }
  orig_UnpackUpdate();
  if (stage_log++ < 200) { mailbox_state(s, sizeof s); debugPrintf("UnpackUpdate out%s\n", s); }
}

// MemoryFramework::ErrorHandlers::OutOfMemory(const MemoryFailure&): log what ran out before the engine stops
static void (*orig_OutOfMemory)(void *);
static void hook_OutOfMemory(void *mf) {
  uint32_t *w = mf;
  debugPrintf("OUT OF MEMORY: words %08X %08X %08X %08X %08X %08X %08X %08X\n", w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
  for (int i = 0; i < 8; i++) if (w[i] > 0x94000000 && w[i] < 0x95900000) debugPrintf("  str[%d]=%.60s\n", i, (const char *)w[i]);
  orig_OutOfMemory(mf);
}
// Scaleform::Render::GL::MeshBuffer::Unmap(this): log the GL-table object and the vtable slot it is about to call
static void (*orig_SfUnmap)(void *);
static void hook_SfUnmap(void *self) {
  uint32_t *s = self;
  uint32_t hal = s[7];                                    // this+0x1c
  uint32_t obj = hal ? *(uint32_t *)(hal + 0x1eb30) : 0;  // [hal+0x1eb30]
  uint32_t vt = obj ? *(uint32_t *)obj : 0, fn = vt ? *(uint32_t *)(vt + 0x3c) : 0;
  static int n; if (n++ < 8) debugPrintf("SfUnmap: this=%p flags=%08X size=%u data=%08X buf=%u hal=%08X obj=%08X vt=%08X slot15=%08X (game+0x%X)\n",
    self, s[1], s[5], s[6], s[10], hal, obj, vt, fn, fn >= game_mod.text_base ? (unsigned)(fn - game_mod.text_base) : 0);
  orig_SfUnmap(self);
}
// FMTutorialManager::FETutorialCheck(this, key&, bool) -> tutorial attrib instance or 0. The front-end tutorial popups
// ("Tutorial: World Tour") render with their NEXT/BACK buttons off-screen on the Vita and cannot be dismissed, so the
// globe is unusable. Report "no tutorial for this key": RequestHintScreenInBackEnd then returns without showing one.
static void *(*orig_FETutorialCheck)(void *, void *, int);
static void *hook_FETutorialCheck(void *self, void *key, int b) {
  static int n; if (n++ < 20) debugPrintf("FETutorialCheck(key %08X) suppressed\n", key ? *(uint32_t *)key : 0);
  return NULL;
}
// ---- MemoryFramework category discovery + per-category stats (name lives at category+0x1E0) ----
// Only the two Category::Alloc entry points are hooked, and only to learn which category objects exist; the numbers come from
// the game's own Category::GetStatistics. (Per-alloc/free accounting cost a trampoline + table scan on every allocation and
// raced across threads.) Discovery is lock-protected and rare; the fast path is a lock-free check of the last-seen category.
#define MEMCAT_MAX 64
static void *memcat[MEMCAT_MAX]; static volatile int memcat_n; static void *volatile memcat_last;
static SceKernelLwMutexWork memcat_lock; static int memcat_lock_init;
static void memcat_seen(void *cat) {
  if (!cat || cat == memcat_last) return;
  int n = memcat_n; for (int i = 0; i < n; i++) if (memcat[i] == cat) { memcat_last = cat; return; }
  if (!memcat_lock_init) return;
  sceKernelLockLwMutex(&memcat_lock, 1, NULL);
  int i; for (i = 0; i < memcat_n; i++) if (memcat[i] == cat) break;
  if (i == memcat_n && memcat_n < MEMCAT_MAX) { memcat[memcat_n] = cat; memcat_n = memcat_n + 1; }
  sceKernelUnlockLwMutex(&memcat_lock, 1);
  memcat_last = cat;
}
static void (*cat_get_stats)(void *out, void *cat, int refresh);   // MemoryFramework::Category::GetStatistics(bool): sret 7 words summed over the category's allocators
void memcat_report(void) {
  int n = memcat_n;
  for (int i = 0; i < n; i++) {
    const char *nm = (const char *)memcat[i] + 0x1E0; if (!*nm) nm = "?";
    unsigned st[7] = { 0 }; if (cat_get_stats) cat_get_stats(st, memcat[i], 1);
    debugPrintf("memstat %-24s stats(KB) %u %u %u %u %u %u %u\n", nm, st[0] / 1024, st[1] / 1024, st[2] / 1024, st[3] / 1024, st[4] / 1024, st[5] / 1024, st[6] / 1024);
  }
}
static void *(*orig_CatAlloc5)(void *, unsigned, const char *, unsigned, unsigned, unsigned);
// Big-allocation tracer for the categories that run dry: who asks Render/Audio::Data/GameWorld for >=128KB, and for what.
static void memcat_trace(void *cat, unsigned size, const char *name, void *ra) {
  static int n; if (size < 128 * 1024 || n >= 600) return;
  const char *cn = (const char *)cat + 0x1E0;
  if (strcmp(cn, "Render") && strcmp(cn, "Audio::Data") && strncmp(cn, "GameWorld", 9) && strncmp(cn, "GP::", 4)) return;
  n++; debugPrintf("memalloc %s %uKB '%s' from game+0x%X\n", cn, size / 1024, name ? name : "", (unsigned)((uintptr_t)ra - game_mod.text_base));
}
static void *hook_CatAlloc5(void *cat, unsigned size, const char *name, unsigned a, unsigned b, unsigned c) { memcat_seen(cat); memcat_trace(cat, size, name, __builtin_return_address(0)); return orig_CatAlloc5(cat, size, name, a, b, c); }
static void *(*orig_CatAlloc2)(void *, unsigned, const char *);
static void *hook_CatAlloc2(void *cat, unsigned size, const char *name) { memcat_seen(cat); memcat_trace(cat, size, name, __builtin_return_address(0)); return orig_CatAlloc2(cat, size, name); }
// RNA::TexturePlatC keeps a CPU copy of every texture's pixels (this+0x40, allocated from the Render pool; 30MB+ for
// one track load) so Android can re-upload after an EGL context loss. The Vita never loses its GL context, so once an
// asset texture has been uploaded by LoadTextureCommon -> Flush() the copy is dead weight: free it. Dynamic/lockable
// textures (created empty, written through AcquireSurface) are flushed from other callers and keep their copy.
// Destroy() already tolerates a NULL copy. Allocator: *(g_rna_alloc_base + 0x6C), ICoreAllocator vtable slot 4 = Free(ptr, size).
#define RNA_ALLOC_GLOBAL 0x17d0970
#define LOADTEX_START 0xd40900
#define LOADTEX_END   0xd41800
static void (*orig_TexFlush)(void *);
static unsigned tex_copies_freed, tex_copy_bytes_freed;
static void hook_TexFlush(void *tex) {
  orig_TexFlush(tex);
  uintptr_t ra = (uintptr_t)__builtin_return_address(0) - game_mod.text_base;
  if (ra < LOADTEX_START || ra >= LOADTEX_END) return;
  char **slot = (char **)((char *)tex + 0x40); char *buf = *slot;
  if (!buf || !*(uint32_t *)((char *)tex + 0x3c)) return;   // nothing to free / GPU texture not created
  void *alloc = *(void **)(game_mod.text_base + RNA_ALLOC_GLOBAL + 0x6c); if (!alloc) return;
  unsigned size = *(uint32_t *)(buf - 0x10);
  void (*Free)(void *, void *, unsigned) = (void (*)(void *, void *, unsigned))(*(uintptr_t **)alloc)[4];
  *slot = NULL; Free(alloc, buf - 0x10, 0);
  tex_copies_freed++; tex_copy_bytes_freed += size;
  if (tex_copies_freed <= 10 || tex_copies_freed % 50 == 0) debugPrintf("texture CPU copy freed after upload: %uKB (total %u copies, %uKB)\n", size / 1024, tex_copies_freed, tex_copy_bytes_freed / 1024);
}
static void install_hooks(void) {
  HOOK(0xd929e8, hook_TexFlush, orig_TexFlush);
  sceKernelCreateLwMutex(&memcat_lock, "memcat", 0, 0, NULL); memcat_lock_init = 1;
  cat_get_stats = (void (*)(void *, void *, int))(game_mod.text_base + 0x14f2e0);
  HOOK(0x14ef4c, hook_CatAlloc5, orig_CatAlloc5);
  HOOK(0x14ee2c, hook_CatAlloc2, orig_CatAlloc2);
  HOOK(0x1b7968, hook_FETutorialCheck, orig_FETutorialCheck);
  HOOK(0xa3b18c, hook_SfUnmap, orig_SfUnmap);
  HOOK(0x14b888, hook_OutOfMemory, orig_OutOfMemory);
  HOOK(0x6438c0, hook_LoadingUpdate, orig_LoadingUpdate);
  HOOK(0x642b78, hook_UnpackUpdate, orig_UnpackUpdate);
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
  // keep the previous run's log: a relaunch after a crash no longer wipes the evidence
  sceIoRemove(DATA_PATH "/ssx_prev.log"); sceIoRename(DATA_PATH "/ssx.log", DATA_PATH "/ssx_prev.log"); sceIoRemove("ux0:data/ssx.log");
  { // The 25MB ghosts save is legit (Global pool is 45M to load it); only drop saves corrupted to absurd sizes. Loading one OOMs
    // the Global pool. Drop any save file over 32MB; the game recreates them.
    SceUID d = sceIoDopen(DATA_PATH "/internal/ssxv"); SceIoDirent e;
    while (d >= 0 && sceIoDread(d, &e) > 0) {
      if (SCE_S_ISDIR(e.d_stat.st_mode) || e.d_stat.st_size <= 32 * 1024 * 1024) continue;
      char p[512]; snprintf(p, sizeof p, DATA_PATH "/internal/ssxv/%s", e.d_name); sceIoRemove(p);
      debugPrintf("removed oversized save %s (%lld bytes)\n", e.d_name, (long long)e.d_stat.st_size);
    }
    if (d >= 0) sceIoDclose(d);
  }
#ifndef BUILD_ID
#define BUILD_ID "dev"
#endif
  debugPrintf("=== SSX Vita boot (build " BUILD_ID ") ===\n");

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
  scePowerSetArmClockFrequency(444); scePowerSetBusClockFrequency(222); scePowerSetGpuClockFrequency(222); scePowerSetGpuXbarClockFrequency(166);
  debugPrintf("clocks: arm %d bus %d gpu %d xbar %d\n", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
  debugPrintf("vitaGL init...\n");
  vglInitWithCustomThreshold(0, SCREEN_W, SCREEN_H, 22 * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);   // leave 22MB outside VitaGL: thread stacks (3x2MB + 8x512KB) and misc; 32MB left ~17MB unused (free user)
  debugPrintf("vitaGL ok: pools vram %uKB ram %uKB phycont %uKB budget %uKB\n", (unsigned)vglMemFree(VGL_MEM_VRAM)/1024, (unsigned)vglMemFree(VGL_MEM_RAM)/1024, (unsigned)vglMemFree(VGL_MEM_PHYCONT)/1024, (unsigned)vglMemFree(VGL_MEM_BUDGET)/1024);
  glViewport(0, 0, SCREEN_W, SCREEN_H);   // vitaGL's default viewport is 0x0 and the game never sets one; must come AFTER vglInit

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
  audio_bridge_install(&game_mod);     // SubmitAudio -> SceAudioOut ring (replaces the AudioTrack JNI path)

  uintptr_t sInternal = (uintptr_t)jni_new_string("/data/ssx/internal");   // Android-style absolute paths; fix_path() maps them to the card
  uintptr_t sExternal = (uintptr_t)jni_new_string("/data/ssx/external");
  uintptr_t sObb      = (uintptr_t)jni_new_string("/data/ssx/obb");
  uintptr_t am        = (uintptr_t)fake_asset_manager;
  sceIoMkdir(DATA_PATH "/external", 0777); sceIoMkdir(DATA_PATH "/internal/ssxv", 0777);

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
  // SSX's own Activity fires these in addition to the Blast ones; without ContextReset the renderer treats the GL context as lost
  { fn_jni f;
    if ((f = J("Java_com_ea_ssx_MainActivity_NativeOnContextResetEvent"))) { f(fake_env, (void *)&fake_activity, 0,0,0,0,0,0); debugPrintf("NativeOnContextResetEvent ok\n"); }
    if ((f = J("Java_com_ea_ssx_MainActivity_NativeOnResumeEvent")))       { f(fake_env, (void *)&fake_activity, 0,0,0,0,0,0); debugPrintf("NativeOnResumeEvent ok\n"); }
    if ((f = J("Java_com_ea_ssx_MainActivity_ResumeAudio")))               { f(fake_env, (void *)&fake_activity, 0,0,0,0,0,0); debugPrintf("ResumeAudio ok\n"); }
    if ((f = J("Java_com_ea_ssx_MainActivity_ResumeVideo")))               { f(fake_env, (void *)&fake_activity, 0,0,0,0,0,0); debugPrintf("ResumeVideo ok\n"); }
  }
  fn_jni DrawFrame = J("Java_com_ea_blast_MainThread_NativeOnDrawFrame");
  fn_jni Touch = J("Java_com_ea_blast_TouchSurfaceAndroid_NativeOnPointerEvent");
  // Decoded from TouchScreen::HandleMessage: (action, tag=1000 raw pointer, pointerId, x as float bits, y as float bits)
  #define TOUCH_DOWN 0x6000E
  #define TOUCH_MOVE 0x4000E
  #define TOUCH_UP   0x8000E
  #define FBITS(f) ({ union { float v; uintptr_t u; } _c = { (f) }; _c.u; })
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
  extern int jni_video_play_pending; int video_done_at = 0;
  fn_jni VideoDone = J("Java_com_ea_VideoPlayer_PlayerAndroid_OnCompletionNativeImpl");
  typedef void (*fn_jni7)(void *env, void *thiz, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
  fn_jni7 Mouse = (fn_jni7)J("Java_com_ea_blast_MouseAndroid_NativeOnMouseEvent");   // (env, thiz, id, button, c, d, wheel, float x, float y); ids Down 0x2000B Move 0xB Up 0x4000B
  #define MOUSE_DOWN 0x2000B
  #define MOUSE_MOVE 0xB
  #define MOUSE_UP   0x4000B
  fn_jni BackKey = J("Java_com_ea_ssx_MainActivity_OnBackSoftKey");               // Android BACK: dismisses popups/hint panels
  fn_jni Accel = J("Java_com_ea_blast_AccelerometerAndroidDelegate_NativeOnAcceleration");   // (env, thiz, float x, float y, float z)
  { static int logged; if (!logged++) debugPrintf("accel: %s\n", SSX_ACCEL ? "on" : "off (build with -DSSX_ACCEL=1)"); }
  sceMotionStartSampling();
  for (;;) {
    if (frame < 5 || frame % 100 == 0) { SceKernelFreeMemorySizeInfo fi = { .size = sizeof fi }; sceKernelGetFreeMemorySize(&fi); struct mallinfo mi = mallinfo(); void *probe = malloc(1 << 20); debugPrintf("frame %u begin (free user=%dKB cdram=%dKB) newlib heap: used %uKB free %uKB arena %uKB 1MB-probe %s\n", frame, fi.size_user / 1024, fi.size_cdram / 1024, (unsigned)mi.uordblks / 1024, (unsigned)mi.fordblks / 1024, (unsigned)mi.arena / 1024, probe ? "ok" : "FAILED"); free(probe);
      if (frame % 300 == 0) memcat_report();
      { extern unsigned jni_n_str, jni_n_arr, jni_b_arr, jni_n_del; debugPrintf("JNI objects so far: strings %u arrays %u (%uKB) DeleteLocalRef %u\n", jni_n_str, jni_n_arr, jni_b_arr / 1024, jni_n_del); } }
    frame++;
    SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1);
    if (PadDown && PadUp) {
      unsigned changed = pad.buttons ^ prev_buttons;
      for (unsigned i = 0; i < sizeof padmap / sizeof padmap[0]; i++)
        if (changed & padmap[i].vita) {
          int down = (pad.buttons & padmap[i].vita) != 0;
          { static int n; if (n++ < 300) debugPrintf("input: key %d %s (frame %u)\n", padmap[i].key, down ? "down" : "up", frame); }
          (down ? PadDown : PadUp)(fake_env, NULL, 0, padmap[i].key, 0,0,0,0);
          if (down && padmap[i].vita == SCE_CTRL_SELECT && BackKey) { BackKey(fake_env, (void *)&fake_activity, 0,0,0,0,0,0); debugPrintf("input: OnBackSoftKey\n"); }
        }
      prev_buttons = pad.buttons;
    }
    if (PadAxis) {   // Android axes: 0=X 1=Y 11=Z 14=RZ ; value -1..1
      float ax[4] = { (pad.lx - 128) / 128.0f, (pad.ly - 128) / 128.0f, (pad.rx - 128) / 128.0f, (pad.ry - 128) / 128.0f };
      static const int axis_id[4] = { 0, 1, 11, 14 };
      for (int i = 0; i < 4; i++) { if (ax[i] > -0.08f && ax[i] < 0.08f) ax[i] = 0; if (ax[i] != prev_axis[i]) { union { float f; uintptr_t u; } v = { ax[i] }; PadAxis(fake_env, NULL, 0, axis_id[i], v.u, 0,0,0); prev_axis[i] = ax[i]; } }
    }
    SceTouchData td; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (Touch) {   // up to 2 fingers (pinch/zoom hints); Android pointer id = slot index
      static int on[2]; static float lx[2], ly[2]; static int vid[2]; int seen[2] = { 0, 0 };
      for (int r = 0; r < td.reportNum && r < 4; r++) {
        float x = td.report[r].x / 1920.0f * SCREEN_W, y = td.report[r].y / 1088.0f * SCREEN_H; int slot = -1;
        for (int s = 0; s < 2; s++) if (on[s] && vid[s] == td.report[r].id) slot = s;
        if (slot < 0) for (int s = 0; s < 2; s++) if (!on[s] && !seen[s]) { slot = s; break; }
        if (slot < 0) continue;
        seen[slot] = 1;
        if (!on[slot]) { static int n; if (n++ < 300) debugPrintf("input: touch%d down %.0f,%.0f (frame %u)\n", slot, x, y, frame); }
        Touch(fake_env, NULL, on[slot] ? TOUCH_MOVE : TOUCH_DOWN, 1000, slot, FBITS(x), FBITS(y), 0);
        if (slot == 0 && Mouse) Mouse(fake_env, NULL, on[0] ? MOUSE_MOVE : MOUSE_DOWN, 1, 0, 0, 0, FBITS(x), FBITS(y));   // Blast's Java layer mirrors finger 0 as a left-button mouse (Scaleform UI listens to mouse)
        on[slot] = 1; vid[slot] = td.report[r].id; lx[slot] = x; ly[slot] = y;
      }
      for (int s = 0; s < 2; s++) if (on[s] && !seen[s]) {
        { static int n; if (n++ < 300) debugPrintf("input: touch%d up %.0f,%.0f (frame %u)\n", s, lx[s], ly[s], frame); }
        Touch(fake_env, NULL, TOUCH_UP, 1000, s, FBITS(lx[s]), FBITS(ly[s]), 0); on[s] = 0;
        if (s == 0 && Mouse) Mouse(fake_env, NULL, MOUSE_UP, 1, 0, 0, 0, FBITS(lx[0]), FBITS(ly[0]));
      }
    }
    if (SSX_ACCEL && Accel && (frame & 1)) {   // ~30Hz accelerometer from the Vita's motion sensor, in m/s^2 like Android
      SceMotionState ms; if (sceMotionGetState(&ms) >= 0) {
        union { float f; uintptr_t u; } ax = { ms.acceleration.x * 9.81f }, ay = { ms.acceleration.y * 9.81f }, az = { ms.acceleration.z * 9.81f };
        Accel(fake_env, NULL, ax.u, ay.u, az.u, 0, 0, 0);
        { static int n; if (n++ < 3) debugPrintf("accel: %.2f %.2f %.2f\n", ax.f, ay.f, az.f); }
      }
    }
    if (jni_video_play_pending && !video_done_at) video_done_at = frame + 30;   // let the player register, then "finish" the movie
    if (video_done_at && frame >= (unsigned)video_done_at) {
      video_done_at = 0; jni_video_play_pending = 0;
      if (VideoDone) { for (int id = 0; id < 4; id++) VideoDone(fake_env, NULL, id, 0,0,0,0,0); debugPrintf("video: reported completion\n"); }
    }
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    DrawFrame(fake_env, NULL, 0,0,0,0,0,0);
    SceUInt64 t1 = sceKernelGetProcessTimeWide();
    if (frame <= 5) { GLenum e = glGetError(); if (e) debugPrintf("  glError 0x%X after frame %u\n", e, frame - 1); }
    extern void gl_lock_acquire(void), gl_lock_release(void);
    gl_lock_acquire(); vglSwapBuffers(GL_FALSE); gl_lock_release();
    { // frame timing: per 100 frames, avg/max of game update+render (DrawFrame) and of the swap, plus frames over 33ms
      static SceUInt64 last, sum_d, sum_s, max_d, max_s, max_f; static unsigned slow, n;
      SceUInt64 t2 = sceKernelGetProcessTimeWide(), d = t1 - t0, sw = t2 - t1, fr = last ? t2 - last : 0; last = t2;
      sum_d += d; sum_s += sw; if (d > max_d) max_d = d; if (sw > max_s) max_s = sw; if (fr > max_f) max_f = fr; if (fr > 33333) slow++; n++;
      if (frame % 100 == 0) { debugPrintf("timing: frame %u avg %.1fms (draw %.1f max %.1f, swap %.1f max %.1f) worst frame %.1fms, %u/%u frames >33ms\n", frame,
          (sum_d + sum_s) / 1000.0 / n, sum_d / 1000.0 / n, max_d / 1000.0, sum_s / 1000.0 / n, max_s / 1000.0, max_f / 1000.0, slow, n);
        sum_d = sum_s = max_d = max_s = max_f = 0; slow = n = 0; }
    }
    { extern void gl_frame_summary(unsigned); gl_frame_summary(frame - 1); }
    if (frame <= 5) debugPrintf("  frame %u swapped\n", frame - 1);
  }
  return 0;
}
