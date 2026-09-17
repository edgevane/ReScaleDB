#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
int exec_ddl(Db *db, Stmt *s, char *out, usize cap){
    if(s->kind==STMT_DUMP_ALL){        const char *path=s->table[0]?s->table:"state.rsc.dump";
        if(pager_save(db->pager,path)!=0) return -1;
        if(out&&cap) rsc_strcpy(out,"OK\n"); return 0;
    }
    if(s->kind==STMT_LOAD_ALL){        const char *path=s->table[0]?s->table:"state.rsc.dump";
        if(pager_load(db->pager,path)!=0) return -1;
        db->ntables=0; rsc_memset(db->tables,0,sizeof(db->tables));
        extern void db_load_catalog(Db *db);
        db_load_catalog(db);
        if(out&&cap) rsc_strcpy(out,"OK\n"); return 0;
    }
    if(s->kind==STMT_CREATE_DB){        db_close(db);
        if(pager_open_mem(db->pager,4)!=0) return -1;
        db->ntables=0; rsc_memset(db->tables,0,sizeof(db->tables));
        rsc_memset(db->pending_path,0,sizeof(db->pending_path));
        rsc_memset(db->pending_target,0,sizeof(db->pending_target));
        db->has_pending=0;
        if(out&&cap){ rsc_strcpy(out,"OK\n"); } return 0;
    }
    if(s->kind==STMT_DROP_DB){        if(out&&cap){ rsc_strcpy(out,"OK\n"); } return 0;
    }
    if(s->kind==STMT_USE){        if(out&&cap){ rsc_strcpy(out,"OK\n"); } return 0;
    }
    if(s->kind==STMT_CREATE){        if(db->ntables>=SQL_MAX_TABLES) return -1;
        if(find_table(db,s->table)>=0) return -1;
        int pk_count=0; int pk_idx=-1;
        for(int i=0;i<s->ncols;i++) if(s->cols[i].is_pk){ pk_count++; pk_idx=i; }
        if(pk_count>1){ if(out&&cap) rsc_strcpy(out,"ERR multiple primary keys\n"); return -1; }
        for(int i=0;i<s->ncols;i++){
            if(s->cols[i].type==COL_VECTOR){
                if(s->cols[i].dims<=0||s->cols[i].dims>RSC_VEC_MAX_DIMS){ if(out&&cap) rsc_strcpy(out,"ERR invalid VECTOR dims\n"); return -1; }
                if(s->cols[i].is_pk){ if(out&&cap) rsc_strcpy(out,"ERR PRIMARY KEY on VECTOR not supported\n"); return -1; }
                if(s->cols[i].has_default){ if(out&&cap) rsc_strcpy(out,"ERR DEFAULT on VECTOR not supported\n"); return -1; }
            }
        }
        Table *t=&db->tables[db->ntables++];
        rsc_strcpy(t->name,s->table);
        t->ncols=s->ncols; for(int i=0;i<s->ncols;i++) t->cols[i]=s->cols[i];
        t->pk_col=pk_idx;
        t->has_nullmap=1;
        t->rowid_seq=1;
        u64 root=0; btree_create(db->pager,&root); t->root=root;
        // persist catalog into pager hdr root as simple? also store in catalog btree if exists
        out[0]=0; return 0;
    }
    if(s->kind==STMT_CREATE_IDX){        // create secondary index btree (not used for query yet, just create)
        int idx=find_table(db,s->table); if(idx<0) return -1;
        u64 iroot=0; btree_create(db->pager,&iroot); (void)iroot;
        if(out&&cap) { rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    (void)db; (void)s; (void)out; (void)cap;
    return -2;
}
