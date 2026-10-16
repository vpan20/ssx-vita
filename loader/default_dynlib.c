// AUTO-GENERATED from libgame.so undefined imports (432). Edit stubs.c for overrides.
#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <wchar.h>
#include <strings.h>
#include <malloc.h>
#include <locale.h>
#include <sched.h>
#include <wctype.h>
#include <signal.h>
#include "so_util.h"
#include "stubs.h"
// clock_gettime: every clock id reports the same wall clock vitasdk's sem_timedwait/pthread_cond_timedwait compare
// against (soloader convention). Otherwise CLOCK_MONOTONIC-based deadlines are misread and every timed wait expires at once.
int clock_gettime_rt(int clk, struct timespec *t) { return clock_gettime(CLOCK_REALTIME, t); }   // exactly what pthread-embedded's timeout conversion (ftime) uses
// usleep/nanosleep with a floor: the game uses tiny sleeps as yields; on Vita a 0us delay never yields
// Frame-pacing diagnosis: time spent sleeping / in timed waits on the main (DrawFrame) thread, read per frame by main.c.
// Hypothesis under test: the game self-limits to ~33ms inside NativeOnDrawFrame with a sleep, fighting our vsync.
// SSX_SKIP_MAIN_SLEEP=1 turns main-thread sleeps >= 2ms into a short yield (the fix if the hypothesis holds); default off.
#ifndef SSX_SKIP_MAIN_SLEEP
#define SSX_SKIP_MAIN_SLEEP 0
#endif
int stats_main_tid = -1;
volatile unsigned long long main_sleep_us, main_wait_us; volatile unsigned main_sleep_calls, main_wait_calls;
static inline int on_main(void) { return stats_main_tid >= 0 && sceKernelGetThreadId() == stats_main_tid; }
static void delay_us(long long us) {
  if (us < 100) us = 100;
  if (!on_main()) { sceKernelDelayThread((SceUInt)us); return; }
  if (SSX_SKIP_MAIN_SLEEP && us >= 2000) us = 100;
  SceUInt64 t = sceKernelGetProcessTimeWide(); sceKernelDelayThread((SceUInt)us);
  main_sleep_us += sceKernelGetProcessTimeWide() - t; main_sleep_calls++;
}
int usleep_yield(useconds_t us) { delay_us(us); return 0; }
int nanosleep_yield(const struct timespec *req, struct timespec *rem) { long long us = req ? (long long)req->tv_sec * 1000000 + req->tv_nsec / 1000 : 0; delay_us(us); return 0; }
// Shader compile/link diagnostics: VitaGL translates GLSL through vitashark; failures are otherwise silent
static char last_src_head[400];
static char last_src_full[32768]; static int shader_fail_dumps;
// keep a copy of the (pre-processed) source per shader handle so link-time compile failures can be dumped
static struct { GLuint sh; char *src; } src_tab[64]; static int src_tab_n;
static void remember_src(GLuint sh, const char *src) {
  for (int i = 0; i < 64; i++) if (src_tab[i].sh == sh) { free(src_tab[i].src); src_tab[i].src = strdup(src); return; }
  int i = src_tab_n++ % 64; free(src_tab[i].src); src_tab[i].sh = sh; src_tab[i].src = strdup(src);
}
static const char *recall_src(GLuint sh) { for (int i = 0; i < 64; i++) if (src_tab[i].sh == sh) return src_tab[i].src; return NULL; }
static void dump_src(GLuint sh) {
  const char *src = recall_src(sh); if (!src) { debugPrintf("(no source kept for shader %u)\n", sh); return; }
  char *copy = strdup(src); debugPrintf("--- source of shader %u ---\n", sh);
  for (char *q = copy; *q; ) { char *e = strchr(q, '\n'); if (!e) { debugPrintf("%s\n", q); break; } *e = 0; debugPrintf("%s\n", q); q = e + 1; }
  debugPrintf("--- end ---\n"); free(copy);
}
// GLSL pre-pass for VitaGL's translator: it does not handle the "*=" operator (documented FIXME), so rewrite
// "lhs *= rhs;" as "lhs = lhs * rhs;". lhs = identifier with optional .swizzle / [index] chain.
static int is_id_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '[' || c == ']'; }
static char *glsl_prepass(const char *src, int len) {
  int extra = 0; for (int i = 0; i + 1 < len; i++) if (src[i] == '*' && src[i+1] == '=') extra += 64;
  char *out = malloc(len + extra + 1); int o = 0;
  for (int i = 0; i < len; ) {
    if (src[i] == '*' && i + 1 < len && src[i+1] == '=') {
      int e = o; while (e > 0 && (out[e-1] == ' ' || out[e-1] == '\t')) e--;   // end of lhs in out
      int s = e; while (s > 0 && is_id_char(out[s-1])) s--;
      int ll = e - s;
      if (ll > 0 && ll < 48) { out[o++] = '='; out[o++] = ' '; memcpy(out + o, out + s, ll); o += ll; out[o++] = ' '; out[o++] = '*'; i += 2; continue; }
    }
    out[o++] = src[i++];
  }
  out[o] = 0; return out;
}
// rename `tex` to `s_tex` in GLSL source: `tex` is a built-in function name in Cg and gets dropped
static char *rename_tex_sampler(const char *src, int len) {
  char *out = malloc(len * 2 + 64); int o = 0, i = 0;
  while (i < len) {
    if (i + 3 <= len && memcmp(src + i, "tex", 3) == 0) {
      int pre = i > 0 ? src[i-1] : 0; int post = (i+3 < len) ? src[i+3] : 0;
      int id_pre = (pre >= 'a' && pre <= 'z') || (pre >= 'A' && pre <= 'Z') || (pre >= '0' && pre <= '9') || pre == '_';
      int id_post = (post >= 'a' && post <= 'z') || (post >= 'A' && post <= 'Z') || (post >= '0' && post <= '9') || post == '_';
      if (!id_pre && !id_post) { memcpy(out + o, "s_tex", 5); o += 5; i += 3; continue; }
    }
    out[o++] = src[i++];
  }
  out[o] = 0; return out;
}
void glShaderSource_log(GLuint sh, GLsizei n, const GLchar **src, const GLint *len) {
  if (n > 0 && src && src[0]) {
    int l = len && len[0] > 0 ? len[0] : (int)strlen(src[0]);
    int h = l > 380 ? 380 : l; memcpy(last_src_head, src[0], h); last_src_head[h] = 0;
    if (n == 1) {
      char *fixed = glsl_prepass(src[0], l);
      char *final = rename_tex_sampler(fixed, strlen(fixed)); free(fixed);
      remember_src(sh, final);
      { size_t fl = strlen(final); if (fl >= sizeof last_src_full) fl = sizeof last_src_full - 1; memcpy(last_src_full, final, fl); last_src_full[fl] = 0; }
      const GLchar *one[1] = { final }; glShaderSource(sh, 1, one, NULL); free(final); return;
    }
  }
  glShaderSource(sh, n, src, len);
}
void glCompileShader_log(GLuint sh) {
  glCompileShader(sh);
  GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024] = ""; GLsizei ln = 0; glGetShaderInfoLog(sh, sizeof log, &ln, log);
    debugPrintf("SHADER COMPILE FAIL (%u): %s\n--- source head ---\n%s\n---\n", sh, log, last_src_head);
    if (shader_fail_dumps++ < 3) { debugPrintf("--- full source of failed shader %u ---\n", sh); for (char *q = last_src_full; *q; ) { char *e = strchr(q, '\n'); if (!e) { debugPrintf("%s\n", q); break; } *e = 0; debugPrintf("%s\n", q); q = e + 1; } debugPrintf("--- end ---\n"); } }
}
void glLinkProgram_log(GLuint p) {
  glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[1024] = ""; GLsizei ln = 0; glGetProgramInfoLog(p, sizeof log, &ln, log); debugPrintf("PROGRAM LINK FAIL (%u): %s\n", p, log);
    GLuint shs[4] = {0}; GLsizei nsh = 0; glGetAttachedShaders(p, 4, &nsh, shs);
    for (GLsizei i = 0; i < nsh; i++) { GLint cok = 0; glGetShaderiv(shs[i], GL_COMPILE_STATUS, &cok); char sl[2048] = ""; GLsizei sln = 0; glGetShaderInfoLog(shs[i], sizeof sl, &sln, sl);
      debugPrintf("  shader %u compile ok=%d log: %s\n", shs[i], cok, sl); if (!cok && shader_fail_dumps++ < 4) dump_src(shs[i]); } }
}
// GL functions absent from VitaGL — no-op/sane-default implementations
void glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {}
void glCompressedTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLsizei s, const void *d) { static int once; if (!once++) debugPrintf("GL: glCompressedTexSubImage2D called (stub)\n"); }
void glDetachShader(GLuint p, GLuint s) {}
void glGetRenderbufferParameteriv(GLenum t, GLenum p, GLint *v) { *v = 0; }
void glGetShaderPrecisionFormat(GLenum st, GLenum pt, GLint *range, GLint *prec) { range[0] = 127; range[1] = 127; *prec = 23; }
void glGetTexParameterfv(GLenum t, GLenum p, GLfloat *v) { *v = 0; }
void glGetTexParameteriv(GLenum t, GLenum p, GLint *v) { *v = 0; }
void glGetUniformfv(GLuint p, GLint l, GLfloat *v) { *v = 0; }
void glGetUniformiv(GLuint p, GLint l, GLint *v) { *v = 0; }
GLboolean glIsBuffer(GLuint b) { return b != 0; }
GLboolean glIsShader(GLuint s) { return s != 0; }
void glSampleCoverage(GLfloat v, GLboolean i) {}
void glTexParameterfv(GLenum t, GLenum p, const GLfloat *v) { glTexParameterf(t, p, *v); }
void glValidateProgram(GLuint p) {}
void *__gnu_Unwind_Find_exidx(void *pc, int *count) { *count = 0; return NULL; }
int writev(int fd, const void *iov, int cnt) { const struct { void *b; size_t l; } *v = iov; int n = 0; for (int i = 0; i < cnt; i++) n += write(fd, v[i].b, v[i].l); return n; }
extern const char _ctype_[];
// pthread/sem ABI bridge (pthread_bridge.c)
extern void pthread_attr_destroy_bridge();
extern void pthread_attr_getstack_bridge();
extern void pthread_attr_init_bridge();
extern void pthread_attr_setdetachstate_bridge();
extern void pthread_attr_setschedparam_bridge();
extern void pthread_attr_setschedpolicy_bridge();
extern void pthread_attr_setstack_bridge();
extern void pthread_attr_setstacksize_bridge();
extern void pthread_cond_broadcast_bridge();
extern void pthread_cond_destroy_bridge();
extern void pthread_cond_init_bridge();
extern void pthread_cond_signal_bridge();
extern void pthread_cond_timedwait_bridge();
int cond_timedwait_timed(void **c, void **m, const struct timespec *ts) {   // main-thread timed waits counted like sleeps
  int (*f)(void **, void **, const struct timespec *) = (int (*)(void **, void **, const struct timespec *))pthread_cond_timedwait_bridge;
  if (!on_main()) return f(c, m, ts);
  SceUInt64 t = sceKernelGetProcessTimeWide(); int r = f(c, m, ts);
  main_wait_us += sceKernelGetProcessTimeWide() - t; main_wait_calls++; return r;
}
extern void pthread_cond_wait_bridge();
extern void pthread_create_bridge();
extern void pthread_detach_bridge();
extern void pthread_exit_bridge();
extern void pthread_getattr_np_bridge();
extern void pthread_getschedparam_bridge();
extern void pthread_getspecific_bridge();
extern void pthread_join_bridge();
extern void pthread_key_create_bridge();
extern void pthread_key_delete_bridge();
extern void pthread_mutex_destroy_bridge();
extern void pthread_mutex_init_bridge();
extern void pthread_mutex_lock_bridge();
extern void pthread_mutex_trylock_bridge();
extern void pthread_mutex_unlock_bridge();
extern void pthread_mutexattr_destroy_bridge();
extern void pthread_mutexattr_init_bridge();
extern void pthread_mutexattr_setpshared_bridge();
extern void pthread_mutexattr_settype_bridge();
extern void pthread_once_bridge();
extern void pthread_self_bridge();
extern void pthread_setschedparam_bridge();
extern void pthread_setspecific_bridge();
extern void sched_yield_bridge();
extern void sem_destroy_bridge();
extern void sem_getvalue_bridge();
extern void sem_init_bridge();
extern void sem_post_bridge();
extern void sem_timedwait_bridge();
extern void sem_trywait_bridge();
extern void sem_wait_bridge();
// ---- POSIX path translation: the game is given Android-style absolute storage paths ("/data/ssx/...") so its
// filesystem layer routes them to its native device; every libc call maps that prefix back to the card.
const char *fix_path(const char *p, char *out, size_t n) {
  if (p && !strncmp(p, "/data/ssx/", 10)) { snprintf(out, n, DATA_PATH "/%s", p + 10); return out; }
  if (p && !strcmp(p, "/data/ssx")) { snprintf(out, n, DATA_PATH); return out; }
  // Relative paths (MemoryExhausted.xml, GFxShaders.cache, Lua's "./x.lua") have no cwd on the Vita and always fail: root them in DATA_PATH.
  if (p && *p && *p != '/' && !strchr(p, ':')) { if (p[0] == '.' && p[1] == '/') p += 2; snprintf(out, n, DATA_PATH "/%s", p); return out; }
  return p;
}
FILE *fopen_log(const char *p, const char *m) { char b[512]; const char *q = fix_path(p, b, sizeof b); FILE *f = fopen(q, m); debugPrintf("fopen(%s,%s) -> %p\n", q, m, f); return f; }
// open(2) flags differ between bionic and newlib: bionic O_CREAT 0x40 O_EXCL 0x80 O_TRUNC 0x200 O_APPEND 0x400. Passed through raw,
// the game's create-for-write (save profile) arrived as "truncate, no create" and failed with -1 -> save/load popup stuck.
static int bionic_oflags(int f) {
  int nf = f & 3;                       // O_RDONLY/O_WRONLY/O_RDWR are the same
  if (f & 0x40)  nf |= O_CREAT;
  if (f & 0x80)  nf |= O_EXCL;
  if (f & 0x200) nf |= O_TRUNC;
  if (f & 0x400) nf |= O_APPEND;
  return nf;
}
// Save-file I/O tracing (internal/ssxv/*). The game writes a ~25MB "Personal Best Ghosts" save while loading a race and then
// can't afford the 25MB 'SaveLoadFileBuffer' (Global pool) to read it back. Log every write/seek/truncate on save files and
// cap the ghosts file at GHOST_CAP bytes (writes past the cap are reported as done but not stored).
#define GHOST_CAP (64 * 1024 * 1024)   // effectively off: GLOBAL_GEN 32M holds the 25MB SaveLoadFileBuffer (~6MB headroom over the measured ~26MB peak). If Global must shrink, set to 1MB to force a small ghosts file.
static unsigned char save_fd[256];   // 1 = save file, 2 = ghosts save
static int save_log_n;
#define SAVE_LOG(...) do { if (save_log_n++ < 400) debugPrintf(__VA_ARGS__); } while (0)
int open_log(const char *p, int f, ...) { char b[512]; const char *q = fix_path(p, b, sizeof b); int fd = open(q, bionic_oflags(f), 0777); debugPrintf("open(%s, 0x%X) -> %d\n", q, f, fd);
  if (fd >= 0 && fd < 256) save_fd[fd] = strstr(q, "/internal/ssxv/") ? (strstr(q, "ghosts") ? 2 : 1) : 0;
  return fd; }
