// Bionic (Android) → vitasdk pthread ABI bridge.
// Bionic's pthread_mutex_t / pthread_cond_t / sem_t / pthread_once_t are 4 bytes; vitasdk's are structs.
// The game hands us its 4-byte slot; we heap-allocate the real object and keep only its pointer in the slot.
// Bionic static initialisers: PTHREAD_MUTEX_INITIALIZER = 0, RECURSIVE = 0x4000, ERRORCHECK = 0x8000,
// PTHREAD_COND_INITIALIZER = 0, PTHREAD_ONCE_INIT = 0. Anything < 0x10000 in a slot is treated as "not yet bridged".
#include <vitasdk.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "stubs.h"

#define IS_UNINIT(v) ((uintptr_t)(v) < 0x10000)
#define DEFAULT_STACK (512 * 1024)
#define MAX_STACK     (4 * 1024 * 1024)

static pthread_mutex_t bridge_lock = PTHREAD_MUTEX_INITIALIZER;

// ---------- mutex ----------
static pthread_mutex_t *mutex_get(void **slot) {
  if (!IS_UNINIT(*slot)) return (pthread_mutex_t *)*slot;
  pthread_mutex_lock(&bridge_lock);
  if (IS_UNINIT(*slot)) {
    uintptr_t init = (uintptr_t)*slot;
    pthread_mutex_t *m = calloc(1, sizeof(pthread_mutex_t));
    pthread_mutexattr_t a; pthread_mutexattr_init(&a);
    int type = (init & 0x4000) ? PTHREAD_MUTEX_RECURSIVE : (init & 0x8000) ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL;
    pthread_mutexattr_settype(&a, type);
    pthread_mutex_init(m, &a); pthread_mutexattr_destroy(&a);
    *slot = m;
  }
  pthread_mutex_unlock(&bridge_lock);
  return (pthread_mutex_t *)*slot;
}
int pthread_mutexattr_init_bridge(int *attr) { *attr = 0; return 0; }
int pthread_mutexattr_settype_bridge(int *attr, int type) { *attr = type; return 0; }
int pthread_mutexattr_setpshared_bridge(int *attr, int p) { return 0; }
int pthread_mutexattr_destroy_bridge(int *attr) { return 0; }
int pthread_mutex_init_bridge(void **slot, const int *attr) {
  // Bionic types: 0 normal, 1 recursive, 2 errorcheck
  int type = attr ? *attr : 0;
  *slot = (void *)(uintptr_t)(type == 1 ? 0x4000 : type == 2 ? 0x8000 : 0);
  mutex_get(slot);
  return 0;
}
int pthread_mutex_destroy_bridge(void **slot) {
  if (!IS_UNINIT(*slot)) { pthread_mutex_destroy((pthread_mutex_t *)*slot); free(*slot); *slot = NULL; }
  return 0;
}
int pthread_mutex_lock_bridge(void **slot)    { return pthread_mutex_lock(mutex_get(slot)); }
int pthread_mutex_trylock_bridge(void **slot) { return pthread_mutex_trylock(mutex_get(slot)); }
int pthread_mutex_unlock_bridge(void **slot)  { return pthread_mutex_unlock(mutex_get(slot)); }

// ---------- cond ----------
static pthread_cond_t *cond_get(void **slot) {
  if (!IS_UNINIT(*slot)) return (pthread_cond_t *)*slot;
  pthread_mutex_lock(&bridge_lock);
  if (IS_UNINIT(*slot)) { pthread_cond_t *c = calloc(1, sizeof(pthread_cond_t)); pthread_cond_init(c, NULL); *slot = c; }
  pthread_mutex_unlock(&bridge_lock);
  return (pthread_cond_t *)*slot;
}
int pthread_cond_init_bridge(void **slot, const void *attr) { *slot = NULL; cond_get(slot); return 0; }
int pthread_cond_destroy_bridge(void **slot) {
  if (!IS_UNINIT(*slot)) { pthread_cond_destroy((pthread_cond_t *)*slot); free(*slot); *slot = NULL; }
  return 0;
}
int pthread_cond_signal_bridge(void **slot)    { return pthread_cond_signal(cond_get(slot)); }
int pthread_cond_broadcast_bridge(void **slot) { return pthread_cond_broadcast(cond_get(slot)); }
int pthread_cond_wait_bridge(void **c, void **m) { return pthread_cond_wait(cond_get(c), mutex_get(m)); }
int pthread_cond_timedwait_bridge(void **c, void **m, const struct timespec *ts) { return pthread_cond_timedwait(cond_get(c), mutex_get(m), ts); }

