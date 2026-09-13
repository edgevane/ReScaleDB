#include "sql.h"
#include "../debug.h"
#include "../../libs/string.h"
static void db_save_catalog(Db *db){
    db->pager->hdr->version=RSC_VERSION;
    u8 *base=(u8*)db->pager->hdr + 64;
    *(u32*)base=(u32)db->ntables;
    u8 *p=base+4;
    for(int i=0;i<db->ntables;i++){
        Table *t=&db->tables[i];
        rsc_memcpy(p,t->name,32); p+=32;
        *(u32*)p=(u32)t->ncols; p+=4;
        for(int c=0;c<t->ncols;c++){ rsc_memcpy(p,t->cols[c].name,32); p+=32; *(u32*)p=(u32)t->cols[c].type; p+=4; *(u32*)p=(u32)t->cols[c].is_pk; p+=4; }
        *(u64*)p=t->root; p+=8;
        *(u64*)p=t->rowid_seq; p+=8;
        *(i32*)p=(i32)t->pk_col; p+=4;
    }
}
void db_load_catalog(Db *db){
    u8 *base=(u8*)db->pager->hdr + 64;
    u32 ver=db->pager->hdr->version;
    u32 nt=*(u32*)base;
    if(nt>SQL_MAX_TABLES) return;
    if(nt==0||nt==0xFFFFFFFF) return;
    u8 *p=base+4;
    for(u32 i=0;i<nt;i++){
        Table *t=&db->tables[db->ntables++];
        rsc_memcpy(t->name,p,32); p+=32;
        t->ncols=(int)*(u32*)p; p+=4;
        if(t->ncols>SQL_MAX_COLS) t->ncols=SQL_MAX_COLS;
        t->pk_col=-1;
        for(int c=0;c<t->ncols;c++){
            rsc_memcpy(t->cols[c].name,p,32); p+=32;
            t->cols[c].type=(ColType)*(u32*)p; p+=4;
            if(ver>=2){ t->cols[c].is_pk=(int)*(u32*)p; p+=4; }
            else t->cols[c].is_pk=0;
        }
        t->root=*(u64*)p; p+=8;
        t->rowid_seq=*(u64*)p; p+=8;
        if(ver>=2){ t->pk_col=(int)*(i32*)p; p+=4; }
        else {
            t->pk_col=-1;
            for(int c=0;c<t->ncols;c++) if(t->cols[c].is_pk){ t->pk_col=c; break; }
        }
        if(t->pk_col<-1||t->pk_col>=t->ncols) t->pk_col=-1;
    }
}
int db_open(Db *db, const char *path){
    rsc_memset(db,0,sizeof(*db));
    static Pager pager_storage;
    db->pager=&pager_storage;
    if(pager_open(db->pager,path)!=0) return -1;
    db_load_catalog(db);
    return 0;
}
int db_init(Db *db){
    rsc_memset(db,0,sizeof(*db));
    static Pager pager_storage;
    db->pager=&pager_storage;
    if(pager_open_mem(db->pager,4)!=0) return -1;
    return 0;
}
int db_close(Db *db){ if(db->pager) pager_close(db->pager); return 0; }
int sql_exec_stmt(Db *db, Stmt *s, char *out, usize cap);
int db_exec(Db *db, const char *sql, char *out, usize out_cap){
    if(!db||!sql||!out) { if(rsc_debug_enabled()) rsc_debug("ERR null arg"); return -1; }
    if(!db->pager || !db->pager->hdr){ if(rsc_debug_enabled()) rsc_debug("ERR no db"); if(out&&out_cap) rsc_strcpy(out,"ERR no db\n"); return -1; }
    Stmt s; if(sql_parse(sql,&s)!=0){ if(out&&out_cap) rsc_strcpy(out,"ERR parse\n"); if(rsc_debug_enabled()) rsc_debug("ERR parse"); return -1; }
    int rc=sql_exec_stmt(db,&s,out,out_cap);
    if(rc!=0){
        if(out&&out_cap && out[0]==0) rsc_strcpy(out,"ERR exec\n");
        if(rsc_debug_enabled()){
            int structured = out && (rsc_strncmp(out,"ERR column",10)==0 || rsc_strncmp(out,"ERR table",9)==0 || rsc_strncmp(out,"ERR duplicate",13)==0);
            if(!structured) rsc_debug(out&&out[0]?out:"ERR exec");
        }
    }
    else { db_save_catalog(db); }
    if(db->pager && db->pager->hdr) pager_sync(db->pager);
    return rc;
}
