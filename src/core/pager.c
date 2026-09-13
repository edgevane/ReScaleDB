#include "pager.h"
#include "../arch/arch.h"
#include "../libs/string.h"
int pager_open(Pager *p, const char *path){
    rsc_memset(p,0,sizeof(*p));
    int fd=arch_open(path, ARCH_O_RDWR|ARCH_O_CREAT, 0644);
    if(fd<0) return -1;
    p->fd=fd;
    i64 sz=arch_lseek(fd,0,2);
    if(sz<0) sz=0;
    if(sz==0){
        arch_ftruncate(fd, RSC_PAGE_SIZE*4);
        sz=RSC_PAGE_SIZE*4;
    }
    if(sz < RSC_PAGE_SIZE) sz=RSC_PAGE_SIZE;
    p->map_len=(usize)sz;
    void *m=arch_mmap(0, p->map_len, ARCH_PROT_READ|ARCH_PROT_WRITE, ARCH_MAP_SHARED, fd, 0);
    if(!m){arch_close(fd);return -1;}
    p->map=(u8*)m;
    p->hdr=(RscHeader*)m;
    if(p->hdr->magic!=RSC_MAGIC){
        rsc_memset(p->hdr,0,RSC_PAGE_SIZE);
        p->hdr->magic=RSC_MAGIC;
        p->hdr->version=RSC_VERSION;
        p->hdr->page_size=RSC_PAGE_SIZE;
        p->hdr->page_count=(u32)(p->map_len/RSC_PAGE_SIZE);
        p->hdr->root_pageno=0;
        p->hdr->freelist_head=0;
        p->hdr->txn_id=1;
        for(u32 i=1;i<p->hdr->page_count;i++) pager_free(p,i);
        pager_sync(p);
    }
    return 0;
}
int pager_open_mem(Pager *p, usize pages){
    rsc_memset(p,0,sizeof(*p));
    p->fd=-1;
    if(pages<4) pages=4;
    p->map_len=pages*RSC_PAGE_SIZE;
    void *m=arch_mmap(0,p->map_len,ARCH_PROT_READ|ARCH_PROT_WRITE,ARCH_MAP_PRIVATE|ARCH_MAP_ANON,-1,0);
    if(!m) return -1;
    p->map=(u8*)m;
    p->hdr=(RscHeader*)m;
    rsc_memset(p->hdr,0,RSC_PAGE_SIZE);
    p->hdr->magic=RSC_MAGIC;
    p->hdr->version=RSC_VERSION;
    p->hdr->page_size=RSC_PAGE_SIZE;
    p->hdr->page_count=(u32)pages;
    p->hdr->root_pageno=0;
    p->hdr->freelist_head=0;
    p->hdr->txn_id=1;
    for(u32 i=1;i<p->hdr->page_count;i++) pager_free(p,i);
    return 0;
}
int pager_close(Pager *p){
    if(p->map && p->fd>=0) pager_sync(p);
    if(p->map) arch_munmap(p->map,p->map_len);
    if(p->fd>=0) arch_close(p->fd);
    rsc_memset(p,0,sizeof(*p));
    return 0;
}
void *pager_get(Pager *p, u64 pageno){
    if(pageno>=p->hdr->page_count) return 0;
    return p->map + pageno*RSC_PAGE_SIZE;
}
u64 pager_alloc(Pager *p){
    if(p->hdr->freelist_head!=0){
        u64 pn=p->hdr->freelist_head;
        u64 *next=(u64*)pager_get(p,pn);
        p->hdr->freelist_head=*next;
        rsc_memset(next,0,RSC_PAGE_SIZE);
        return pn;
    }
    usize old=p->map_len;
    usize np=p->hdr->page_count+1;
    usize nlen=np*RSC_PAGE_SIZE;
    if(p->fd>=0){
        if(arch_ftruncate(p->fd,nlen)<0) return 0;
        arch_munmap(p->map,old);
        void *m=arch_mmap(0,nlen,ARCH_PROT_READ|ARCH_PROT_WRITE,ARCH_MAP_SHARED,p->fd,0);
        if(!m) return 0;
        p->map=(u8*)m;
    } else {
        void *m=arch_mmap(0,nlen,ARCH_PROT_READ|ARCH_PROT_WRITE,ARCH_MAP_PRIVATE|ARCH_MAP_ANON,-1,0);
        if(!m) return 0;
        rsc_memcpy(m,p->map,old);
        arch_munmap(p->map,old);
        p->map=(u8*)m;
    }
    p->map_len=nlen;
    p->hdr=(RscHeader*)p->map;
    p->hdr->page_count=(u32)np;
    rsc_memset(p->map+(np-1)*RSC_PAGE_SIZE,0,RSC_PAGE_SIZE);
    return np-1;
}
void pager_free(Pager *p, u64 pageno){
    if(pageno==0) return;
    u64 *slot=(u64*)pager_get(p,pageno);
    *slot=p->hdr->freelist_head;
    p->hdr->freelist_head=pageno;
}
int pager_sync(Pager *p){ if(p->fd<0) return 0; return arch_msync(p->map,p->map_len,ARCH_MS_SYNC);}
int pager_grow(Pager *p, usize new_pages){
    usize np=p->hdr->page_count+new_pages;
    usize nlen=np*RSC_PAGE_SIZE;
    if(p->fd>=0){
        if(arch_ftruncate(p->fd,nlen)<0) return -1;
        arch_munmap(p->map,p->map_len);
        void *m=arch_mmap(0,nlen,ARCH_PROT_READ|ARCH_PROT_WRITE,ARCH_MAP_SHARED,p->fd,0);
        if(!m) return -1;
        p->map=(u8*)m;
    } else {
        void *m=arch_mmap(0,nlen,ARCH_PROT_READ|ARCH_PROT_WRITE,ARCH_MAP_PRIVATE|ARCH_MAP_ANON,-1,0);
        if(!m) return -1;
        rsc_memcpy(m,p->map,p->map_len);
        arch_munmap(p->map,p->map_len);
        p->map=(u8*)m;
    }
    p->map_len=nlen;
    for(usize i=p->hdr->page_count;i<np;i++){rsc_memset(p->map+i*RSC_PAGE_SIZE,0,RSC_PAGE_SIZE);pager_free(p,i);}
    p->hdr->page_count=(u32)np;
    return 0;
}
