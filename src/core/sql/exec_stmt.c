#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
int sql_exec_stmt(Db *db, Stmt *s, char *out, usize cap){
    int r;
    r = exec_ddl(db, s, out, cap); if(r != -2) return r;
    if(s->kind==STMT_INSERT) return exec_insert(db, s, out, cap);
    if(s->kind==STMT_SELECT) return exec_select(db, s, out, cap);
    r = exec_write(db, s, out, cap); if(r != -2) return r;
    return -1;
}