int close_fix(int fd) { if (fd >= 0 && fd < 256 && save_fd[fd]) { SAVE_LOG("save close(%d)\n", fd); save_fd[fd] = 0; } return close(fd); }
ssize_t write_fix(int fd, const void *buf, size_t n) {
  if (fd < 0 || fd >= 256 || !save_fd[fd]) return write(fd, buf, n);
  off_t pos = lseek(fd, 0, SEEK_CUR);
  if (save_fd[fd] == 2 && (long long)pos + (long long)n > GHOST_CAP) {
    size_t keep = pos >= GHOST_CAP ? 0 : (size_t)(GHOST_CAP - pos);
    SAVE_LOG("save write(%d) @%ld len %u -> CAPPED to %u (ghosts cap %d)\n", fd, (long)pos, (unsigned)n, (unsigned)keep, GHOST_CAP);
    if (keep) write(fd, buf, keep);
    lseek(fd, pos + (off_t)n, SEEK_SET);   // keep the game's notion of the file position consistent
    return (ssize_t)n;
  }
  ssize_t r = write(fd, buf, n);
  SAVE_LOG("save write(%d) @%ld len %u -> %d\n", fd, (long)pos, (unsigned)n, (int)r);
  return r;
}
off_t lseek_fix(int fd, off_t off, int whence) {
  off_t r = lseek(fd, off, whence);
  if (fd >= 0 && fd < 256 && save_fd[fd]) SAVE_LOG("save lseek(%d, %ld, %d) -> %ld\n", fd, (long)off, whence, (long)r);
  return r;
}
int ftruncate_fix(int fd, off_t len) {
  if (fd >= 0 && fd < 256 && save_fd[fd]) {
    SAVE_LOG("save ftruncate(%d, %ld)%s\n", fd, (long)len, (save_fd[fd] == 2 && len > GHOST_CAP) ? " -> CAPPED" : "");
    if (save_fd[fd] == 2 && len > GHOST_CAP) len = GHOST_CAP;
  }
  return ftruncate(fd, len);
}
DIR *opendir_fix(const char *p) { char b[512]; const char *q = fix_path(p, b, sizeof b); DIR *d = opendir(q); debugPrintf("opendir(%s) -> %p\n", q, d); return d; }
// The game is compiled against bionic's ARM struct stat (104 bytes, st_size at +48); newlib's Vita struct stat has a different,
// smaller layout. Passing newlib's struct straight through gave the game a garbage st_size (-> 25MB 'SaveLoadFileBuffer' OOM when
// loading the ghosts save) and let stat() write past/short of the caller's buffer. Convert field by field.
typedef struct {
  unsigned long long st_dev; unsigned char pad0[4]; unsigned long st_ino32;
  unsigned int st_mode, st_nlink, st_uid, st_gid;
  unsigned long long st_rdev; unsigned char pad3[4];
  long long st_size; unsigned long st_blksize; unsigned long long st_blocks;
  unsigned long st_atime_s, st_atime_ns, st_mtime_s, st_mtime_ns, st_ctime_s, st_ctime_ns;
  unsigned long long st_ino;
} bionic_stat;
_Static_assert(sizeof(bionic_stat) == 104, "bionic struct stat must be 104 bytes");
_Static_assert(__builtin_offsetof(bionic_stat, st_size) == 48, "bionic st_size must be at +48");
static void to_bionic_stat(const struct stat *n, bionic_stat *b) {
  memset(b, 0, sizeof *b);
  b->st_dev = n->st_dev; b->st_ino32 = (unsigned long)n->st_ino; b->st_ino = n->st_ino;
  b->st_mode = n->st_mode; b->st_nlink = n->st_nlink; b->st_uid = n->st_uid; b->st_gid = n->st_gid;
  b->st_rdev = n->st_rdev; b->st_size = n->st_size; b->st_blksize = 512; b->st_blocks = (n->st_size + 511) / 512;
  b->st_atime_s = (unsigned long)n->st_atime; b->st_mtime_s = (unsigned long)n->st_mtime; b->st_ctime_s = (unsigned long)n->st_ctime;
}
int stat_fix(const char *p, bionic_stat *bs) {
  char b[512]; const char *q = fix_path(p, b, sizeof b); struct stat s; int r = stat(q, &s);
  if (r == 0 && bs) to_bionic_stat(&s, bs);
  { static int n; if (n++ < 60) debugPrintf("stat(%s) -> %d size %lld\n", q, r, r == 0 ? (long long)s.st_size : -1LL); }
  return r;
}
int fstat_fix(int fd, bionic_stat *bs) {
  struct stat s; int r = fstat(fd, &s);
  if (r == 0 && bs) to_bionic_stat(&s, bs);
  { static int n; if (n++ < 60) debugPrintf("fstat(%d) -> %d size %lld\n", fd, r, r == 0 ? (long long)s.st_size : -1LL); }
  return r;
}
int mkdir_fix(const char *p, int mode) { char b[512]; const char *q = fix_path(p, b, sizeof b); int r = mkdir(q, mode); debugPrintf("mkdir(%s) -> %d\n", q, r); return r; }
int rmdir_fix(const char *p) { char b[512]; return rmdir(fix_path(p, b, sizeof b)); }
int unlink_fix(const char *p) { char b[512]; return unlink(fix_path(p, b, sizeof b)); }
int remove_fix(const char *p) { char b[512]; return remove(fix_path(p, b, sizeof b)); }
int rename_fix(const char *a, const char *c) { char b1[512], b2[512]; return rename(fix_path(a, b1, sizeof b1), fix_path(c, b2, sizeof b2)); }
// malloc/free with logging of large requests (game heaps are carved with malloc)
// Big game pools (>=20MB, allocated once at boot) are served from VitaGL's otherwise idle phycont (~26MB) and
// cdialog-budget (~9MB) pools before touching the newlib heap, which the game's other pools fill to the byte.
// Only the 26MB GameWorld::SlotAlloc slab fits phycont; everything else falls through to newlib unchanged.
static struct { void *p; size_t n; } big_tab[8]; static int big_n;
static void *big_alloc(size_t n) {
  if (n < 8u * 1024 * 1024 || big_n >= 8) return NULL;
  void *p = NULL; int pool = 0;
  if (n >= 24u * 1024 * 1024) {                    // >=24MB slabs (the 25MB GameWorld::SlotAlloc): phycont first, then budget.
                                                   // (24MB, not 20: Audio::Data at 22M is allocated earlier and must not take phycont)
    p = vglAlloc(n, VGL_MEM_PHYCONT); pool = 2;
    if (!p) { p = vglAlloc(n, VGL_MEM_BUDGET); pool = 3; }
  } else if (n <= vglMemFree(VGL_MEM_BUDGET)) {   // 8-20MB: the ~9MB cdialog budget pool takes the first one that fits (an 8MB boot pool),
    p = vglAlloc(n, VGL_MEM_BUDGET); pool = 3;     // freeing that much newlib heap for the Audio::Data pool
  }
  if (!p) return NULL;
  big_tab[big_n].p = p; big_tab[big_n].n = n; big_n++;
  debugPrintf("big alloc %uKB -> VitaGL pool %d at %p (phycont free %uKB budget free %uKB)\n", (unsigned)(n / 1024), pool, p, (unsigned)vglMemFree(VGL_MEM_PHYCONT) / 1024, (unsigned)vglMemFree(VGL_MEM_BUDGET) / 1024);
  return p;
}
static int big_free(void *p) { for (int i = 0; i < big_n; i++) if (big_tab[i].p == p) { vglFree(p); big_tab[i] = big_tab[--big_n]; return 1; } return 0; }
void free_big(void *p) { if (p && big_free(p)) return; free(p); }
void *malloc_log(size_t n) { void *p = big_alloc(n); if (!p) p = malloc(n); if (n >= 0x100000 || !p) debugPrintf("malloc(%u) -> %p\n", (unsigned)n, p); return p; }
void *calloc_log(size_t a, size_t b) { void *p = calloc(a, b); if (a * b >= 0x100000 || !p) debugPrintf("calloc(%u) -> %p\n", (unsigned)(a * b), p); return p; }
void *memalign_log(size_t al, size_t n) { void *p = al <= 4096 ? big_alloc(n) : NULL; if (!p) p = memalign(al, n); if (n >= 0x100000 || !p) debugPrintf("memalign(%u,%u) -> %p\n", (unsigned)al, (unsigned)n, p); return p; }
// GL wrappers (gl_lock.c)
extern void *egl_get_proc_address_shim(const char *);
extern void glActiveTexture_locked();
extern void glAttachShader_locked();
extern void glBindAttribLocation_locked();
extern void glBindBuffer_locked();
extern void glBindFramebuffer_locked();
extern void glBindRenderbuffer_locked();
extern void glBindTexture_locked();
extern void glBlendEquation_locked();
extern void glBlendEquationSeparate_locked();
extern void glBlendFunc_locked();
extern void glBlendFuncSeparate_locked();
extern void glBufferData_locked();
extern void glBufferSubData_locked();
extern void glCheckFramebufferStatus_locked();
extern void glClear_locked();
extern void glClearColor_locked();
extern void glClearDepthf_locked();
extern void glClearStencil_locked();
extern void glColorMask_locked();
extern void glCompileShader_locked();
extern void glCompressedTexImage2D_locked();
extern void glCopyTexImage2D_locked();
extern void glCopyTexSubImage2D_locked();
extern void glCreateProgram_locked();
extern void glCreateShader_locked();
extern void glCullFace_locked();
extern void glDeleteBuffers_locked();
extern void glDeleteFramebuffers_locked();
extern void glDeleteProgram_locked();
extern void glDeleteRenderbuffers_locked();
extern void glDeleteShader_locked();
extern void glDeleteTextures_locked();
extern void glDepthFunc_locked();
extern void glDepthMask_locked();
extern void glDepthRangef_locked();
extern void glDisable_locked();
extern void glDisableVertexAttribArray_locked();
extern void glDrawArrays_locked();
extern void glDrawElements_locked();
extern void glEnable_locked();
extern void glEnableVertexAttribArray_locked();
extern void glFinish_locked();
extern void glFlush_locked();
extern void glFramebufferRenderbuffer_locked();
extern void glFramebufferTexture2D_locked();
extern void glFrontFace_locked();
extern void glGenBuffers_locked();
extern void glGenFramebuffers_locked();
extern void glGenRenderbuffers_locked();
extern void glGenTextures_locked();
extern void glGenerateMipmap_locked();
extern void glGetActiveAttrib_locked();
extern void glGetActiveUniform_locked();
extern void glGetAttachedShaders_locked();
extern void glGetAttribLocation_locked();
extern void glGetBooleanv_locked();
extern void glGetBufferParameteriv_locked();
extern void glGetError_locked();
extern void glGetFloatv_locked();
extern void glGetFramebufferAttachmentParameteriv_locked();
extern void glGetIntegerv_locked();
extern void glGetProgramInfoLog_locked();
extern void glGetProgramiv_locked();
extern void glGetShaderInfoLog_locked();
extern void glGetShaderSource_locked();
extern void glGetShaderiv_locked();
extern void glGetString_locked();
extern void glGetUniformLocation_locked();
extern void glGetVertexAttribPointerv_locked();
extern void glGetVertexAttribfv_locked();
extern void glGetVertexAttribiv_locked();
extern void glHint_locked();
extern void glIsEnabled_locked();
extern void glIsFramebuffer_locked();
extern void glIsProgram_locked();
extern void glIsRenderbuffer_locked();
extern void glIsTexture_locked();
extern void glLineWidth_locked();
extern void glLinkProgram_locked();
extern void glPixelStorei_locked();
extern void glPolygonOffset_locked();
extern void glReadPixels_locked();
extern void glReleaseShaderCompiler_locked();
extern void glRenderbufferStorage_locked();
extern void glScissor_locked();
extern void glShaderBinary_locked();
extern void glShaderSource_locked();
extern void glStencilFunc_locked();
extern void glStencilFuncSeparate_locked();
extern void glStencilMask_locked();
extern void glStencilMaskSeparate_locked();
extern void glStencilOp_locked();
extern void glStencilOpSeparate_locked();
extern void glTexImage2D_locked();
extern void glTexParameterf_locked();
extern void glTexParameteri_locked();
extern void glTexParameteriv_locked();
extern void glTexSubImage2D_locked();
extern void glUniform1f_locked();
extern void glUniform1fv_locked();
extern void glUniform1i_locked();
extern void glUniform1iv_locked();
extern void glUniform2f_locked();
extern void glUniform2fv_locked();
extern void glUniform2i_locked();
extern void glUniform2iv_locked();
extern void glUniform3f_locked();
extern void glUniform3fv_locked();
extern void glUniform3i_locked();
extern void glUniform3iv_locked();
extern void glUniform4f_locked();
extern void glUniform4fv_locked();
extern void glUniform4i_locked();
extern void glUniform4iv_locked();
extern void glUniformMatrix2fv_locked();
extern void glUniformMatrix3fv_locked();
extern void glUniformMatrix4fv_locked();
extern void glUseProgram_locked();
extern void glVertexAttrib1f_locked();
extern void glVertexAttrib1fv_locked();
extern void glVertexAttrib2f_locked();
extern void glVertexAttrib2fv_locked();
extern void glVertexAttrib3f_locked();
extern void glVertexAttrib3fv_locked();
extern void glVertexAttrib4f_locked();
extern void glVertexAttrib4fv_locked();
extern void glVertexAttribPointer_locked();
extern void glViewport_locked();
// libgcc / libstdc++ runtime symbols with no header
extern void __aeabi_atexit();
extern void __aeabi_d2lz();
extern void __aeabi_d2uiz();
extern void __aeabi_d2ulz();
extern void __aeabi_dmul();
extern void __aeabi_dsub();
extern void __aeabi_f2d();
extern void __aeabi_fcmplt();
extern void __aeabi_idiv();
extern void __aeabi_idivmod();
extern void __aeabi_l2d();
extern void __aeabi_l2f();
extern void __aeabi_ldivmod();
extern void __aeabi_ui2d();
extern void __aeabi_uidiv();
extern void __aeabi_uidivmod();
extern void __aeabi_ul2d();
extern void __aeabi_ul2f();
extern void __aeabi_uldivmod();
extern void __cxa_atexit();
extern void __cxa_finalize();

