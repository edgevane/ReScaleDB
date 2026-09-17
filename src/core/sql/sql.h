#pragma once
#include "../../libs/types.h"
#include "../pager.h"
#define SQL_MAX_COLS 16
#define SQL_MAX_TABLES 16
// Max chars of a single parsed literal (vector "[...]" included).
// Compile-time ceiling; effective limit is runtime-configurable
// via rsc_set_vector_limit() in [64, RSC_VEC_MAX].
#ifndef RSC_VEC_MAX
#define RSC_VEC_MAX 1024
#endif
// Max dims of VECTOR[N]: 255*4+2 bytes still fits BTREE_MAX_VAL.
#define RSC_VEC_MAX_DIMS 255
typedef enum { COL_INT, COL_TEXT, COL_VECTOR } ColType;
typedef struct { char name[32]; ColType type; int dims; int is_pk; int is_unique; int is_not_null; int has_default; int default_is_null; char default_val[64]; } Column;
typedef struct { char name[32]; Column cols[SQL_MAX_COLS]; int ncols; int pk_col; int has_nullmap; u64 root; u64 rowid_seq; } Table;
typedef enum { STMT_CREATE, STMT_INSERT, STMT_SELECT, STMT_UPDATE, STMT_DELETE, STMT_CREATE_IDX, STMT_CREATE_DB, STMT_DROP_DB, STMT_USE, STMT_DUMP_ALL, STMT_LOAD_ALL, STMT_UNKNOWN } StmtKind;
typedef struct { char col[32]; char op[3]; char val[64]; int val_is_null; int is_null_check; } WhereClause;
typedef struct {
    StmtKind kind;
    char table[32];
    Column cols[SQL_MAX_COLS]; int ncols;
    char vals[SQL_MAX_COLS][RSC_VEC_MAX]; int nvals;
    int vals_is_null[SQL_MAX_COLS];
    int vals_is_default[SQL_MAX_COLS];
    WhereClause where[8]; int nwhere;
    char order_by[32]; int has_order;
    int limit; int has_limit;
    char idx_name[32]; char idx_col[32];
    int is_count;
    int is_avg;
    int is_sum;
    int is_max;
    int is_min;
    int is_distinct;
    char agg_col[32];
    char select_cols[16][32]; int nselect; int is_star;
    int has_cossim;
    char cossim_col[32];
    char cossim_qvec[RSC_VEC_MAX];
    char cossim_thresh[64];
    char orig_sql[256];
    Table *bound_table;
} Stmt;
typedef struct { Pager *pager; Table tables[SQL_MAX_TABLES]; int ntables; char pending_path[128]; char pending_target[128]; int has_pending; } Db;
int db_open(Db *db, const char *path);
int db_init(Db *db);
int db_close(Db *db);
int db_exec(Db *db, const char *sql, char *out, usize out_cap);
int sql_parse(const char *sql, Stmt *out);
void rsc_enable_debug(int on);
int rsc_debug_enabled();
void rsc_set_vector_limit(int n);
int rsc_get_vector_limit(void);
#define RSC_MAX_ROWS 256
typedef struct {
    int ncols;
    char cols[16][32];
    int nrows;
    char cells[256][16][64];
} RscResult;
int db_query(Db *db, const char *sql, RscResult *res);
