#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
int exec_insert(Db *db, Stmt *s, char *out, usize cap){
        int idx=find_table(db,s->table);
        if(idx<0){ rsc_debug_unknown_table(db,s,s->table); set_table_error(out,cap,s->table); return -1; }
        Table *t=&db->tables[idx];
        if(s->nvals != t->ncols){ if(out&&cap) rsc_strcpy(out,"ERR column count\n"); return -1; }
        char fvals[16][RSC_VEC_MAX]; int fis_null[16]={0};
        extern void rsc_debug_constraint(Db *db, Stmt *s, const char *col, const char *errmsg);
        for(int i=0;i<t->ncols;i++){
            int is_null=0;
            char tmp[RSC_VEC_MAX]={0};
            if(s->vals_is_default[i]){
                if(t->cols[i].has_default){
                    if(t->cols[i].default_is_null) is_null=1;
                    else rsc_strcpy(tmp,t->cols[i].default_val);
                } else { set_default_error(out,cap,t->cols[i].name); rsc_debug_constraint(db,s,t->cols[i].name,out); return -1; }
            } else if(s->vals_is_null[i]){
                is_null=1;
            } else {
                rsc_strcpy(tmp,s->vals[i]);
            }
            if(is_null){
                if(t->cols[i].is_not_null || t->cols[i].is_pk){
                    set_notnull_error(out,cap,t->cols[i].name);
                    rsc_debug_constraint(db,s,t->cols[i].name,out);
                    return -1;
                }
                fis_null[i]=1; fvals[i][0]=0;
            } else {
                fis_null[i]=0; rsc_strcpy(fvals[i],tmp);
            }
        }
        for(int i=0;i<t->ncols;i++){
            if(fis_null[i]) continue;
            if(t->cols[i].is_pk){
                if(pk_value_exists(db,t,fvals[i])){
                    extern void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
                    rsc_debug_duplicate_pk(db,s,fvals[i]);
                    set_pk_error(out,cap,fvals[i]);
                    return -1;
                }
            } else if(t->cols[i].is_unique){
                if(col_value_exists(db,t,i,fvals[i])){
                    rsc_debug_constraint(db,s,t->cols[i].name,"duplicate UNIQUE value");
                    set_unique_error(out,cap,t->cols[i].name,fvals[i]);
                    return -1;
                }
            }
        }
        u8 row[1024]; int off=0;
        if(t->has_nullmap){
            if(encode_row_new(t,fvals,fis_null,row,&off)!=0){
                const char *bad=t->cols[0].name;
                for(int i=0;i<t->ncols;i++) if(t->cols[i].type==COL_VECTOR&&!fis_null[i]){ bad=t->cols[i].name; break; }
                set_vector_error(out,cap,bad);
                rsc_debug_constraint(db,s,bad,out);
                return -1;
            }
        } else {
            for(int i=0;i<t->ncols;i++){
                if(t->cols[i].type==COL_INT){
                    long v=parse_int_val(fvals[i]);
                    for(int k=0;k<8;k++) row[off++]= (u8)((v>>(k*8))&0xFF);
                } else {
                    usize l=rsc_strlen(fvals[i]); if(l>255) l=255; *(u16*)(row+off)=(u16)l; off+=2; rsc_memcpy(row+off,fvals[i],l); off+=l;
                }
            }
        }
        u8 key[8]; enc_key_rowid(t->rowid_seq++,key);
        btree_insert(db->pager,&t->root,key,8,row,off);
        if(out&&cap){ rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
}
