#pragma once
#include "../../libs/types.h"
#include "../pager.h"
#define SQL_MAX_COLS 16
#define SQL_MAX_TABLES 16
typedef enum { COL_INT, COL_TEXT } ColType;
typedef struct { char name[32]; ColType type; } Column;
typedef struct { char name[32]; Column cols[SQL_MAX_COLS]; int ncols; u64 root; u64 rowid_seq; } Table;
typedef enum { STMT_CREATE, STMT_INSERT, STMT_SELECT, STMT_UPDATE, STMT_DELETE, STMT_CREATE_IDX, STMT_CREATE_DB, STMT_DROP_DB, STMT_USE, STMT_DUMP_ALL, STMT_LOAD_ALL, STMT_UNKNOWN } StmtKind;
typedef struct { char col[32]; char op[3]; char val[64]; } WhereClause;
typedef struct {
    StmtKind kind;
    char table[32];
    Column cols[SQL_MAX_COLS]; int ncols;
    char vals[SQL_MAX_COLS][64]; int nvals;
    WhereClause where[8]; int nwhere;
    char order_by[32]; int has_order;
    int limit; int has_limit;
    char idx_name[32]; char idx_col[32];
    Table *bound_table;
} Stmt;
typedef struct { Pager *pager; Table tables[SQL_MAX_TABLES]; int ntables; char pending_path[128]; char pending_target[128]; int has_pending; } Db;
int db_open(Db *db, const char *path);
int db_init(Db *db);
int db_close(Db *db);
int db_exec(Db *db, const char *sql, char *out, usize out_cap);
int sql_parse(const char *sql, Stmt *out);