// ---------- attr (Bionic: 24-byte struct; we use its first word as a pointer to the real attr) ----------
typedef struct { pthread_attr_t *real; int pad[5]; } bionic_attr;
static pthread_attr_t *attr_get(bionic_attr *a) {
  if (!a) return NULL;
  if (IS_UNINIT(a->real)) {
    a->real = calloc(1, sizeof(pthread_attr_t));
    pthread_attr_init(a->real);
    pthread_attr_setstacksize(a->real, DEFAULT_STACK);
  }
  return a->real;
}
int pthread_attr_init_bridge(bionic_attr *a) { memset(a, 0, sizeof(*a)); attr_get(a); return 0; }
int pthread_attr_destroy_bridge(bionic_attr *a) {
  if (a && !IS_UNINIT(a->real)) { pthread_attr_destroy(a->real); free(a->real); a->real = NULL; }
  return 0;
}
int pthread_attr_setdetachstate_bridge(bionic_attr *a, int s) { return pthread_attr_setdetachstate(attr_get(a), s ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE); }
int pthread_attr_setstacksize_bridge(bionic_attr *a, size_t n) {
  if (n < 64 * 1024) n = 64 * 1024; if (n > MAX_STACK) n = MAX_STACK;
  return pthread_attr_setstacksize(attr_get(a), n);
}
int pthread_attr_setstack_bridge(bionic_attr *a, void *base, size_t n) { return pthread_attr_setstacksize_bridge(a, n); } // ignore caller-supplied stack memory
int pthread_attr_getstack_bridge(bionic_attr *a, void **base, size_t *n) { *base = NULL; *n = DEFAULT_STACK; return 0; }
int pthread_attr_setschedpolicy_bridge(bionic_attr *a, int p) { return 0; }
int pthread_attr_setschedparam_bridge(bionic_attr *a, const void *p) { if (p) debugPrintf("thread attr sched_priority=%d\n", *(const int *)p); return 0; }
int pthread_getattr_np_bridge(pthread_t t, bionic_attr *a) { return -1; }
int pthread_getschedparam_bridge(pthread_t t, int *policy, void *param) { if (policy) *policy = 0; if (param) memset(param, 0, 4); return 0; }
int pthread_setschedparam_bridge(pthread_t t, int policy, const void *param) { if (param) debugPrintf("setschedparam policy=%d prio=%d\n", policy, *(const int *)param); return 0; }

