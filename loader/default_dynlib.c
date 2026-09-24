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
int usleep_yield(useconds_t us) { sceKernelDelayThread(us < 100 ? 100 : us); return 0; }
int nanosleep_yield(const struct timespec *req, struct timespec *rem) { long long us = req ? (long long)req->tv_sec * 1000000 + req->tv_nsec / 1000 : 0; sceKernelDelayThread(us < 100 ? 100 : (SceUInt)us); return 0; }
// Shader compile/link diagnostics: VitaGL translates GLSL through vitashark; failures are otherwise silent
static char last_src_head[400];
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
void glShaderSource_log(GLuint sh, GLsizei n, const GLchar **src, const GLint *len) {
  if (n > 0 && src && src[0]) {
    int l = len && len[0] > 0 ? len[0] : (int)strlen(src[0]);
    int h = l > 380 ? 380 : l; memcpy(last_src_head, src[0], h); last_src_head[h] = 0;
    if (n == 1) { char *fixed = glsl_prepass(src[0], l); const GLchar *one[1] = { fixed }; glShaderSource(sh, 1, one, NULL); free(fixed); return; }
  }
  glShaderSource(sh, n, src, len);
}
void glCompileShader_log(GLuint sh) {
  glCompileShader(sh);
  GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024] = ""; GLsizei ln = 0; glGetShaderInfoLog(sh, sizeof log, &ln, log);
    debugPrintf("SHADER COMPILE FAIL (%u): %s\n--- source head ---\n%s\n---\n", sh, log, last_src_head); }
}
void glLinkProgram_log(GLuint p) {
  glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[1024] = ""; GLsizei ln = 0; glGetProgramInfoLog(p, sizeof log, &ln, log); debugPrintf("PROGRAM LINK FAIL (%u): %s\n", p, log); }
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
FILE *fopen_log(const char *p, const char *m) { FILE *f = fopen(p, m); debugPrintf("fopen(%s,%s) -> %p\n", p, m, f); return f; }
int open_log(const char *p, int f, ...) { int fd = open(p, f, 0777); debugPrintf("open(%s) -> %d\n", p, fd); return fd; }
// malloc/free with logging of large requests (game heaps are carved with malloc)
void *malloc_log(size_t n) { void *p = malloc(n); if (n >= 0x100000 || !p) debugPrintf("malloc(%u) -> %p\n", (unsigned)n, p); return p; }
void *calloc_log(size_t a, size_t b) { void *p = calloc(a, b); if (a * b >= 0x100000 || !p) debugPrintf("calloc(%u) -> %p\n", (unsigned)(a * b), p); return p; }
void *memalign_log(size_t al, size_t n) { void *p = memalign(al, n); if (n >= 0x100000 || !p) debugPrintf("memalign(%u,%u) -> %p\n", (unsigned)al, (unsigned)n, p); return p; }
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
  { "close", (uintptr_t)&close },
  { "closedir", (uintptr_t)&closedir },
  { "connect", (uintptr_t)&connect },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  { "cosh", (uintptr_t)&cosh },
  { "difftime", (uintptr_t)&difftime },
  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
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
  { "free", (uintptr_t)&free },
  { "freeaddrinfo", (uintptr_t)&freeaddrinfo },
  { "freopen", (uintptr_t)&freopen },
  { "frexp", (uintptr_t)&frexp },
  { "fseek", (uintptr_t)&fseek },
  { "fstat", (uintptr_t)&fstat },
  { "fsync", (uintptr_t)&fsync },
  { "ftell", (uintptr_t)&ftell },
  { "ftruncate", (uintptr_t)&ftruncate },
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
  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
  { "glBindBuffer", (uintptr_t)&glBindBuffer },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendColor", (uintptr_t)&glBlendColor },
  { "glBlendEquation", (uintptr_t)&glBlendEquation },
  { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate },
  { "glBufferData", (uintptr_t)&glBufferData },
  { "glBufferSubData", (uintptr_t)&glBufferSubData },
  { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
  { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor },
  { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil },
  { "glColorMask", (uintptr_t)&glColorMask },
  { "glCompileShader", (uintptr_t)&glCompileShader_log },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
  { "glCompressedTexSubImage2D", (uintptr_t)&glCompressedTexSubImage2D },
  { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D },
  { "glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D },
  { "glCreateProgram", (uintptr_t)&glCreateProgram },
  { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCullFace", (uintptr_t)&glCullFace },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
  { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
  { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask },
  { "glDepthRangef", (uintptr_t)&glDepthRangef },
  { "glDetachShader", (uintptr_t)&glDetachShader },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
  { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFinish", (uintptr_t)&glFinish },
  { "glFlush", (uintptr_t)&glFlush },
  { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
  { "glFrontFace", (uintptr_t)&glFrontFace },
  { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
  { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap },
  { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttrib },
  { "glGetActiveUniform", (uintptr_t)&glGetActiveUniform },
  { "glGetAttachedShaders", (uintptr_t)&glGetAttachedShaders },
  { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
  { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
  { "glGetBufferParameteriv", (uintptr_t)&glGetBufferParameteriv },
  { "glGetError", (uintptr_t)&glGetError },
  { "glGetFloatv", (uintptr_t)&glGetFloatv },
  { "glGetFramebufferAttachmentParameteriv", (uintptr_t)&glGetFramebufferAttachmentParameteriv },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetRenderbufferParameteriv", (uintptr_t)&glGetRenderbufferParameteriv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
  { "glGetShaderPrecisionFormat", (uintptr_t)&glGetShaderPrecisionFormat },
  { "glGetShaderSource", (uintptr_t)&glGetShaderSource },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
  { "glGetString", (uintptr_t)&glGetString },
  { "glGetTexParameterfv", (uintptr_t)&glGetTexParameterfv },
  { "glGetTexParameteriv", (uintptr_t)&glGetTexParameteriv },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glGetUniformfv", (uintptr_t)&glGetUniformfv },
  { "glGetUniformiv", (uintptr_t)&glGetUniformiv },
  { "glGetVertexAttribPointerv", (uintptr_t)&glGetVertexAttribPointerv },
  { "glGetVertexAttribfv", (uintptr_t)&glGetVertexAttribfv },
  { "glGetVertexAttribiv", (uintptr_t)&glGetVertexAttribiv },
  { "glHint", (uintptr_t)&glHint },
  { "glIsBuffer", (uintptr_t)&glIsBuffer },
  { "glIsEnabled", (uintptr_t)&glIsEnabled },
  { "glIsFramebuffer", (uintptr_t)&glIsFramebuffer },
  { "glIsProgram", (uintptr_t)&glIsProgram },
  { "glIsRenderbuffer", (uintptr_t)&glIsRenderbuffer },
  { "glIsShader", (uintptr_t)&glIsShader },
  { "glIsTexture", (uintptr_t)&glIsTexture },
  { "glLineWidth", (uintptr_t)&glLineWidth },
  { "glLinkProgram", (uintptr_t)&glLinkProgram_log },
  { "glPixelStorei", (uintptr_t)&glPixelStorei },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffset },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glReleaseShaderCompiler", (uintptr_t)&glReleaseShaderCompiler },
  { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage },
  { "glSampleCoverage", (uintptr_t)&glSampleCoverage },
  { "glScissor", (uintptr_t)&glScissor },
  { "glShaderBinary", (uintptr_t)&glShaderBinary },
  { "glShaderSource", (uintptr_t)&glShaderSource_log },
  { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilFuncSeparate", (uintptr_t)&glStencilFuncSeparate },
  { "glStencilMask", (uintptr_t)&glStencilMask },
  { "glStencilMaskSeparate", (uintptr_t)&glStencilMaskSeparate },
  { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glStencilOpSeparate", (uintptr_t)&glStencilOpSeparate },
  { "glTexImage2D", (uintptr_t)&glTexImage2D },
  { "glTexParameterf", (uintptr_t)&glTexParameterf },
  { "glTexParameterfv", (uintptr_t)&glTexParameterfv },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexParameteriv", (uintptr_t)&glTexParameteriv },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
  { "glUniform1f", (uintptr_t)&glUniform1f },
  { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform1iv", (uintptr_t)&glUniform1iv },
  { "glUniform2f", (uintptr_t)&glUniform2f },
  { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform2i", (uintptr_t)&glUniform2i },
  { "glUniform2iv", (uintptr_t)&glUniform2iv },
  { "glUniform3f", (uintptr_t)&glUniform3f },
  { "glUniform3fv", (uintptr_t)&glUniform3fv },
  { "glUniform3i", (uintptr_t)&glUniform3i },
  { "glUniform3iv", (uintptr_t)&glUniform3iv },
  { "glUniform4f", (uintptr_t)&glUniform4f },
  { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniform4i", (uintptr_t)&glUniform4i },
  { "glUniform4iv", (uintptr_t)&glUniform4iv },
  { "glUniformMatrix2fv", (uintptr_t)&glUniformMatrix2fv },
  { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glValidateProgram", (uintptr_t)&glValidateProgram },
  { "glVertexAttrib1f", (uintptr_t)&glVertexAttrib1f },
  { "glVertexAttrib1fv", (uintptr_t)&glVertexAttrib1fv },
  { "glVertexAttrib2f", (uintptr_t)&glVertexAttrib2f },
  { "glVertexAttrib2fv", (uintptr_t)&glVertexAttrib2fv },
  { "glVertexAttrib3f", (uintptr_t)&glVertexAttrib3f },
  { "glVertexAttrib3fv", (uintptr_t)&glVertexAttrib3fv },
  { "glVertexAttrib4f", (uintptr_t)&glVertexAttrib4f },
  { "glVertexAttrib4fv", (uintptr_t)&glVertexAttrib4fv },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glViewport", (uintptr_t)&glViewport },
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
  { "lseek", (uintptr_t)&lseek },
  { "malloc", (uintptr_t)&malloc_log },
  { "memalign", (uintptr_t)&memalign_log },
  { "memchr", (uintptr_t)&memchr },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "mkdir", (uintptr_t)&mkdir },
  { "mktime", (uintptr_t)&mktime },
  { "mmap", (uintptr_t)&mmap },
  { "modf", (uintptr_t)&modf },
  { "munmap", (uintptr_t)&munmap },
  { "nanosleep", (uintptr_t)&nanosleep_yield },
  { "open", (uintptr_t)&open_log },
  { "opendir", (uintptr_t)&opendir },
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
  { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_bridge },
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
  { "remove", (uintptr_t)&remove },
  { "rename", (uintptr_t)&rename },
  { "rewind", (uintptr_t)&rewind },
  { "rmdir", (uintptr_t)&rmdir },
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
  { "stat", (uintptr_t)&stat },
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
  { "unlink", (uintptr_t)&unlink },
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
  { "write", (uintptr_t)&write },
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
