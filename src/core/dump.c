#include "dump.h"
#include "../arch/arch.h"
#include "../arch/linux/syscall.h"
#include "../libs/string.h"
#define DENTS_BUF 8192
struct linux_dirent64 { u64 ino; i64 off; u16 reclen; u8 type; char name[]; };
#if defined(__aarch64__)
#define SYS_getdents64 61
#else
#define SYS_getdents64 217
#endif
static long arch_getdents64(int fd, void *buf, usize n){
    return arch_syscall3(SYS_getdents64,fd,(long)buf,(long)n);
}
static int is_rsc_db(const char *name){
    usize l=rsc_strlen(name);
    if(l<7) return 0;
    return rsc_strcmp(name+l-7,".rsc.db")==0;
}
static int copy_file(const char *src, const char *dst){
    int f1=arch_open(src,ARCH_O_RDONLY,0);
    if(f1<0) return -1;
    int f2=arch_open(dst,ARCH_O_RDWR|ARCH_O_CREAT|ARCH_O_TRUNC,0644);
    if(f2<0){ arch_close(f1); return -1; }
    char buf[4096]; i64 r;
    while((r=arch_read(f1,buf,4096))>0) if(arch_write(f2,buf,(usize)r)!=r) break;
    arch_close(f1); arch_close(f2);
    return 0;
}
int rsc_dump_db(const char *db_path, const char *dump_path){ return copy_file(db_path,dump_path); }
int rsc_load_db(const char *dump_path, const char *db_path){ return copy_file(dump_path,db_path); }
int pager_save(Pager *p, const char *path){
    int fd=arch_open(path,ARCH_O_RDWR|ARCH_O_CREAT|ARCH_O_TRUNC,0644);
    if(fd<0) return -1;
    usize left=p->map_len; u8 *ptr=p->map;
    while(left>0){
        usize w= left>4096?4096:left;
        i64 n=arch_write(fd,ptr,w);
        if(n<=0) break;
        ptr+=n; left-=n;
    }
    arch_close(fd);
    return 0;
}
int pager_load(Pager *p, const char *path){
    int fd=arch_open(path,ARCH_O_RDONLY,0);
    if(fd<0) return -1;
    i64 sz=arch_lseek(fd,0,2); arch_lseek(fd,0,0);
    if(sz<=0){ arch_close(fd); return -1; }
    usize need=(usize)sz;
    if(need>p->map_len){
        arch_munmap(p->map,p->map_len);
        void *m=arch_mmap(0,need,ARCH_PROT_READ|ARCH_PROT_WRITE,ARCH_MAP_PRIVATE|ARCH_MAP_ANON,-1,0);
        if(!m){ arch_close(fd); return -1; }
        p->map=(u8*)m; p->map_len=need; p->hdr=(RscHeader*)m;
    }
    usize off=0;
    while(off<need){
        i64 r=arch_read(fd,p->map+off,need-off);
        if(r<=0) break;
        off+=r;
    }
    p->hdr->page_count=(u32)(need/RSC_PAGE_SIZE);
    arch_close(fd);
    return 0;
}
int rsc_dump_all(const char *dump_path){
    int out=arch_open(dump_path,ARCH_O_RDWR|ARCH_O_CREAT|ARCH_O_TRUNC,0644);
    if(out<0) return -1;
    int dfd=arch_open(".",ARCH_O_RDONLY,0);
    if(dfd<0){ arch_close(out); return -1; }
    char buf[DENTS_BUF];
    char names[64][128]; int n=0;
    i64 sz;
    while((sz=arch_getdents64(dfd,buf,DENTS_BUF))>0){
        usize off=0;
        while(off<(usize)sz){
            struct linux_dirent64 *d=(void*)(buf+off);
            if(d->type==8 && is_rsc_db(d->name) && n<64){
                rsc_strcpy(names[n++],d->name);
            }
            off+=d->reclen;
        }
    }
    arch_close(dfd);
    u32 cnt=(u32)n;
    arch_write(out,&cnt,4);
    for(int i=0;i<n;i++){
        u32 nl=(u32)rsc_strlen(names[i]);
        arch_write(out,&nl,4);
        arch_write(out,names[i],nl);
        int fd=arch_open(names[i],ARCH_O_RDONLY,0);
        u64 fsz=0;
        if(fd>=0){ fsz=(u64)arch_lseek(fd,0,2); arch_lseek(fd,0,0); }
        arch_write(out,&fsz,8);
        if(fd>=0){
            char b[4096]; i64 r;
            while((r=arch_read(fd,b,4096))>0) arch_write(out,b,(usize)r);
            arch_close(fd);
        }
    }
    arch_close(out);
    return 0;
}
int rsc_load_all(const char *dump_path){
    int fd=arch_open(dump_path,ARCH_O_RDONLY,0);
    if(fd<0) return -1;
    u32 cnt=0; if(arch_read(fd,&cnt,4)!=4){ arch_close(fd); return -1; }
    for(u32 i=0;i<cnt;i++){
        u32 nl=0; if(arch_read(fd,&nl,4)!=4) break;
        if(nl>=128) break;
        char name[128]={0}; if(arch_read(fd,name,nl)!=(i64)nl) break;
        u64 fsz=0; if(arch_read(fd,&fsz,8)!=8) break;
        int out=arch_open(name,ARCH_O_RDWR|ARCH_O_CREAT|ARCH_O_TRUNC,0644);
        if(out<0){
            char tmp[4096]; i64 skip=(i64)fsz; while(skip>0){ i64 to= skip>4096?4096:skip; i64 r=arch_read(fd,tmp,(usize)to); if(r<=0) break; skip-=r; }
            continue;
        }
        u64 left=fsz; char b[4096];
        while(left>0){
            usize want= left>4096?4096:(usize)left;
            i64 r=arch_read(fd,b,want); if(r<=0) break;
            arch_write(out,b,(usize)r); left-=r;
        }
        arch_close(out);
    }
    arch_close(fd);
    return 0;
}
