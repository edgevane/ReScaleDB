#pragma once
#include "../libs/types.h"
#define RSC_PAGE_SIZE 4096
#define RSC_MAGIC 0x52534344u
#define RSC_VERSION 2u
typedef struct {
    u32 magic;
    u32 version;
    u32 page_size;
    u32 page_count;
    u64 root_pageno;
    u64 freelist_head;
    u64 txn_id;
    u64 db_size;
    u8 _pad[RSC_PAGE_SIZE - 40];
} RscHeader;
typedef struct {
    int fd;
    u8 *map;
    usize map_len;
    RscHeader *hdr;
} Pager;
int pager_open(Pager *p, const char *path);
int pager_open_mem(Pager *p, usize pages);
int pager_close(Pager *p);
void *pager_get(Pager *p, u64 pageno);
u64 pager_alloc(Pager *p);
void pager_free(Pager *p, u64 pageno);
int pager_sync(Pager *p);
int pager_grow(Pager *p, usize new_pages);