// ---------- threads ----------
// Worker threads run one notch below the main thread and on any core;
// otherwise the Vita scheduler lets the main thread starve loader/translator threads and asset lifetimes race.
int main_thread_prio = 0;
#define main_prio main_thread_prio
// thread registry for the watchdog (names come from prctl(PR_SET_NAME) which EAThread uses)
typedef struct { SceUID uid; char name[32]; } thread_rec;
thread_rec thread_registry[64]; int thread_registry_n;
static pthread_mutex_t reg_lock = PTHREAD_MUTEX_INITIALIZER;
void thread_registry_add(SceUID uid, const char *nm) {
  pthread_mutex_lock(&reg_lock);
  for (int i = 0; i < thread_registry_n; i++) if (thread_registry[i].uid == uid) { if (nm) strncpy(thread_registry[i].name, nm, 31); pthread_mutex_unlock(&reg_lock); return; }
  if (thread_registry_n < 64) { thread_registry[thread_registry_n].uid = uid; strncpy(thread_registry[thread_registry_n].name, nm ? nm : "?", 31); thread_registry_n++; }
  pthread_mutex_unlock(&reg_lock);
}
typedef struct { void *(*fn)(void *); void *arg; } thread_boot;
static void *thread_boot_fn(void *p) {
  thread_boot b = *(thread_boot *)p; free(p);
  thread_registry_add(sceKernelGetThreadId(), "pthread");
  if (main_prio) sceKernelChangeThreadPriority(0, main_prio + 1);   // one notch BELOW main: spinning workers must never starve it
  sceKernelChangeThreadCpuAffinityMask(0, 0x70000);   // any of the 3 user cores
  return b.fn(b.arg);
}
int pthread_create_bridge(pthread_t *t, bionic_attr *a, void *(*fn)(void *), void *arg) {
  if (!main_prio) { SceKernelThreadInfo ti = { .size = sizeof ti }; if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) >= 0) main_prio = ti.currentPriority; }
  pthread_attr_t *ra = attr_get(a);
  pthread_attr_t tmp;
  if (!ra) { pthread_attr_init(&tmp); pthread_attr_setstacksize(&tmp, DEFAULT_STACK); ra = &tmp; }
  thread_boot *b = malloc(sizeof *b); b->fn = fn; b->arg = arg;
  int r = pthread_create(t, ra, thread_boot_fn, b);
  if (ra == &tmp) pthread_attr_destroy(&tmp);
  if (r) debugPrintf("pthread_create failed: %d\n", r);
  return r;
}
int pthread_join_bridge(pthread_t t, void **ret) { return pthread_join(t, ret); }
int pthread_detach_bridge(pthread_t t) { return pthread_detach(t); }
void pthread_exit_bridge(void *ret) { pthread_exit(ret); }
// vitasdk's pthread_self() returns 0 on the main thread (not created via pthread_create). Game locks use 0 as
// "unowned", so the main thread would appear to own every lock. Report a fixed non-zero id instead.
pthread_t pthread_self_bridge(void) { pthread_t t = pthread_self(); return t ? t : (pthread_t)0x4D41494E; }

// ---------- once (Bionic int slot: 0 = not run) ----------
int pthread_once_bridge(int *slot, void (*fn)(void)) {
  if (*slot == 2) return 0;
  pthread_mutex_lock(&bridge_lock);
  if (*slot != 2) { *slot = 1; pthread_mutex_unlock(&bridge_lock); fn(); pthread_mutex_lock(&bridge_lock); *slot = 2; }
  pthread_mutex_unlock(&bridge_lock);
  return 0;
}

// ---------- TLS keys (int on both sides) ----------
int pthread_key_create_bridge(pthread_key_t *k, void (*d)(void *)) { return pthread_key_create(k, d); }
int pthread_key_delete_bridge(pthread_key_t k) { return pthread_key_delete(k); }
void *pthread_getspecific_bridge(pthread_key_t k) { return pthread_getspecific(k); }
int pthread_setspecific_bridge(pthread_key_t k, const void *v) { return pthread_setspecific(k, v); }

// ---------- semaphores (Bionic sem_t is 4 bytes) ----------
static sem_t *sem_get(void **slot) {
  if (!IS_UNINIT(*slot)) return (sem_t *)*slot;
  pthread_mutex_lock(&bridge_lock);
  if (IS_UNINIT(*slot)) { sem_t *s = calloc(1, sizeof(sem_t)); sem_init(s, 0, (unsigned)(uintptr_t)*slot); *slot = s; }
  pthread_mutex_unlock(&bridge_lock);
  return (sem_t *)*slot;
}
int sem_init_bridge(void **slot, int pshared, unsigned value) { *slot = (void *)(uintptr_t)value; sem_get(slot); return 0; }
int sem_destroy_bridge(void **slot) { if (!IS_UNINIT(*slot)) { sem_destroy((sem_t *)*slot); free(*slot); *slot = NULL; } return 0; }
int sem_post_bridge(void **slot)    { return sem_post(sem_get(slot)); }
int sem_wait_bridge(void **slot)    { return sem_wait(sem_get(slot)); }
int sem_trywait_bridge(void **slot) { return sem_trywait(sem_get(slot)); }

