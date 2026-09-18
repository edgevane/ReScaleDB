#pragma once
#include "sql.h"
#include "../btree.h"
typedef struct { Db *db; Table *t; Stmt *st; char *out; usize cap; usize off; u8 *rows[256]; u16 lens[256]; int nrows; int limit_hit; int cossim_col; float *cossim_q; int cossim_qn; float cossim_thresh; } ScanCtx;

int find_table(Db *db,const char *name);
int find_col(Table *t, const char *name);
void set_col_error(char *out, usize cap, const char *col);
void set_table_error(char *out, usize cap, const char *tab);
void set_pk_error(char *out, usize cap, const char *val);
long parse_int_val(const char *p);
int row_is_null(Table *t, u8 *row, int col);
u8 *row_col_ptr(Table *t, u8 *row, int col);
void row_cell_str(Table *t, u8 *row, int col, char *dst, int *isnull);
void decode_row_all(Table *t, u8 *row, char out[16][64], int isnull[16]);
int encode_row_new(Table *t, char vals[16][RSC_VEC_MAX], int is_null[16], u8 *out, int *out_len);
int col_value_exists(Db *db, Table *t, int col, const char *val_str);
int pk_value_exists(Db *db, Table *t, const char *val_str);
void set_unique_error(char *out, usize cap, const char *col, const char *val);
void set_notnull_error(char *out, usize cap, const char *col);
void set_default_error(char *out, usize cap, const char *col);
u64 enc_key_rowid(u64 id, u8 *out);
int eval_where(Table *t, u8 *row, WhereClause *w);
int parse_float(const char *s, float *out);
void format_float(float f, char *dst, int cap);
float fsqrt_f(float a);
int vec_parse(const char *s, float *out, int maxn);
int vec_encode_str(const char *s, int dims, int quant, u8 *out);
int vec_storage_bytes(int dims, int quant);
const char *vec_quant_name(int quant);
int row_vec_get(Table *t, u8 *row, int col, float *out, int maxn);
float vec_cosine_dist(const float *a, const float *b, int n);
int eval_cossim_row(Table *t, u8 *row, int col, const float *q, int qn, float thresh);
float cossim_dist_row(Table *t, u8 *row, int col, const float *q, int qn);
int cossim_prepare(Db *db, Stmt *s, Table *t, char *out, usize cap, float *qf, int *qn, float *thresh, int *qcol);
int row_replace_col(Table *t, u8 *oldrow, int oldlen, int col, const char *newval, int new_is_null, u8 *newrow, int *newlen);
void set_vector_error(char *out, usize cap, const char *col);
void set_cossim_error(char *out, usize cap, const char *msg);
void append_row_text(Table *t, u8 *row, u16 rlen, char *out, usize cap, usize *off);
void scan_cb(const void *k,u16 kl,const void *v,u16 vl,void *ctx);
int exec_ddl(Db *db, Stmt *s, char *out, usize cap);
int exec_insert(Db *db, Stmt *s, char *out, usize cap);
int exec_select(Db *db, Stmt *s, char *out, usize cap);
int exec_write(Db *db, Stmt *s, char *out, usize cap);
int sql_exec_stmt(Db *db, Stmt *s, char *out, usize cap);
int db_open(Db *db, const char *path);
int db_close(Db *db);
int rsc_dump_all(const char *p);
int rsc_load_all(const char *p);
