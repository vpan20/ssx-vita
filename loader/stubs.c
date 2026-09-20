// Overrides for Android/Bionic symbols that have no Vita equivalent or must be neutered (network, fork, DRM).
#include <vitasdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <malloc.h>
#include <wchar.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <unistd.h>
#include "stubs.h"

void debugPrintf(const char*fmt,...){va_list a;va_start(a,fmt);char b[1024];int n=vsnprintf(b,sizeof b,fmt,a);va_end(a);if(n<=0)return;
  SceUID fd=sceIoOpen(DATA_PATH"/ssx.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0777);
  if(fd>=0){sceIoWrite(fd,b,n);sceIoClose(fd);}
  sceClibPrintf("%s",b);}
void log_vprintf(const char*tag,const char*fmt,va_list a){if(!fmt)return;char b[1024];vsnprintf(b,sizeof b,fmt,a);debugPrintf("[%s] %s\n",tag,b);}

int fork(void){return -1;}
int execv(const char*p,char*const a[]){return -1;}
int system(const char*c){return -1;}
int waitpid(int p,int*s,int o){return -1;}
void thread_registry_add(int uid, const char *nm);
int prctl(int o,...){ if (o == 15) { va_list a; va_start(a, o); const char *nm = va_arg(a, const char *); va_end(a); thread_registry_add(sceKernelGetThreadId(), nm); debugPrintf("thread name: %s\n", nm ? nm : "?"); } return 0; }
long syscall(long n,...){return -1;}
void*mmap(void*a,size_t l,int p,int f,int fd,long off){void*m=memalign(0x1000,l);if(m)memset(m,0,l);debugPrintf("mmap(%u) -> %p\n",(unsigned)l,m);return m?m:(void*)-1;}
int munmap(void*a,size_t l){free(a);return 0;}
int socket(int d,int t,int p){errno=EACCES;return -1;}
int connect(int s,const void*a,unsigned l){return -1;}
int bind(int s,const void*a,unsigned l){return -1;}
int listen(int s,int b){return -1;}
int accept(int s,void*a,unsigned*l){return -1;}
int send(int s,const void*b,size_t l,int f){return -1;}
int sendto(int s,const void*b,size_t l,int f,const void*a,unsigned al){return -1;}
int recv(int s,void*b,size_t l,int f){return -1;}
int recvfrom(int s,void*b,size_t l,int f,void*a,unsigned*al){return -1;}
int shutdown(int s,int h){return -1;}
int setsockopt(int s,int l,int o,const void*v,unsigned n){return -1;}
int getsockopt(int s,int l,int o,void*v,unsigned*n){return -1;}
int getsockname(int s,void*a,unsigned*l){return -1;}
int getpeername(int s,void*a,unsigned*l){return -1;}
int poll(void*f,unsigned n,int t){return 0;}
int ioctl(int f,unsigned long r,...){return -1;}
int getaddrinfo(const char*n,const char*s,const void*h,void**r){return -2;}
void freeaddrinfo(void*a){}
void*gethostbyname(const char*n){return NULL;}
int gethostname(char*n,size_t l){strncpy(n,"vita",l);return 0;}
unsigned inet_addr(const char*c){return 0xffffffff;}
char*inet_ntoa(unsigned a){return (char*)"0.0.0.0";}
int _Unwind_Backtrace(void*f,void*a){return 0;}
int _Unwind_VRS_Get(void*c,int r,unsigned i,int t,void*o){return 0;}
void __aeabi_unwind_cpp_pr0(void){}
void __aeabi_unwind_cpp_pr1(void){}
int __android_log_print(int p,const char*t,const char*f,...){va_list a;va_start(a,f);log_vprintf(t,f,a);va_end(a);return 0;}
int __android_log_vprint(int p,const char*t,const char*f,va_list a){log_vprintf(t,f,a);return 0;}
int __android_log_write(int p,const char*t,const char*m){debugPrintf("[%s] %s\n",t,m);return 0;}
void __assert2(const char*f,int l,const char*fn,const char*e){debugPrintf("ASSERT %s:%d %s: %s\n",f,l,fn,e);abort();}
int __cxa_guard_acquire(int*g){return !*(char*)g;}
void __cxa_guard_release(int*g){*(char*)g=1;}
void __cxa_pure_virtual(void){debugPrintf("pure virtual call\n");abort();}
int __page_size=0x1000;
void __stack_chk_fail(void){debugPrintf("stack smash\n");abort();}
unsigned __stack_chk_guard=0x42;
FILE __sF_fake[3];
int __srget(FILE*f){return EOF;}
int __isinf(double d){return isinf(d);}
char*setlocale(int c,const char*l){return (char*)"C";}
char*tmpnam(char*s){return NULL;}
int statfs(const char*p,void*b){return -1;}
long sysconf(int n){return n==84?4:(n==30?0x1000:-1);}
ssize_t readlink(const char*p,char*b,size_t l){return -1;}
int chmod(const char*p,unsigned m){return 0;}
int utime(const char*p,const void*t){return 0;}
int fsync(int f){return 0;}
int fwide(FILE*f,int m){return 0;}
int strcoll(const char*a,const char*b){return strcmp(a,b);}
int wcscoll(const wchar_t*a,const wchar_t*b){return wcscmp(a,b);}
long timezone=0;
char*tzname[2]={"UTC","UTC"};
void tzset(void){}
char*getenv(const char*n){return NULL;}
int setenv(const char*n,const char*v,int o){return 0;}
int unsetenv(const char*n){return 0;}
clock_t clock(void){return sceKernelGetProcessTimeWide();}
char*getcwd(char*b,size_t l){strncpy(b,DATA_PATH,l);return b;}
int chdir(const char*p){return 0;}
int ftruncate(int f,long l){return 0;}
void _ZN2EA6Nimble8Tracking8Tracking10setEnabledEb(void*t,int b){}
void*_ZN2EA6Nimble8Tracking8Tracking12getComponentEv(void){static char dummy[256];return dummy;}
void _ZN2EA6Nimble8Tracking8Tracking8logEventESsRKSt3mapISsSsSt4lessISsESaISt4pairIKSsSsEEE(void*t,void*n,void*m){}
int _ZN2EA6Nimble8Tracking8Tracking9isEnabledEv(void*t){return 0;}