// ---------- attr (Bionic: 24-byte struct; we use its first word as a pointer to the real attr) ----------
typedef struct { pthread_attr_t *real; int pad[5]; } bionic_attr;
static pthread_attr_t *attr_get(bionic_attr *a) {
  if (!a) return NULL;
  if (IS_UNINIT(a->real)) {
    a->real = calloc(1, sizeof(pthread_attr_t));
    pthread_attr_init(a->real);
    pthread_attr_setstacksize(a->real, DEFAULT_STACK);
  }
  return a->real;
}
int pthread_attr_init_bridge(bionic_attr *a) { memset(a, 0, sizeof(*a)); attr_get(a); return 0; }
int pthread_attr_destroy_bridge(bionic_attr *a) {
  if (a && !IS_UNINIT(a->real)) { pthread_attr_destroy(a->real); free(a->real); a->real = NULL; }
  return 0;
}
int pthread_attr_setdetachstate_bridge(bionic_attr *a, int s) { return pthread_attr_setdetachstate(attr_get(a), s ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE); }
int pthread_attr_setstacksize_bridge(bionic_attr *a, size_t n) {
  if (n < 64 * 1024) n = 64 * 1024; if (n > MAX_STACK) n = MAX_STACK;
  return pthread_attr_setstacksize(attr_get(a), n);
}
int pthread_attr_setstack_bridge(bionic_attr *a, void *base, size_t n) { return pthread_attr_setstacksize_bridge(a, n); } // ignore caller-supplied stack memory
int pthread_attr_getstack_bridge(bionic_attr *a, void **base, size_t *n) { *base = NULL; *n = DEFAULT_STACK; return 0; }
int pthread_attr_setschedpolicy_bridge(bionic_attr *a, int p) { return 0; }
int pthread_attr_setschedparam_bridge(bionic_attr *a, const void *p) { if (p) debugPrintf("thread attr sched_priority=%d\n", *(const int *)p); return 0; }
int pthread_getattr_np_bridge(pthread_t t, bionic_attr *a) { return -1; }
int pthread_getschedparam_bridge(pthread_t t, int *policy, void *param) { if (policy) *policy = 0; if (param) memset(param, 0, 4); return 0; }
int pthread_setschedparam_bridge(pthread_t t, int policy, const void *param) { if (param) debugPrintf("setschedparam policy=%d prio=%d\n", policy, *(const int *)param); return 0; }

// ---------- threads ----------
// Worker threads run one notch below the main thread and on any core;
// otherwise the Vita scheduler lets the main thread starve loader/translator threads and asset lifetimes race.
int main_thread_prio = 0;
#define main_prio main_thread_prio
// thread registry for the watchdog (names come from prctl(PR_SET_NAME) which EAThread uses)
typedef struct { SceUID uid; char name[32]; } thread_rec;
thread_rec thread_registry[64]; int thread_registry_n;
static pthread_mutex_t reg_lock = PTHREAD_MUTEX_INITIALIZER;
void thread_registry_add(SceUID uid, const char *nm) {
  pthread_mutex_lock(&reg_lock);
  for (int i = 0; i < thread_registry_n; i++) if (thread_registry[i].uid == uid) { if (nm) strncpy(thread_registry[i].name, nm, 31); pthread_mutex_unlock(&reg_lock); return; }
  if (thread_registry_n < 64) { thread_registry[thread_registry_n].uid = uid; strncpy(thread_registry[thread_registry_n].name, nm ? nm : "?", 31); thread_registry_n++; }
  pthread_mutex_unlock(&reg_lock);
}
typedef struct { void *(*fn)(void *); void *arg; } thread_boot;
static void *thread_boot_fn(void *p) {
  thread_boot b = *(thread_boot *)p; free(p);
  thread_registry_add(sceKernelGetThreadId(), "pthread");
  if (main_prio) sceKernelChangeThreadPriority(0, main_prio + 1);   // one notch BELOW main: spinning workers must never starve it
  sceKernelChangeThreadCpuAffinityMask(0, 0x70000);   // any of the 3 user cores
  return b.fn(b.arg);
}
int pthread_create_bridge(pthread_t *t, bionic_attr *a, void *(*fn)(void *), void *arg) {
  if (!main_prio) { SceKernelThreadInfo ti = { .size = sizeof ti }; if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) >= 0) main_prio = ti.currentPriority; }
  pthread_attr_t *ra = attr_get(a);
  pthread_attr_t tmp;
  if (!ra) { pthread_attr_init(&tmp); pthread_attr_setstacksize(&tmp, DEFAULT_STACK); ra = &tmp; }
  thread_boot *b = malloc(sizeof *b); b->fn = fn; b->arg = arg;
  int r = pthread_create(t, ra, thread_boot_fn, b);
  if (ra == &tmp) pthread_attr_destroy(&tmp);
  if (r) debugPrintf("pthread_create failed: %d\n", r);
  return r;
}
int pthread_join_bridge(pthread_t t, void **ret) { return pthread_join(t, ret); }
int pthread_detach_bridge(pthread_t t) { return pthread_detach(t); }
void pthread_exit_bridge(void *ret) { pthread_exit(ret); }
// vitasdk's pthread_self() returns 0 on the main thread (not created via pthread_create). Game locks use 0 as
// "unowned", so the main thread would appear to own every lock. Report a fixed non-zero id instead.
pthread_t pthread_self_bridge(void) { pthread_t t = pthread_self(); return t ? t : (pthread_t)0x4D41494E; }

