#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <wchar.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#define DATA_PATH "uma0:data/ssx"
void debugPrintf(const char*fmt,...);
void log_vprintf(const char*tag,const char*fmt,va_list a);
extern FILE __sF_fake[3];
extern int __page_size;
extern unsigned __stack_chk_guard;
extern long timezone;
extern char*tzname[2];
int fork(void);
int execv(const char*p,char*const a[]);
int system(const char*c);
int waitpid(int p,int*s,int o);
int prctl(int o,...);
long syscall(long n,...);
void*mmap(void*a,size_t l,int p,int f,int fd,long off);
int munmap(void*a,size_t l);
int socket(int d,int t,int p);
int connect(int s,const void*a,unsigned l);
int bind(int s,const void*a,unsigned l);
int listen(int s,int b);
int accept(int s,void*a,unsigned*l);
int send(int s,const void*b,size_t l,int f);
int sendto(int s,const void*b,size_t l,int f,const void*a,unsigned al);
int recv(int s,void*b,size_t l,int f);
int recvfrom(int s,void*b,size_t l,int f,void*a,unsigned*al);
int shutdown(int s,int h);
int setsockopt(int s,int l,int o,const void*v,unsigned n);
int getsockopt(int s,int l,int o,void*v,unsigned*n);
int getsockname(int s,void*a,unsigned*l);
int getpeername(int s,void*a,unsigned*l);
int poll(void*f,unsigned n,int t);
int ioctl(int f,unsigned long r,...);
int getaddrinfo(const char*n,const char*s,const void*h,void**r);
void freeaddrinfo(void*a);
void*gethostbyname(const char*n);
int gethostname(char*n,size_t l);
unsigned inet_addr(const char*c);
char*inet_ntoa(unsigned a);
int _Unwind_Backtrace(void*f,void*a);
int _Unwind_VRS_Get(void*c,int r,unsigned i,int t,void*o);
void __aeabi_unwind_cpp_pr0(void);
void __aeabi_unwind_cpp_pr1(void);
int __android_log_print(int p,const char*t,const char*f,...);
int __android_log_vprint(int p,const char*t,const char*f,va_list a);
int __android_log_write(int p,const char*t,const char*m);
void __assert2(const char*f,int l,const char*fn,const char*e);
int __cxa_guard_acquire(int*g);
void __cxa_guard_release(int*g);
void __cxa_pure_virtual(void);
void __stack_chk_fail(void);
int __srget(FILE*f);
int __isinf(double d);
char*setlocale(int c,const char*l);
char*tmpnam(char*s);
int statfs(const char*p,void*b);
long sysconf(int n);
ssize_t readlink(const char*p,char*b,size_t l);
int chmod(const char*p,unsigned m);
int utime(const char*p,const void*t);
int fsync(int f);
int fwide(FILE*f,int m);
int strcoll(const char*a,const char*b);
int wcscoll(const wchar_t*a,const wchar_t*b);
extern char*tzname[2];
void tzset(void);
char*getenv(const char*n);
int setenv(const char*n,const char*v,int o);
int unsetenv(const char*n);
clock_t clock(void);
char*getcwd(char*b,size_t l);
int chdir(const char*p);
int ftruncate(int f,long l);
void _ZN2EA6Nimble8Tracking8Tracking10setEnabledEb(void*t,int b);
void*_ZN2EA6Nimble8Tracking8Tracking12getComponentEv(void);
void _ZN2EA6Nimble8Tracking8Tracking8logEventESsRKSt3mapISsSsSt4lessISsESaISt4pairIKSsSsEEE(void*t,void*n,void*m);
int _ZN2EA6Nimble8Tracking8Tracking9isEnabledEv(void*t);
