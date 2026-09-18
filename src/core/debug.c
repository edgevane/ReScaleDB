#include "debug.h"
#include "../arch/arch.h"
#include "../libs/string.h"

static int debug_on = 0;
void rsc_enable_debug(int on){ debug_on = on ? 1 : 0; }
int rsc_debug_enabled(){ return debug_on; }

static void w(const char *s){ if(s) arch_write(2, s, rsc_strlen(s)); }
static void wline(const char *s){ w(s); arch_write(2, "\n", 1); }
static void wint(int v){
    char buf[16]; int pos=0; int neg=0;
    if(v<0){ neg=1; v=-v; }
    char rev[16]; int rp=0;
    if(v==0) rev[rp++]='0';
    while(v>0 && rp<15){ rev[rp++]='0'+(v%10); v/=10; }
    if(neg && pos<15) buf[pos++]='-';
    for(int k=rp-1;k>=0 && pos<15;k--) buf[pos++]=rev[k];
    buf[pos]=0;
    w(buf);
}

void rsc_debug(const char *msg){
    if(!debug_on) return;
    if(!msg) return;
    w("\n[ReScaleDB DEBUG]\n\n");
    wline(msg);
}

static const char *coltype_name(ColType t){ return t==COL_INT ? "INT" : (t==COL_VECTOR ? "VECTOR" : "TEXT"); }

static void print_header_query(Stmt *s){
    wline("[ReScaleDB DEBUG]");
    wline("");
    wline("Query:");
    w("    ");
    if(s->orig_sql[0]) wline(s->orig_sql);
    else wline(";");
    wline("");
}

static void print_table_info(Db *db, const char *tname, Table *t){
    w("Table: ");
    wline(tname);
    wline("");
    if(!t){
        if(db->ntables==0){
            wline("Available tables:");
            wline("    (none)");
        } else {
            wline("Available tables:");
            for(int i=0;i<db->ntables;i++){
                w("    ");
                wline(db->tables[i].name);
            }
        }
        wline("");
        return;
    }
    wline("Available columns:");
    for(int i=0;i<t->ncols;i++){
        w("    ");
        wint(i);
        w(": ");
        w(t->cols[i].name);
        w("  ");
        w(coltype_name(t->cols[i].type));
        if(t->cols[i].type==COL_VECTOR){
            w("["); wint(t->cols[i].dims); w("]");
            if(t->cols[i].quant==1) w(" AS FP16");
            else if(t->cols[i].quant==2) w(" AS Q8");
            else if(t->cols[i].quant==3) w(" AS Q4");
            else if(t->cols[i].quant==4) w(" AS Q2");
            else if(t->cols[i].quant==5) w(" AS Q1");
        }
        if(i==t->pk_col) w("  PRIMARY KEY");
        if(t->cols[i].is_unique && i!=t->pk_col) w("  UNIQUE");
        if(t->cols[i].is_not_null && i!=t->pk_col) w("  NOT NULL");
        if(t->cols[i].has_default){
            w("  DEFAULT ");
            if(t->cols[i].default_is_null) w("NULL");
            else w(t->cols[i].default_val);
        }
        arch_write(2, "\n", 1);
    }
    wline("");
}

static int find_col(Table *t, const char *name){
    for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,name)==0) return i;
    return -1;
}

void rsc_debug_unknown_table(Db *db, Stmt *s, const char *table){
    if(!debug_on) return;
    print_header_query(s);
    print_table_info(db, table, 0);
    wline("Error:");
    w("    table '");
    w(table);
    wline("' does not exist");
    wline("");
}

void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val){
    if(!debug_on) return;
    int idx=-1;
    for(int i=0;i<db->ntables;i++) if(rsc_strcmp(db->tables[i].name,s->table)==0){ idx=i; break; }
    print_header_query(s);
    if(idx<0){
        print_table_info(db, s->table, 0);
    } else {
        Table *t=&db->tables[idx];
        print_table_info(db, s->table, t);
        if(t->pk_col>=0){
            wline("Primary key:");
            w("    ");
            w(t->cols[t->pk_col].name);
            wline(" (must be unique)");
            wline("");
        }
    }
    wline("Error:");
    w("    duplicate primary key '");
    w(val);
    wline("'");
    wline("");
}

void rsc_debug_unknown_column(Db *db, Stmt *s, const char *bad_col, const char *context){
    (void)context;
    if(!debug_on) return;
    int idx=-1;
    for(int i=0;i<db->ntables;i++) if(rsc_strcmp(db->tables[i].name,s->table)==0){ idx=i; break; }
    print_header_query(s);
    if(idx<0){
        print_table_info(db, s->table, 0);
        wline("Error:");
        w("    table '");
        w(s->table);
        wline("' does not exist");
        wline("");
        return;
    }
    Table *t=&db->tables[idx];
    print_table_info(db, s->table, t);
    wline("Column resolution:");
    if(s->kind==STMT_SELECT && !s->is_count && !s->is_avg && s->nselect>0 && !s->is_star){
        for(int i=0;i<s->nselect;i++){
            w("    ");
            w(s->select_cols[i]);
            if(find_col(t,s->select_cols[i])>=0) wline("  -> OK");
            else wline("  -> NOT FOUND");
        }
    }
    for(int i=0;i<s->nwhere;i++){
        int ok = find_col(t,s->where[i].col)>=0;
        int dup=0;
        if(s->kind==STMT_SELECT && !s->is_count && !s->is_avg){
            for(int k=0;k<s->nselect;k++) if(rsc_strcmp(s->where[i].col,s->select_cols[k])==0){ dup=1; break; }
        }
        if(!dup){
            w("    ");
            w(s->where[i].col);
            wline(ok ? "  -> OK" : "  -> NOT FOUND");
        }
    }
    if(s->has_order){
        w("    ");
        w(s->order_by);
        wline(find_col(t,s->order_by)>=0 ? "  -> OK" : "  -> NOT FOUND");
    }
    if(s->is_avg){
        w("    ");
        w(s->agg_col);
        wline(find_col(t,s->agg_col)>=0 ? "  -> OK" : "  -> NOT FOUND");
    }
    wline("");
    wline("Error:");
    w("    column '");
    w(bad_col);
    wline("' does not exist");
    wline("");
}

void rsc_debug_constraint(Db *db, Stmt *s, const char *col, const char *errmsg){
    if(!debug_on) return;
    int idx=-1;
    for(int i=0;i<db->ntables;i++) if(rsc_strcmp(db->tables[i].name,s->table)==0){ idx=i; break; }
    print_header_query(s);
    if(idx<0){
        print_table_info(db, s->table, 0);
    } else {
        Table *t=&db->tables[idx];
        print_table_info(db, s->table, t);
        wline("Column:");
        w("    ");
        wline(col);
        wline("");
    }
    wline("Error:");
    w("    ");
    wline(errmsg);
}
