#pragma once
#include "sql/sql.h"
void rsc_enable_debug(int on);
int rsc_debug_enabled();
void rsc_debug(const char *msg);
void rsc_debug_unknown_column(Db *db, Stmt *s, const char *bad_col, const char *context);
void rsc_debug_unknown_table(Db *db, Stmt *s, const char *table);
void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
void rsc_debug_constraint(Db *db, Stmt *s, const char *col, const char *errmsg);
