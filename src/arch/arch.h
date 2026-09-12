#pragma once
#include "../libs/types.h"
#define ARCH_PROT_READ  0x1
#define ARCH_PROT_WRITE 0x2
#define ARCH_MAP_SHARED  0x01
#define ARCH_MAP_PRIVATE 0x02
#define ARCH_MS_SYNC  0x4
#define ARCH_MS_ASYNC 0x1
void *arch_mmap(void *addr, usize len, int prot, int flags, int fd, u64 off);
int arch_munmap(void *addr, usize len);
int arch_msync(void *addr, usize len, int flags);
int arch_open(const char *path, int flags, int mode);
int arch_close(int fd);
int arch_ftruncate(int fd, u64 len);
i64 arch_lseek(int fd, i64 off, int whence);
i64 arch_read(int fd, void *buf, usize n);
i64 arch_write(int fd, const void *buf, usize n);
int arch_unlink(const char *path);
#define ARCH_O_RDONLY 0
#define ARCH_O_WRONLY 1
#define ARCH_O_RDWR 2
#define ARCH_O_CREAT 0100
#define ARCH_O_TRUNC 01000
