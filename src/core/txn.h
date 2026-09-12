#pragma once
#include "pager.h"
typedef struct {
    Pager *pager;
    u64 root;
    u64 orig_root;
    int is_write;
    int active;
} Txn;
int txn_begin_read(Pager *p, Txn *t);
int txn_begin_write(Pager *p, Txn *t);
int txn_commit(Txn *t);
int txn_abort(Txn *t);