so_default_dynlib default_dynlib[] = {
  { "_Unwind_Backtrace", (uintptr_t)&_Unwind_Backtrace },
  { "_Unwind_VRS_Get", (uintptr_t)&_Unwind_VRS_Get },
  { "_ZN2EA6Nimble8Tracking8Tracking10setEnabledEb", (uintptr_t)&_ZN2EA6Nimble8Tracking8Tracking10setEnabledEb },
  { "_ZN2EA6Nimble8Tracking8Tracking12getComponentEv", (uintptr_t)&_ZN2EA6Nimble8Tracking8Tracking12getComponentEv },
  { "_ZN2EA6Nimble8Tracking8Tracking8logEventESsRKSt3mapISsSsSt4lessISsESaISt4pairIKSsSsEEE", (uintptr_t)&_ZN2EA6Nimble8Tracking8Tracking8logEventESsRKSt3mapISsSsSt4lessISsESaISt4pairIKSsSsEEE },
  { "_ZN2EA6Nimble8Tracking8Tracking9isEnabledEv", (uintptr_t)&_ZN2EA6Nimble8Tracking8Tracking9isEnabledEv },
  { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
  { "__aeabi_d2lz", (uintptr_t)&__aeabi_d2lz },
  { "__aeabi_d2uiz", (uintptr_t)&__aeabi_d2uiz },
  { "__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz },
  { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
  { "__aeabi_dsub", (uintptr_t)&__aeabi_dsub },
  { "__aeabi_f2d", (uintptr_t)&__aeabi_f2d },
  { "__aeabi_fcmplt", (uintptr_t)&__aeabi_fcmplt },
  { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
  { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
  { "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
  { "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
  { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
  { "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
  { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
  { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
  { "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d },
  { "__aeabi_ul2f", (uintptr_t)&__aeabi_ul2f },
  { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
  { "__aeabi_unwind_cpp_pr0", (uintptr_t)&__aeabi_unwind_cpp_pr0 },
  { "__aeabi_unwind_cpp_pr1", (uintptr_t)&__aeabi_unwind_cpp_pr1 },
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
  { "__android_log_write", (uintptr_t)&__android_log_write },
  { "__assert2", (uintptr_t)&__assert2 },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
  { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
  { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
  { "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
  { "__errno", (uintptr_t)&__errno },
  { "__isinf", (uintptr_t)&__isinf },
  { "__page_size", (uintptr_t)&__page_size },
  { "__sF", (uintptr_t)__sF_fake },
  { "__srget", (uintptr_t)&__srget },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard },
  { "abort", (uintptr_t)&abort },
  { "accept", (uintptr_t)&accept },
  { "acos", (uintptr_t)&acos },
  { "acosf", (uintptr_t)&acosf },
  { "asctime", (uintptr_t)&asctime },
  { "asin", (uintptr_t)&asin },
  { "asinf", (uintptr_t)&asinf },
  { "atan", (uintptr_t)&atan },
  { "atan2", (uintptr_t)&atan2 },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "atoi", (uintptr_t)&atoi },
  { "bcopy", (uintptr_t)&bcopy },
  { "bind", (uintptr_t)&bind },
  { "bsearch", (uintptr_t)&bsearch },
  { "calloc", (uintptr_t)&calloc_log },
  { "ceil", (uintptr_t)&ceil },
  { "ceilf", (uintptr_t)&ceilf },
  { "chdir", (uintptr_t)&chdir },
  { "chmod", (uintptr_t)&chmod },
  { "clock", (uintptr_t)&clock },
  { "clock_gettime", (uintptr_t)&clock_gettime_rt },
  { "close", (uintptr_t)&close_fix },
  { "closedir", (uintptr_t)&closedir },
  { "connect", (uintptr_t)&connect },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  { "cosh", (uintptr_t)&cosh },
  { "difftime", (uintptr_t)&difftime },
  { "eglGetProcAddress", (uintptr_t)&egl_get_proc_address_shim },
  { "execv", (uintptr_t)&execv },
  { "exit", (uintptr_t)&exit },
  { "exp", (uintptr_t)&exp },
  { "expf", (uintptr_t)&expf },
  { "fabsf", (uintptr_t)&fabsf },
  { "fclose", (uintptr_t)&fclose },
  { "fcntl", (uintptr_t)&fcntl },
  { "fflush", (uintptr_t)&fflush },
  { "fgetc", (uintptr_t)&fgetc },
  { "fgets", (uintptr_t)&fgets },
  { "floor", (uintptr_t)&floor },
  { "floorf", (uintptr_t)&floorf },
  { "fmod", (uintptr_t)&fmod },
  { "fmodf", (uintptr_t)&fmodf },
  { "fopen", (uintptr_t)&fopen_log },
  { "fork", (uintptr_t)&fork },
  { "fprintf", (uintptr_t)&fprintf },
  { "fputc", (uintptr_t)&fputc },
  { "fputs", (uintptr_t)&fputs },
  { "fread", (uintptr_t)&fread },
  { "free", (uintptr_t)&free_big },
  { "freeaddrinfo", (uintptr_t)&freeaddrinfo },
  { "freopen", (uintptr_t)&freopen },
  { "frexp", (uintptr_t)&frexp },
  { "fseek", (uintptr_t)&fseek },
  { "fstat", (uintptr_t)&fstat_fix },
  { "fsync", (uintptr_t)&fsync },
  { "ftell", (uintptr_t)&ftell },
  { "ftruncate", (uintptr_t)&ftruncate_fix },
  { "fwide", (uintptr_t)&fwide },
  { "fwrite", (uintptr_t)&fwrite },
  { "getaddrinfo", (uintptr_t)&getaddrinfo },
  { "getcwd", (uintptr_t)&getcwd },
  { "getenv", (uintptr_t)&getenv },
  { "gethostbyname", (uintptr_t)&gethostbyname },
  { "gethostname", (uintptr_t)&gethostname },
  { "getpeername", (uintptr_t)&getpeername },
  { "getsockname", (uintptr_t)&getsockname },
  { "getsockopt", (uintptr_t)&getsockopt },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  { "glActiveTexture", (uintptr_t)&glActiveTexture_locked },
  { "glAttachShader", (uintptr_t)&glAttachShader_locked },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation_locked },
  { "glBindBuffer", (uintptr_t)&glBindBuffer_locked },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer_locked },
  { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer_locked },
  { "glBindTexture", (uintptr_t)&glBindTexture_locked },
  { "glBlendColor", (uintptr_t)&glBlendColor },
  { "glBlendEquation", (uintptr_t)&glBlendEquation_locked },
  { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate_locked },
  { "glBlendFunc", (uintptr_t)&glBlendFunc_locked },
  { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate_locked },
  { "glBufferData", (uintptr_t)&glBufferData_locked },
  { "glBufferSubData", (uintptr_t)&glBufferSubData_locked },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus_locked },
  { "glClear", (uintptr_t)&glClear_locked },
  { "glClearColor", (uintptr_t)&glClearColor_locked },
  { "glClearDepthf", (uintptr_t)&glClearDepthf_locked },
  { "glClearStencil", (uintptr_t)&glClearStencil_locked },
  { "glColorMask", (uintptr_t)&glColorMask_locked },
  { "glCompileShader", (uintptr_t)&glCompileShader_locked },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D_locked },
  { "glCompressedTexSubImage2D", (uintptr_t)&glCompressedTexSubImage2D },
  { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D_locked },
  { "glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D_locked },
  { "glCreateProgram", (uintptr_t)&glCreateProgram_locked },
  { "glCreateShader", (uintptr_t)&glCreateShader_locked },
  { "glCullFace", (uintptr_t)&glCullFace_locked },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers_locked },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers_locked },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram_locked },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers_locked },
  { "glDeleteShader", (uintptr_t)&glDeleteShader_locked },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures_locked },
  { "glDepthFunc", (uintptr_t)&glDepthFunc_locked },
  { "glDepthMask", (uintptr_t)&glDepthMask_locked },
  { "glDepthRangef", (uintptr_t)&glDepthRangef_locked },
  { "glDetachShader", (uintptr_t)&glDetachShader },
  { "glDisable", (uintptr_t)&glDisable_locked },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray_locked },
  { "glDrawArrays", (uintptr_t)&glDrawArrays_locked },
  { "glDrawElements", (uintptr_t)&glDrawElements_locked },
  { "glEnable", (uintptr_t)&glEnable_locked },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray_locked },
  { "glFinish", (uintptr_t)&glFinish_locked },
  { "glFlush", (uintptr_t)&glFlush_locked },
  { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer_locked },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D_locked },
  { "glFrontFace", (uintptr_t)&glFrontFace_locked },
  { "glGenBuffers", (uintptr_t)&glGenBuffers_locked },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers_locked },
  { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers_locked },
  { "glGenTextures", (uintptr_t)&glGenTextures_locked },
  { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap_locked },
  { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttrib_locked },
  { "glGetActiveUniform", (uintptr_t)&glGetActiveUniform_locked },
  { "glGetAttachedShaders", (uintptr_t)&glGetAttachedShaders_locked },
  { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation_locked },
  { "glGetBooleanv", (uintptr_t)&glGetBooleanv_locked },
  { "glGetBufferParameteriv", (uintptr_t)&glGetBufferParameteriv_locked },
  { "glGetError", (uintptr_t)&glGetError_locked },
  { "glGetFloatv", (uintptr_t)&glGetFloatv_locked },
  { "glGetFramebufferAttachmentParameteriv", (uintptr_t)&glGetFramebufferAttachmentParameteriv_locked },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv_locked },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog_locked },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv_locked },
  { "glGetRenderbufferParameteriv", (uintptr_t)&glGetRenderbufferParameteriv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog_locked },
  { "glGetShaderPrecisionFormat", (uintptr_t)&glGetShaderPrecisionFormat },
  { "glGetShaderSource", (uintptr_t)&glGetShaderSource_locked },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv_locked },
  { "glGetString", (uintptr_t)&glGetString_locked },
  { "glGetTexParameterfv", (uintptr_t)&glGetTexParameterfv },
  { "glGetTexParameteriv", (uintptr_t)&glGetTexParameteriv },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation_locked },
  { "glGetUniformfv", (uintptr_t)&glGetUniformfv },
  { "glGetUniformiv", (uintptr_t)&glGetUniformiv },
  { "glGetVertexAttribPointerv", (uintptr_t)&glGetVertexAttribPointerv_locked },
  { "glGetVertexAttribfv", (uintptr_t)&glGetVertexAttribfv_locked },
  { "glGetVertexAttribiv", (uintptr_t)&glGetVertexAttribiv_locked },
  { "glHint", (uintptr_t)&glHint_locked },
  { "glIsBuffer", (uintptr_t)&glIsBuffer },
  { "glIsEnabled", (uintptr_t)&glIsEnabled_locked },
  { "glIsFramebuffer", (uintptr_t)&glIsFramebuffer_locked },
  { "glIsProgram", (uintptr_t)&glIsProgram_locked },
  { "glIsRenderbuffer", (uintptr_t)&glIsRenderbuffer_locked },
  { "glIsShader", (uintptr_t)&glIsShader },
  { "glIsTexture", (uintptr_t)&glIsTexture_locked },
  { "glLineWidth", (uintptr_t)&glLineWidth_locked },
  { "glLinkProgram", (uintptr_t)&glLinkProgram_locked },
  { "glPixelStorei", (uintptr_t)&glPixelStorei_locked },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffset_locked },
  { "glReadPixels", (uintptr_t)&glReadPixels_locked },
  { "glReleaseShaderCompiler", (uintptr_t)&glReleaseShaderCompiler_locked },
  { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage_locked },
  { "glSampleCoverage", (uintptr_t)&glSampleCoverage },
  { "glScissor", (uintptr_t)&glScissor_locked },
  { "glShaderBinary", (uintptr_t)&glShaderBinary_locked },
  { "glShaderSource", (uintptr_t)&glShaderSource_locked },
  { "glStencilFunc", (uintptr_t)&glStencilFunc_locked },
  { "glStencilFuncSeparate", (uintptr_t)&glStencilFuncSeparate_locked },
  { "glStencilMask", (uintptr_t)&glStencilMask_locked },
  { "glStencilMaskSeparate", (uintptr_t)&glStencilMaskSeparate_locked },
  { "glStencilOp", (uintptr_t)&glStencilOp_locked },
  { "glStencilOpSeparate", (uintptr_t)&glStencilOpSeparate_locked },
  { "glTexImage2D", (uintptr_t)&glTexImage2D_locked },
  { "glTexParameterf", (uintptr_t)&glTexParameterf_locked },
  { "glTexParameterfv", (uintptr_t)&glTexParameterfv },
  { "glTexParameteri", (uintptr_t)&glTexParameteri_locked },
  { "glTexParameteriv", (uintptr_t)&glTexParameteriv_locked },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D_locked },
  { "glUniform1f", (uintptr_t)&glUniform1f_locked },
  { "glUniform1fv", (uintptr_t)&glUniform1fv_locked },
  { "glUniform1i", (uintptr_t)&glUniform1i_locked },
  { "glUniform1iv", (uintptr_t)&glUniform1iv_locked },
  { "glUniform2f", (uintptr_t)&glUniform2f_locked },
  { "glUniform2fv", (uintptr_t)&glUniform2fv_locked },
  { "glUniform2i", (uintptr_t)&glUniform2i_locked },
  { "glUniform2iv", (uintptr_t)&glUniform2iv_locked },
  { "glUniform3f", (uintptr_t)&glUniform3f_locked },
  { "glUniform3fv", (uintptr_t)&glUniform3fv_locked },
  { "glUniform3i", (uintptr_t)&glUniform3i_locked },
  { "glUniform3iv", (uintptr_t)&glUniform3iv_locked },
  { "glUniform4f", (uintptr_t)&glUniform4f_locked },
  { "glUniform4fv", (uintptr_t)&glUniform4fv_locked },
  { "glUniform4i", (uintptr_t)&glUniform4i_locked },
  { "glUniform4iv", (uintptr_t)&glUniform4iv_locked },
  { "glUniformMatrix2fv", (uintptr_t)&glUniformMatrix2fv_locked },
  { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv_locked },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv_locked },
  { "glUseProgram", (uintptr_t)&glUseProgram_locked },
  { "glValidateProgram", (uintptr_t)&glValidateProgram },
  { "glVertexAttrib1f", (uintptr_t)&glVertexAttrib1f_locked },
  { "glVertexAttrib1fv", (uintptr_t)&glVertexAttrib1fv_locked },
  { "glVertexAttrib2f", (uintptr_t)&glVertexAttrib2f_locked },
  { "glVertexAttrib2fv", (uintptr_t)&glVertexAttrib2fv_locked },
  { "glVertexAttrib3f", (uintptr_t)&glVertexAttrib3f_locked },
  { "glVertexAttrib3fv", (uintptr_t)&glVertexAttrib3fv_locked },
  { "glVertexAttrib4f", (uintptr_t)&glVertexAttrib4f_locked },
  { "glVertexAttrib4fv", (uintptr_t)&glVertexAttrib4fv_locked },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer_locked },
  { "glViewport", (uintptr_t)&glViewport_locked },
  { "gmtime", (uintptr_t)&gmtime },
  { "inet_addr", (uintptr_t)&inet_addr },
  { "inet_ntoa", (uintptr_t)&inet_ntoa },
  { "ioctl", (uintptr_t)&ioctl },
  { "isalnum", (uintptr_t)&isalnum },
  { "isalpha", (uintptr_t)&isalpha },
  { "iscntrl", (uintptr_t)&iscntrl },
  { "islower", (uintptr_t)&islower },
  { "isnan", (uintptr_t)&isnan },
  { "isprint", (uintptr_t)&isprint },
  { "ispunct", (uintptr_t)&ispunct },
  { "isspace", (uintptr_t)&isspace },
  { "isupper", (uintptr_t)&isupper },
  { "isxdigit", (uintptr_t)&isxdigit },
  { "ldexp", (uintptr_t)&ldexp },
  { "listen", (uintptr_t)&listen },
  { "localtime", (uintptr_t)&localtime },
  { "log", (uintptr_t)&log },
  { "log10", (uintptr_t)&log10 },
  { "log10f", (uintptr_t)&log10f },
  { "logf", (uintptr_t)&logf },
  { "longjmp", (uintptr_t)&longjmp },
  { "lrand48", (uintptr_t)&lrand48 },
  { "lseek", (uintptr_t)&lseek_fix },
  { "malloc", (uintptr_t)&malloc_log },
  { "memalign", (uintptr_t)&memalign_log },
  { "memchr", (uintptr_t)&memchr },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "mkdir", (uintptr_t)&mkdir_fix },
  { "mktime", (uintptr_t)&mktime },
  { "mmap", (uintptr_t)&mmap },
  { "modf", (uintptr_t)&modf },
  { "munmap", (uintptr_t)&munmap },
  { "nanosleep", (uintptr_t)&nanosleep_yield },
  { "open", (uintptr_t)&open_log },
  { "opendir", (uintptr_t)&opendir_fix },
  { "poll", (uintptr_t)&poll },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "prctl", (uintptr_t)&prctl },
  { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_bridge },
  { "pthread_attr_getstack", (uintptr_t)&pthread_attr_getstack_bridge },
  { "pthread_attr_init", (uintptr_t)&pthread_attr_init_bridge },
  { "pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate_bridge },
  { "pthread_attr_setschedparam", (uintptr_t)&pthread_attr_setschedparam_bridge },
  { "pthread_attr_setschedpolicy", (uintptr_t)&pthread_attr_setschedpolicy_bridge },
  { "pthread_attr_setstack", (uintptr_t)&pthread_attr_setstack_bridge },
  { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize_bridge },
  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_bridge },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_bridge },
  { "pthread_cond_init", (uintptr_t)&pthread_cond_init_bridge },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_bridge },
  { "pthread_cond_timedwait", (uintptr_t)&cond_timedwait_timed },
  { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_bridge },
  { "pthread_create", (uintptr_t)&pthread_create_bridge },
  { "pthread_detach", (uintptr_t)&pthread_detach_bridge },
  { "pthread_exit", (uintptr_t)&pthread_exit_bridge },
  { "pthread_getattr_np", (uintptr_t)&pthread_getattr_np_bridge },
  { "pthread_getschedparam", (uintptr_t)&pthread_getschedparam_bridge },
  { "pthread_getspecific", (uintptr_t)&pthread_getspecific_bridge },
  { "pthread_join", (uintptr_t)&pthread_join_bridge },
  { "pthread_key_create", (uintptr_t)&pthread_key_create_bridge },
  { "pthread_key_delete", (uintptr_t)&pthread_key_delete_bridge },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_bridge },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_bridge },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_bridge },
  { "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_bridge },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_bridge },
  { "pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy_bridge },
  { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_bridge },
  { "pthread_mutexattr_setpshared", (uintptr_t)&pthread_mutexattr_setpshared_bridge },
  { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_bridge },
  { "pthread_once", (uintptr_t)&pthread_once_bridge },
  { "pthread_self", (uintptr_t)&pthread_self_bridge },
  { "pthread_setschedparam", (uintptr_t)&pthread_setschedparam_bridge },
  { "pthread_setspecific", (uintptr_t)&pthread_setspecific_bridge },
  { "putchar", (uintptr_t)&putchar },
  { "qsort", (uintptr_t)&qsort },
  { "read", (uintptr_t)&read },
  { "readdir", (uintptr_t)&readdir },
  { "readdir_r", (uintptr_t)&readdir_r },
  { "readlink", (uintptr_t)&readlink },
  { "realloc", (uintptr_t)&realloc },
  { "recv", (uintptr_t)&recv },
  { "recvfrom", (uintptr_t)&recvfrom },
  { "remove", (uintptr_t)&remove_fix },
  { "rename", (uintptr_t)&rename_fix },
  { "rewind", (uintptr_t)&rewind },
  { "rmdir", (uintptr_t)&rmdir_fix },
  { "sched_yield", (uintptr_t)&sched_yield_bridge },
  { "sem_destroy", (uintptr_t)&sem_destroy_bridge },
  { "sem_getvalue", (uintptr_t)&sem_getvalue_bridge },
  { "sem_init", (uintptr_t)&sem_init_bridge },
  { "sem_post", (uintptr_t)&sem_post_bridge },
  { "sem_timedwait", (uintptr_t)&sem_timedwait_bridge },
  { "sem_trywait", (uintptr_t)&sem_trywait_bridge },
  { "sem_wait", (uintptr_t)&sem_wait_bridge },
  { "send", (uintptr_t)&send },
  { "sendto", (uintptr_t)&sendto },
  { "setenv", (uintptr_t)&setenv },
  { "setjmp", (uintptr_t)&setjmp },
  { "setlocale", (uintptr_t)&setlocale },
  { "setsockopt", (uintptr_t)&setsockopt },
  { "setvbuf", (uintptr_t)&setvbuf },
  { "shutdown", (uintptr_t)&shutdown },
  { "sin", (uintptr_t)&sin },
  { "sinf", (uintptr_t)&sinf },
  { "sinh", (uintptr_t)&sinh },
  { "sleep", (uintptr_t)&sleep },
  { "snprintf", (uintptr_t)&snprintf },
  { "socket", (uintptr_t)&socket },
  { "sprintf", (uintptr_t)&sprintf },
  { "sqrt", (uintptr_t)&sqrt },
  { "sqrtf", (uintptr_t)&sqrtf },
  { "srand48", (uintptr_t)&srand48 },
  { "sscanf", (uintptr_t)&sscanf },
  { "stat", (uintptr_t)&stat_fix },
  { "statfs", (uintptr_t)&statfs },
  { "strcasecmp", (uintptr_t)&strcasecmp },
  { "strcat", (uintptr_t)&strcat },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcoll", (uintptr_t)&strcoll },
  { "strcpy", (uintptr_t)&strcpy },
  { "strcspn", (uintptr_t)&strcspn },
  { "strerror", (uintptr_t)&strerror },
  { "strftime", (uintptr_t)&strftime },
  { "strlen", (uintptr_t)&strlen },
  { "strncasecmp", (uintptr_t)&strncasecmp },
  { "strncat", (uintptr_t)&strncat },
  { "strncmp", (uintptr_t)&strncmp },
  { "strncpy", (uintptr_t)&strncpy },
  { "strpbrk", (uintptr_t)&strpbrk },
  { "strrchr", (uintptr_t)&strrchr },
  { "strspn", (uintptr_t)&strspn },
  { "strstr", (uintptr_t)&strstr },
  { "strtod", (uintptr_t)&strtod },
  { "strtok", (uintptr_t)&strtok },
  { "strtol", (uintptr_t)&strtol },
  { "strtoul", (uintptr_t)&strtoul },
  { "strtoull", (uintptr_t)&strtoull },
  { "syscall", (uintptr_t)&syscall },
  { "sysconf", (uintptr_t)&sysconf },
  { "system", (uintptr_t)&system },
  { "tan", (uintptr_t)&tan },
  { "tanf", (uintptr_t)&tanf },
  { "tanh", (uintptr_t)&tanh },
  { "time", (uintptr_t)&time },
  { "timezone", (uintptr_t)&timezone },
  { "tmpnam", (uintptr_t)&tmpnam },
  { "tolower", (uintptr_t)&tolower },
  { "toupper", (uintptr_t)&toupper },
  { "tzname", (uintptr_t)&tzname },
  { "tzset", (uintptr_t)&tzset },
  { "ungetc", (uintptr_t)&ungetc },
  { "unlink", (uintptr_t)&unlink_fix },
  { "unsetenv", (uintptr_t)&unsetenv },
  { "usleep", (uintptr_t)&usleep_yield },
  { "utime", (uintptr_t)&utime },
  { "vprintf", (uintptr_t)&vprintf },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf },
  { "waitpid", (uintptr_t)&waitpid },
  { "wcscmp", (uintptr_t)&wcscmp },
  { "wcscoll", (uintptr_t)&wcscoll },
  { "wcscpy", (uintptr_t)&wcscpy },
  { "write", (uintptr_t)&write_fix },
  { "_ctype_", (uintptr_t)&_ctype_ },
  { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
  { "wmemchr", (uintptr_t)&wmemchr },
  { "wmemcpy", (uintptr_t)&wmemcpy },
  { "wcrtomb", (uintptr_t)&wcrtomb },
  { "mbrtowc", (uintptr_t)&mbrtowc },
  { "strxfrm", (uintptr_t)&strxfrm },
  { "wcsxfrm", (uintptr_t)&wcsxfrm },
  { "wctype", (uintptr_t)&wctype },
  { "towupper", (uintptr_t)&towupper },
  { "towlower", (uintptr_t)&towlower },
  { "iswctype", (uintptr_t)&iswctype },
  { "wctob", (uintptr_t)&wctob },
  { "btowc", (uintptr_t)&btowc },
  { "wcsftime", (uintptr_t)&wcsftime },
  { "fdopen", (uintptr_t)&fdopen },
  { "writev", (uintptr_t)&writev },
  { "putwc", (uintptr_t)&putwc },
  { "ungetwc", (uintptr_t)&ungetwc },
  { "getwc", (uintptr_t)&getwc },
  { "getc", (uintptr_t)&getc },
  { "putc", (uintptr_t)&putc },
  { "wcslen", (uintptr_t)&wcslen },
  { "wmemset", (uintptr_t)&wmemset },
  { "wmemmove", (uintptr_t)&wmemmove },
  { "wmemcmp", (uintptr_t)&wmemcmp },
  { "raise", (uintptr_t)&raise },
};
int default_dynlib_size = sizeof(default_dynlib);
