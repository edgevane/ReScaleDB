#pragma once
#include "pager.h"
#define BTREE_MAX_KEY 256
#define BTREE_MAX_VAL 1024
typedef struct {
    u8 is_leaf;
    u16 nkeys;
    u16 free_off;
    u64 next_leaf;
    u16 offs[256];
} BNode;
int btree_create(Pager *p, u64 *out_root);
int btree_search(Pager *p, u64 root, const void *key, u16 klen, void *out_val, u16 *out_vlen);
int btree_insert(Pager *p, u64 *root, const void *key, u16 klen, const void *val, u16 vlen);
int btree_delete(Pager *p, u64 *root, const void *key, u16 klen);
int btree_scan(Pager *p, u64 root, void (*cb)(const void*k,u16 kl,const void*v,u16 vl,void*ctx), void *ctx);
u64 btree_new_root_cow(Pager *p, u64 old_root);
