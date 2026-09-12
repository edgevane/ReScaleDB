#pragma once
#include "../../libs/types.h"
static inline long arch_syscall6(long n,long a1,long a2,long a3,long a4,long a5,long a6){
    long ret;
    register long r10 asm("r10")=a4;
    register long r8 asm("r8")=a5;
    register long r9 asm("r9")=a6;
    asm volatile("syscall" : "=a"(ret) : "a"(n),"D"(a1),"S"(a2),"d"(a3),"r"(r10),"r"(r8),"r"(r9) : "rcx","r11","memory");
    return ret;
}
static inline long arch_syscall3(long n,long a1,long a2,long a3){return arch_syscall6(n,a1,a2,a3,0,0,0);}
static inline long arch_syscall4(long n,long a1,long a2,long a3,long a4){return arch_syscall6(n,a1,a2,a3,a4,0,0);}