// ---------- once (Bionic int slot: 0 = not run) ----------
int pthread_once_bridge(int *slot, void (*fn)(void)) {
  if (*slot == 2) return 0;
  pthread_mutex_lock(&bridge_lock);
  if (*slot != 2) { *slot = 1; pthread_mutex_unlock(&bridge_lock); fn(); pthread_mutex_lock(&bridge_lock); *slot = 2; }
  pthread_mutex_unlock(&bridge_lock);
  return 0;
}

// ---------- TLS keys (int on both sides) ----------
int pthread_key_create_bridge(pthread_key_t *k, void (*d)(void *)) { return pthread_key_create(k, d); }
int pthread_key_delete_bridge(pthread_key_t k) { return pthread_key_delete(k); }
void *pthread_getspecific_bridge(pthread_key_t k) { return pthread_getspecific(k); }
int pthread_setspecific_bridge(pthread_key_t k, const void *v) { return pthread_setspecific(k, v); }

// ---------- semaphores (Bionic sem_t is 4 bytes) ----------
static sem_t *sem_get(void **slot) {
  if (!IS_UNINIT(*slot)) return (sem_t *)*slot;
  pthread_mutex_lock(&bridge_lock);
  if (IS_UNINIT(*slot)) { sem_t *s = calloc(1, sizeof(sem_t)); sem_init(s, 0, (unsigned)(uintptr_t)*slot); *slot = s; }
  pthread_mutex_unlock(&bridge_lock);
  return (sem_t *)*slot;
}
int sem_init_bridge(void **slot, int pshared, unsigned value) { *slot = (void *)(uintptr_t)value; sem_get(slot); return 0; }
int sem_destroy_bridge(void **slot) { if (!IS_UNINIT(*slot)) { sem_destroy((sem_t *)*slot); free(*slot); *slot = NULL; } return 0; }
int sem_post_bridge(void **slot)    { return sem_post(sem_get(slot)); }
int sem_wait_bridge(void **slot)    { return sem_wait(sem_get(slot)); }
int sem_trywait_bridge(void **slot) { return sem_trywait(sem_get(slot)); }
// timed waits are native: clock_gettime is bridged to the wall clock these compare against (soloader convention)
int sem_timedwait_bridge(void **slot, const struct timespec *ts) { return sem_timedwait(sem_get(slot), ts); }
int sem_getvalue_bridge(void **slot, int *v) { return sem_getvalue(sem_get(slot), v); }

int sched_yield_bridge(void) { sceKernelDelayThread(100); return 0; }   // a real yield: DelayThread(0) does not let lower-priority threads run

