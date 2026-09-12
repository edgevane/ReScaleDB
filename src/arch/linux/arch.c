#include "../arch.h"
#include "syscall.h"
#if defined(__aarch64__)
#define SYS_openat 56
#define SYS_close 57
#define SYS_mmap 222
#define SYS_munmap 215
#define SYS_msync 227
#define SYS_ftruncate 46
#define SYS_lseek 62
#define SYS_read 63
#define SYS_write 64
#define SYS_unlinkat 35
#else
#define SYS_openat 257
#define SYS_close 3
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_msync 26
#define SYS_ftruncate 77
#define SYS_lseek 8
#define SYS_read 0
#define SYS_write 1
#define SYS_unlinkat 263
#endif
#define AT_FDCWD -100
void *arch_mmap(void *addr, usize len, int prot, int flags, int fd, u64 off){
    long ret=arch_syscall6(SYS_mmap,(long)addr,(long)len,(long)prot,(long)flags,(long)fd,(long)off);
    if(ret<0&&ret>-4096) return 0;
    return (void*)ret;
}
int arch_munmap(void *addr, usize len){return (int)arch_syscall3(SYS_munmap,(long)addr,(long)len,0);}
int arch_msync(void *addr, usize len, int flags){return (int)arch_syscall3(SYS_msync,(long)addr,(long)len,flags);}
int arch_open(const char *path,int flags,int mode){long ret=arch_syscall4(SYS_openat,AT_FDCWD,(long)path,flags,mode); if(ret<0&&ret>-4096) return (int)ret; return (int)ret;}
int arch_close(int fd){return (int)arch_syscall3(SYS_close,fd,0,0);}
int arch_ftruncate(int fd,u64 len){return (int)arch_syscall3(SYS_ftruncate,fd,(long)len,0);}
i64 arch_lseek(int fd,i64 off,int whence){return arch_syscall4(SYS_lseek,fd,(long)off,whence,0);}
i64 arch_read(int fd,void *buf,usize n){return arch_syscall3(SYS_read,fd,(long)buf,(long)n);}
i64 arch_write(int fd,const void *buf,usize n){return arch_syscall3(SYS_write,fd,(long)buf,(long)n);}
int arch_unlink(const char *path){ long ret=arch_syscall4(SYS_unlinkat,AT_FDCWD,(long)path,0,0); if(ret<0&&ret>-4096) return -1; return 0; }
