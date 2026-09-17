#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
int exec_write(Db *db, Stmt *s, char *out, usize cap){
    if(s->kind==STMT_DELETE){        int idx=find_table(db,s->table);
        if(idx<0){ rsc_debug_unknown_table(db,s,s->table); set_table_error(out,cap,s->table); return -1; }
        Table *t=&db->tables[idx];
        for(int i=0;i<s->nwhere;i++) if(find_col(t,s->where[i].col)<0){ rsc_debug_unknown_column(db,s,s->where[i].col,"where"); set_col_error(out,cap,s->where[i].col); return -1; }
        for(int i=0;i<s->nwhere;i++){ int wc=find_col(t,s->where[i].col); if(wc>=0&&t->cols[wc].type==COL_VECTOR){ set_cossim_error(out,cap,"ERR cannot compare VECTOR"); return -1; } }
        float cossim_qf[RSC_VEC_MAX_DIMS]; int cossim_qn=0; float cossim_th=0; int cossim_col=-1;
        if(s->has_cossim){
            if(cossim_prepare(db,s,t,out,cap,cossim_qf,&cossim_qn,&cossim_th,&cossim_col)!=0) return -1;
        }
        // scan and delete matching rowids
        // collect keys to delete
        u8 keys[256][8]; int nk=0;
        // custom scan to collect
        // reuse btree_scan with ctx
        ScanCtx ctx; rsc_memset(&ctx,0,sizeof(ctx)); ctx.db=db; ctx.t=t; ctx.st=s;
        // we need keys, so do manual scan
        // brute: iterate via btree_scan and eval
        // For now, do manual leaf scan collecting keys
        // Use btree_scan to get rows, but need keys - easier: iterate again
        // Collect rowids by scanning leaves directly
        // Simplified: scan via pager leaves and eval
        u64 pn=t->root;
        // go leftmost
        while(pn){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n||n->is_leaf) break; if(n->nkeys==0) break; u8 *e=(u8*)n+n->offs[0]; u16 ek=*(u16*)e; pn=*(u64*)(e+2+ek); }
        while(pn&&nk<256){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n) break; for(int i=0;i<n->nkeys;i++){ u8 *e=(u8*)n+n->offs[i]; u16 kl=*(u16*)e; u16 vl=*(u16*)(e+2); u8 *k=e+4; u8 *v=e+4+kl; int ok=1; for(int w=0;w<s->nwhere;w++) if(!eval_where(t,v,&s->where[w])){ok=0;break;} if(ok&&s->has_cossim) if(!eval_cossim_row(t,v,cossim_col,cossim_qf,cossim_qn,cossim_th)) ok=0; if(ok){ rsc_memcpy(keys[nk],k,kl); nk++; } } pn=n->next_leaf; }
        for(int i=0;i<nk;i++) btree_delete(db->pager,&t->root,keys[i],8);
        if(out&&cap){ rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    if(s->kind==STMT_UPDATE){        int idx=find_table(db,s->table);
        if(idx<0){ rsc_debug_unknown_table(db,s,s->table); set_table_error(out,cap,s->table); return -1; }
        Table *t=&db->tables[idx];
        int colidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,s->cols[0].name)==0) colidx=i;
        if(colidx<0){ rsc_debug_unknown_column(db,s,s->cols[0].name,"update"); set_col_error(out,cap,s->cols[0].name); return -1; }
        for(int i=0;i<s->nwhere;i++) if(find_col(t,s->where[i].col)<0){ rsc_debug_unknown_column(db,s,s->where[i].col,"where"); set_col_error(out,cap,s->where[i].col); return -1; }
        for(int i=0;i<s->nwhere;i++){ int wc=find_col(t,s->where[i].col); if(wc>=0&&t->cols[wc].type==COL_VECTOR){ set_cossim_error(out,cap,"ERR cannot compare VECTOR"); return -1; } }
        float cossim_qf[RSC_VEC_MAX_DIMS]; int cossim_qn=0; float cossim_th=0; int cossim_col=-1;
        if(s->has_cossim){
            if(cossim_prepare(db,s,t,out,cap,cossim_qf,&cossim_qn,&cossim_th,&cossim_col)!=0) return -1;
        }
        extern void rsc_debug_constraint(Db *db, Stmt *s, const char *col, const char *errmsg);
        int new_is_null=0; char new_val[RSC_VEC_MAX]={0};
        if(s->vals_is_default[0]){
            if(t->cols[colidx].has_default){
                if(t->cols[colidx].default_is_null) new_is_null=1;
                else rsc_strcpy(new_val,t->cols[colidx].default_val);
            } else { set_default_error(out,cap,t->cols[colidx].name); rsc_debug_constraint(db,s,t->cols[colidx].name,out); return -1; }
        } else if(s->vals_is_null[0]){
            new_is_null=1;
        } else {
            rsc_strcpy(new_val,s->vals[0]);
        }
        if(new_is_null && (t->cols[colidx].is_not_null || t->cols[colidx].is_pk)){
            set_notnull_error(out,cap,t->cols[colidx].name);
            rsc_debug_constraint(db,s,t->cols[colidx].name,out);
            return -1;
        }
        // similar scan
        u8 keys[256][8]; u8 rows[256][1024]; u16 rls[256]; int nk=0;
        u64 pn=t->root;
        while(pn){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n||n->is_leaf) break; if(n->nkeys==0) break; u8 *e=(u8*)n+n->offs[0]; u16 ek=*(u16*)e; pn=*(u64*)(e+2+ek); }
        while(pn&&nk<256){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n) break; for(int i=0;i<n->nkeys;i++){ u8 *e=(u8*)n+n->offs[i]; u16 kl=*(u16*)e; u16 vl=*(u16*)(e+2); u8 *k=e+4; u8 *v=e+4+kl; int ok=1; for(int w=0;w<s->nwhere;w++) if(!eval_where(t,v,&s->where[w])){ok=0;break;} if(ok&&s->has_cossim) if(!eval_cossim_row(t,v,cossim_col,cossim_qf,cossim_qn,cossim_th)) ok=0; if(ok){ rsc_memcpy(keys[nk],k,kl); rsc_memcpy(rows[nk],v,vl); rls[nk]=vl; nk++; } } pn=n->next_leaf; }
        if(!new_is_null && (colidx==t->pk_col || t->cols[colidx].is_unique) && nk>0){
            if(nk>1){
                if(colidx==t->pk_col){
                    extern void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
                    rsc_debug_duplicate_pk(db,s,new_val);
                    set_pk_error(out,cap,new_val);
                } else {
                    rsc_debug_constraint(db,s,t->cols[colidx].name,"duplicate UNIQUE value");
                    set_unique_error(out,cap,t->cols[colidx].name,new_val);
                }
                return -1;
            }
            // nk==1: check if new value differs from current and already exists elsewhere
            {
                int cur_null=row_is_null(t,rows[0],colidx);
                int same=0;
                if(cur_null){
                    same=0;
                } else {
                    char curval[64]={0}; row_cell_str(t,rows[0],colidx,curval,0);
                    if(t->cols[colidx].type==COL_INT) same=(parse_int_val(curval)==parse_int_val(new_val));
                    else same=(rsc_strcmp(curval,new_val)==0);
                }
                if(!same && col_value_exists(db,t,colidx,new_val)){
                    // make sure the existing row isn't the one being updated (same value already handled)
                    // col_value_exists finds any row incl. current; since current differs, it's a real dup
                    if(colidx==t->pk_col){
                        extern void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
                        rsc_debug_duplicate_pk(db,s,new_val);
                        set_pk_error(out,cap,new_val);
                    } else {
                        rsc_debug_constraint(db,s,t->cols[colidx].name,"duplicate UNIQUE value");
                        set_unique_error(out,cap,t->cols[colidx].name,new_val);
                    }
                    return -1;
                }
            }
        }
        for(int i=0;i<nk;i++){
            u8 newrow[1024]; int nlen=0;
            if(row_replace_col(t,rows[i],rls[i],colidx,new_is_null?"":new_val,new_is_null,newrow,&nlen)!=0){
                if(t->cols[colidx].type==COL_VECTOR){
                    set_vector_error(out,cap,t->cols[colidx].name);
                    rsc_debug_constraint(db,s,t->cols[colidx].name,out);
                }
                return -1;
            }
            btree_insert(db->pager,&t->root,keys[i],8,newrow,nlen);
        }
        if(out&&cap){ rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    (void)db; (void)s; (void)out; (void)cap;
    return -2;
}
