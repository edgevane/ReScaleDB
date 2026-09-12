#include "sql.h"
#include "../../libs/string.h"
int db_open(Db *db, const char *path){
    rsc_memset(db,0,sizeof(*db));
    static Pager pager_storage;
    db->pager=&pager_storage;
    if(pager_open(db->pager,path)!=0) return -1;
    return 0;
}
int db_close(Db *db){ if(db->pager) pager_close(db->pager); return 0; }
int sql_exec_stmt(Db *db, Stmt *s, char *out, usize cap);
int db_exec(Db *db, const char *sql, char *out, usize out_cap){
    Stmt s; if(sql_parse(sql,&s)!=0){ if(out&&out_cap) rsc_strcpy(out,"ERR parse\n"); return -1; }
    int rc=sql_exec_stmt(db,&s,out,out_cap);
    if(rc!=0){ if(out&&out_cap) rsc_strcpy(out,"ERR exec\n"); }
    pager_sync(db->pager);
    return rc;
}
