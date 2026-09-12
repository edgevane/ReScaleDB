#include "txn.h"
#include "btree.h"
int txn_begin_read(Pager *p, Txn *t){
    t->pager=p; t->root=p->hdr->root_pageno; t->orig_root=t->root; t->is_write=0; t->active=1; return 0;
}
int txn_begin_write(Pager *p, Txn *t){
    t->pager=p; t->orig_root=p->hdr->root_pageno; t->root=t->orig_root; t->is_write=1; t->active=1;
    if(t->root==0){ u64 nr; if(btree_create(p,&nr)==0) t->root=nr; }
    return 0;
}
int txn_commit(Txn *t){
    if(!t->active) return -1;
    if(t->is_write){
        t->pager->hdr->root_pageno=t->root;
        t->pager->hdr->txn_id++;
        pager_sync(t->pager);
    }
    t->active=0; return 0;
}
int txn_abort(Txn *t){ if(!t->active) return -1; t->active=0; return 0; }
