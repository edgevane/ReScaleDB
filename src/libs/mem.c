#include "mem.h"
#include "string.h"
static u8 *heap_base=0;
static usize heap_cap=0;
static usize heap_off=0;
void rsc_alloc_init(void *base, usize cap){heap_base=(u8*)base;heap_cap=cap;heap_off=0;}
void *rsc_alloc(usize n){
    n=(n+7)&~7ULL;
    if(heap_off+n>heap_cap) return 0;
    void *p=heap_base+heap_off;
    heap_off+=n;
    return p;
}
void rsc_free(void *p){(void)p;}
void *rsc_realloc(void *p, usize n){
    if(!p) return rsc_alloc(n);
    void *np=rsc_alloc(n);
    if(np) rsc_memcpy(np,p,n);
    return np;
}
