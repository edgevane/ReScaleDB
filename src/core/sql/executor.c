#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
static int find_table(Db *db,const char *name){ for(int i=0;i<db->ntables;i++) if(rsc_strcmp(db->tables[i].name,name)==0) return i; return -1; }
static int find_col(Table *t, const char *name){ for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,name)==0) return i; return -1; }
static void set_col_error(char *out, usize cap, const char *col){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR column '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,col); l+=rsc_strlen(col); }
    if(l+20<cap) rsc_strcpy(out+l,"' does not exist\n");
    out[cap-1]=0;
}
static void set_table_error(char *out, usize cap, const char *tab){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR table '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,tab); l+=rsc_strlen(tab); }
    if(l+20<cap) rsc_strcpy(out+l,"' does not exist\n");
    out[cap-1]=0;
}
static void set_pk_error(char *out, usize cap, const char *val){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR duplicate primary key '",cap-1);
    usize l=rsc_strlen(out);
    if(l+64<cap){ rsc_strcpy(out+l,val); l+=rsc_strlen(val); }
    if(l+3<cap) rsc_strcpy(out+l,"'\n");
    out[cap-1]=0;
}
static long parse_int_val(const char *p){
    long v=0; int neg=0;
    if(*p=='-'){neg=1;p++;}
    while(*p&&rsc_isdigit(*p)) v=v*10+(*p++-'0');
    if(neg) v=-v;
    return v;
}
static int pk_value_exists(Db *db, Table *t, const char *val_str){
    if(t->pk_col<0) return 0;
    int pk=t->pk_col;
    long want_int=0;
    if(t->cols[pk].type==COL_INT) want_int=parse_int_val(val_str);
    u64 pn=t->root;
    while(pn){
        BNode *n=(BNode*)pager_get(db->pager,pn);
        if(!n||n->is_leaf) break;
        if(n->nkeys==0) break;
        u8 *e=(u8*)n+n->offs[0]; u16 ek=*(u16*)e;
        pn=*(u64*)(e+2+ek);
    }
    while(pn){
        BNode *n=(BNode*)pager_get(db->pager,pn);
        if(!n) break;
        for(int i=0;i<n->nkeys;i++){
            u8 *e=(u8*)n+n->offs[i];
            u16 kl=*(u16*)e;
            u8 *row=e+4+kl;
            u8 *p=row;
            for(int k=0;k<pk;k++){ if(t->cols[k].type==COL_INT) p+=8; else { u16 l=*(u16*)p; p+=2+l; } }
            if(t->cols[pk].type==COL_INT){
                i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8);
                if(v==(i64)want_int) return 1;
            } else {
                u16 l=*(u16*)p;
                usize vl=rsc_strlen(val_str);
                if(l==vl && rsc_memcmp(p+2,val_str,l)==0) return 1;
            }
        }
        pn=n->next_leaf;
    }
    return 0;
}
static u64 enc_key_rowid(u64 id, u8 *out){ for(int i=7;i>=0;i--) out[i]=id&0xFF, id>>=8; return 8; }
static int eval_where(Table *t, u8 *row, WhereClause *w){
    // row format: for each col i: if INT 8 bytes, if TEXT u16 len + bytes
    // need to find col idx
    int cidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,w->col)==0) cidx=i;
    if(cidx<0) return 0;
    // decode row up to cidx
    u8 *p=row;
    for(int i=0;i<cidx;i++){ if(t->cols[i].type==COL_INT) p+=8; else { u16 l=*(u16*)p; p+=2+l; } }
    char cell[64]={0};
    if(t->cols[cidx].type==COL_INT){ i64 v=0; for(int k=0;k<8;k++) v=(v<<8)|p[k]; // big endian? we stored BE? actually store BE for key but row we store LE? use LE
        // row store LE for now
        v=0; for(int k=7;k>=0;k--) v=(v<<8)|p[k];
        // simple
        char tmp[32]; int neg=0; if(v<0){neg=1; v=-v;} int pos=0; char rev[32]; if(v==0) rev[pos++]='0'; while(v>0){rev[pos++]='0'+(v%10); v/=10;} if(neg) rev[pos++]='-'; for(int k=0;k<pos;k++) cell[k]=rev[pos-1-k]; cell[pos]=0;
    } else { u16 l=*(u16*)p; if(l>63) l=63; rsc_memcpy(cell,p+2,l); cell[l]=0; }
    // compare cell vs w->val
    int cmp;
    if(t->cols[cidx].type==COL_INT){
        // numeric compare
        long a=0,b=0; int sa=1,sb=1; const char *pa=cell,*pb=w->val;
        if(*pa=='-'){sa=-1;pa++;} if(*pb=='-'){sb=-1;pb++;}
        while(*pa&&rsc_isdigit(*pa)) a=a*10+(*pa++-'0'); a*=sa;
        while(*pb&&rsc_isdigit(*pb)) b=b*10+(*pb++-'0'); b*=sb;
        if(a<b) cmp=-1; else if(a>b) cmp=1; else cmp=0;
    } else cmp=rsc_strcmp(cell,w->val);
    if(rsc_strcmp(w->op,"=")==0) return cmp==0;
    if(rsc_strcmp(w->op,"<")==0) return cmp<0;
    if(rsc_strcmp(w->op,">")==0) return cmp>0;
    if(rsc_strcmp(w->op,"<=")==0) return cmp<=0;
    if(rsc_strcmp(w->op,">=")==0) return cmp>=0;
    return cmp==0;
}
static void append_row_text(Table *t, u8 *row, u16 rlen, char *out, usize cap, usize *off){
    // format as "col=val | ..."
    for(int i=0;i<t->ncols;i++){
        if(i) { if(*off+3<cap){ out[*off]=' '; out[*off+1]='|'; out[*off+2]=' '; *off+=3; } }
        usize nl=rsc_strlen(t->cols[i].name);
        if(*off+nl<cap){ rsc_memcpy(out+*off,t->cols[i].name,nl); *off+=nl; }
        if(*off+1<cap) out[(*off)++]='=';
        // decode
        // find offset
        u8 *p=row;
        for(int k=0;k<i;k++){ if(t->cols[k].type==COL_INT) p+=8; else { u16 l=*(u16*)p; p+=2+l; } }
        char cell[64]={0};
        if(t->cols[i].type==COL_INT){ i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8); char tmp[32]; int pos=0; int neg=0; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) cell[pos++]=rev[k]; cell[pos]=0; }
        else { u16 l=*(u16*)p; if(l>63) l=63; rsc_memcpy(cell,p+2,l); cell[l]=0; }
        usize cl=rsc_strlen(cell);
        if(*off+cl<cap){ rsc_memcpy(out+*off,cell,cl); *off+=cl; }
    }
    if(*off+1<cap) out[(*off)++]='\n';
}
typedef struct { Db *db; Table *t; Stmt *st; char *out; usize cap; usize off; u8 *rows[256]; u16 lens[256]; int nrows; int limit_hit; } ScanCtx;
static void scan_cb(const void *k,u16 kl,const void *v,u16 vl,void *ctx){
    ScanCtx *c=(ScanCtx*)ctx;
    if(c->nrows>=256) return;
    // filter
    int ok=1;
    for(int i=0;i<c->st->nwhere;i++) if(!eval_where(c->t,(u8*)v,&c->st->where[i])){ ok=0; break; }
    if(!ok) return;
    // copy row
    static u8 store[256][1024];
    if(c->nrows<256){ rsc_memcpy(store[c->nrows],v,vl); c->rows[c->nrows]=store[c->nrows]; c->lens[c->nrows]=vl; c->nrows++; }
}
int db_open(Db *db, const char *path);
int db_close(Db *db);
int rsc_dump_all(const char *p);
int rsc_load_all(const char *p);
int sql_exec_stmt(Db *db, Stmt *s, char *out, usize cap){
    if(s->kind==STMT_DUMP_ALL){
        const char *path=s->table[0]?s->table:"state.rsc.dump";
        if(pager_save(db->pager,path)!=0) return -1;
        if(out&&cap) rsc_strcpy(out,"OK\n"); return 0;
    }
    if(s->kind==STMT_LOAD_ALL){
        const char *path=s->table[0]?s->table:"state.rsc.dump";
        if(pager_load(db->pager,path)!=0) return -1;
        db->ntables=0; rsc_memset(db->tables,0,sizeof(db->tables));
        extern void db_load_catalog(Db *db);
        db_load_catalog(db);
        if(out&&cap) rsc_strcpy(out,"OK\n"); return 0;
    }
    if(s->kind==STMT_CREATE_DB){
        db_close(db);
        if(pager_open_mem(db->pager,4)!=0) return -1;
        db->ntables=0; rsc_memset(db->tables,0,sizeof(db->tables));
        rsc_memset(db->pending_path,0,sizeof(db->pending_path));
        rsc_memset(db->pending_target,0,sizeof(db->pending_target));
        db->has_pending=0;
        if(out&&cap){ rsc_strcpy(out,"OK\n"); } return 0;
    }
    if(s->kind==STMT_DROP_DB){
        if(out&&cap){ rsc_strcpy(out,"OK\n"); } return 0;
    }
    if(s->kind==STMT_USE){
        if(out&&cap){ rsc_strcpy(out,"OK\n"); } return 0;
    }
    if(s->kind==STMT_CREATE){
        if(db->ntables>=SQL_MAX_TABLES) return -1;
        if(find_table(db,s->table)>=0) return -1;
        int pk_count=0; int pk_idx=-1;
        for(int i=0;i<s->ncols;i++) if(s->cols[i].is_pk){ pk_count++; pk_idx=i; }
        if(pk_count>1){ if(out&&cap) rsc_strcpy(out,"ERR multiple primary keys\n"); return -1; }
        Table *t=&db->tables[db->ntables++];
        rsc_strcpy(t->name,s->table);
        t->ncols=s->ncols; for(int i=0;i<s->ncols;i++) t->cols[i]=s->cols[i];
        t->pk_col=pk_idx;
        t->rowid_seq=1;
        u64 root=0; btree_create(db->pager,&root); t->root=root;
        // persist catalog into pager hdr root as simple? also store in catalog btree if exists
        out[0]=0; return 0;
    }
    if(s->kind==STMT_CREATE_IDX){
        // create secondary index btree (not used for query yet, just create)
        int idx=find_table(db,s->table); if(idx<0) return -1;
        u64 iroot=0; btree_create(db->pager,&iroot); (void)iroot;
        if(out&&cap) { rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    if(s->kind==STMT_INSERT){
        int idx=find_table(db,s->table);
        if(idx<0){ rsc_debug_unknown_table(db,s,s->table); set_table_error(out,cap,s->table); return -1; }
        Table *t=&db->tables[idx];
        if(s->nvals != t->ncols){ if(out&&cap) rsc_strcpy(out,"ERR column count\n"); return -1; }
        if(t->pk_col>=0){
            const char *pkval=s->vals[t->pk_col];
            if(!pkval[0]){ if(out&&cap) rsc_strcpy(out,"ERR primary key required\n"); return -1; }
            if(pk_value_exists(db,t,pkval)){
                extern void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
                rsc_debug_duplicate_pk(db,s,pkval);
                set_pk_error(out,cap,pkval);
                return -1;
            }
        }
        u8 row[1024]; int off=0;
        for(int i=0;i<t->ncols;i++){
            if(t->cols[i].type==COL_INT){
                long v=0; int neg=0; const char *p=s->vals[i]; if(*p=='-'){neg=1;p++;} while(*p&&rsc_isdigit(*p)) v=v*10+(*p++-'0'); if(neg) v=-v;
                for(int k=0;k<8;k++) row[off++]= (v>>(k*8))&0xFF;
            } else {
                usize l=rsc_strlen(s->vals[i]); if(l>255) l=255; *(u16*)(row+off)=(u16)l; off+=2; rsc_memcpy(row+off,s->vals[i],l); off+=l;
            }
        }
        u8 key[8]; enc_key_rowid(t->rowid_seq++,key);
        btree_insert(db->pager,&t->root,key,8,row,off);
        // update pager hdr root if catalog usage - for now keep
        if(out&&cap){ rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    if(s->kind==STMT_SELECT){
        int idx=find_table(db,s->table);
        if(idx<0){ rsc_debug_unknown_table(db,s,s->table); set_table_error(out,cap,s->table); return -1; }
        Table *t=&db->tables[idx];
        if(!s->is_star && !s->is_count && !s->is_avg && s->nselect>0){
            for(int i=0;i<s->nselect;i++){
                if(find_col(t,s->select_cols[i])<0){ rsc_debug_unknown_column(db,s,s->select_cols[i],"select"); set_col_error(out,cap,s->select_cols[i]); return -1; }
            }
        }
        for(int i=0;i<s->nwhere;i++){
            if(find_col(t,s->where[i].col)<0){ rsc_debug_unknown_column(db,s,s->where[i].col,"where"); set_col_error(out,cap,s->where[i].col); return -1; }
        }
        if(s->has_order){
            if(find_col(t,s->order_by)<0){ rsc_debug_unknown_column(db,s,s->order_by,"order"); set_col_error(out,cap,s->order_by); return -1; }
        }
        if(s->is_avg){
            if(find_col(t,s->agg_col)<0){ rsc_debug_unknown_column(db,s,s->agg_col,"avg"); set_col_error(out,cap,s->agg_col); return -1; }
        }
        if(s->is_count && s->agg_col[0] && rsc_strcmp(s->agg_col,"*")!=0){
            if(find_col(t,s->agg_col)<0){ rsc_debug_unknown_column(db,s,s->agg_col,"count"); set_col_error(out,cap,s->agg_col); return -1; }
        }
        int proj_idx[16]; int proj_n=0;
        int use_all = s->is_star || (s->nselect==0 && !s->is_count && !s->is_avg);
        if(use_all){ for(int c=0;c<t->ncols;c++) proj_idx[proj_n++]=c; }
        else if(!s->is_count && !s->is_avg){ for(int i=0;i<s->nselect;i++) proj_idx[proj_n++]=find_col(t,s->select_cols[i]); }
        ScanCtx ctx; rsc_memset(&ctx,0,sizeof(ctx)); ctx.db=db; ctx.t=t; ctx.st=s; ctx.out=out; ctx.cap=cap; ctx.off=0;
        btree_scan(db->pager,t->root,scan_cb,&ctx);
        // simple order by (bubble)
        if(s->has_order){
            int oidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,s->order_by)==0) oidx=i;
            if(oidx>=0){
                for(int i=0;i<ctx.nrows;i++) for(int j=i+1;j<ctx.nrows;j++){
                    // decode both rows col
                    u8 *pa=ctx.rows[i],*pb=ctx.rows[j];
                    for(int k=0;k<oidx;k++){ if(t->cols[k].type==COL_INT) pa+=8,pb+=8; else {u16 la=*(u16*)pa,lb=*(u16*)pb; pa+=2+la; pb+=2+lb; } }
                    int cmp=0;
                    if(t->cols[oidx].type==COL_INT){ i64 va=0,vb=0; for(int k=0;k<8;k++) va|=(i64)pa[k]<<(k*8), vb|=(i64)pb[k]<<(k*8); cmp=(va<vb?-1:(va>vb?1:0)); }
                    else { u16 la=*(u16*)pa, lb=*(u16*)pb; usize m=la<lb?la:lb; cmp=rsc_memcmp(pa+2,pb+2,m); if(!cmp) cmp=(la<lb?-1:(la>lb?1:0)); }
                    if(cmp>0){ u8 *tmp=ctx.rows[i]; ctx.rows[i]=ctx.rows[j]; ctx.rows[j]=tmp; u16 tl=ctx.lens[i]; ctx.lens[i]=ctx.lens[j]; ctx.lens[j]=tl; }
                }
            }
        }
        if(s->is_count || s->is_avg){
            int cnt=ctx.nrows;
            usize off=0;
            #define OUTC2(c) do{ if(off+1<cap) out[off++]=c; }while(0)
            #define OUTS2(s) do{ usize _l=rsc_strlen(s); if(off+_l<cap){ rsc_memcpy(out+off,s,_l); off+=_l; } }while(0)
            if(s->is_count){
                char hdr2[64]="COUNT"; char val[32]; int pos=0; int v=cnt; char rev[16]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
                int w=(int)rsc_strlen(hdr2); int wl=(int)rsc_strlen(val); if(wl>w) w=wl;
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(hdr2); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(val); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
            }
            if(s->is_avg){
                int cidx=-1; for(int c=0;c<t->ncols;c++) if(rsc_strcmp(t->cols[c].name,s->agg_col)==0) cidx=c;
                if(cidx<0) return -1;
                if(t->cols[cidx].type!=COL_INT) return -1;
                i64 sum=0;
                for(int r=0;r<cnt;r++){
                    u8 *p=ctx.rows[r]; for(int k=0;k<cidx;k++){ if(t->cols[k].type==COL_INT) p+=8; else {u16 l=*(u16*)p; p+=2+l; } }
                    i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8); sum+=v;
                }
                i64 avg=cnt? sum/cnt : 0;
                char hdr2[64]; rsc_strcpy(hdr2,"AVG("); rsc_strcpy(hdr2+4,s->agg_col); rsc_strcpy(hdr2+4+rsc_strlen(s->agg_col),")");
                char val[32]; int pos=0; int neg=0; i64 v=avg; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
                int w=(int)rsc_strlen(hdr2); int wl=(int)rsc_strlen(val); if(wl>w) w=wl;
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(hdr2); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(val); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
            }
        }
        int lim = s->has_limit? s->limit : ctx.nrows;
        if(lim>ctx.nrows) lim=ctx.nrows;
        int widths[SQL_MAX_COLS]={0};
        for(int pi=0;pi<proj_n;pi++){ int c=proj_idx[pi]; widths[pi]=(int)rsc_strlen(t->cols[c].name); }
        static char cells[256][16][64];
        for(int r=0;r<lim;r++){
            u8 *row=ctx.rows[r];
            char tmpvals[16][64];
            {
                u8 *p=row;
                for(int c=0;c<t->ncols;c++){
                    char *dst=tmpvals[c];
                    if(t->cols[c].type==COL_INT){
                        i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8);
                        int pos=0; int neg=0; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) dst[pos++]=rev[k]; dst[pos]=0; p+=8;
                    } else { u16 l=*(u16*)p; if(l>63) l=63; rsc_memcpy(dst,p+2,l); dst[l]=0; p+=2+l; }
                }
            }
            for(int pi=0;pi<proj_n;pi++){
                int c=proj_idx[pi];
                rsc_strcpy(cells[r][pi], tmpvals[c]);
                int cl=(int)rsc_strlen(cells[r][pi]);
                if(cl>widths[pi]) widths[pi]=cl;
            }
        }
        usize off=0;
        #define OUTC(c) do{ if(off+1<cap) out[off++]=c; }while(0)
        #define OUTS(s) do{ usize _l=rsc_strlen(s); if(off+_l<cap){ rsc_memcpy(out+off,s,_l); off+=_l; } }while(0)
        #define OUTN(s,n) do{ if(off+(n)<cap){ rsc_memcpy(out+off,s,n); off+=n; } }while(0)
        if(lim==0 && ctx.nrows==0){
            OUTS("(empty)\n"); if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
        }
        for(int pi=0;pi<proj_n;pi++){ OUTC('+'); for(int k=0;k<widths[pi]+2;k++) OUTC('-'); } OUTC('+'); OUTC('\n');
        for(int pi=0;pi<proj_n;pi++){
            int c=proj_idx[pi];
            OUTC('|'); OUTC(' ');
            OUTS(t->cols[c].name);
            for(int k=(int)rsc_strlen(t->cols[c].name);k<widths[pi];k++) OUTC(' ');
            OUTC(' ');
        } OUTC('|'); OUTC('\n');
        for(int pi=0;pi<proj_n;pi++){ OUTC('+'); for(int k=0;k<widths[pi]+2;k++) OUTC('-'); } OUTC('+'); OUTC('\n');
        for(int r=0;r<lim;r++){
            for(int pi=0;pi<proj_n;pi++){
                OUTC('|'); OUTC(' ');
                OUTS(cells[r][pi]);
                for(int k=(int)rsc_strlen(cells[r][pi]);k<widths[pi];k++) OUTC(' ');
                OUTC(' ');
            } OUTC('|'); OUTC('\n');
        }
        for(int pi=0;pi<proj_n;pi++){ OUTC('+'); for(int k=0;k<widths[pi]+2;k++) OUTC('-'); } OUTC('+'); OUTC('\n');
        if(off<cap) out[off]=0; else out[cap-1]=0;
        return 0;
    }
    if(s->kind==STMT_DELETE){
        int idx=find_table(db,s->table); if(idx<0) return -1;
        Table *t=&db->tables[idx];
        // scan and delete matching rowids
        // collect keys to delete
        u8 keys[256][8]; int nk=0;
        // custom scan to collect
        // reuse btree_scan with ctx
        ScanCtx ctx; rsc_memset(&ctx,0,sizeof(ctx)); ctx.db=db; ctx.t=t; ctx.st=s;
        // we need keys, so do manual scan
        // brute: iterate via btree_scan and eval
        // For now, do manual leaf scan collecting keys
        // Use btree_scan to get rows, but need keys - easier: iterate again
        // Collect rowids by scanning leaves directly
        // Simplified: scan via pager leaves and eval
        u64 pn=t->root;
        // go leftmost
        while(pn){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n||n->is_leaf) break; if(n->nkeys==0) break; u8 *e=(u8*)n+n->offs[0]; u16 ek=*(u16*)e; pn=*(u64*)(e+2+ek); }
        while(pn&&nk<256){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n) break; for(int i=0;i<n->nkeys;i++){ u8 *e=(u8*)n+n->offs[i]; u16 kl=*(u16*)e; u16 vl=*(u16*)(e+2); u8 *k=e+4; u8 *v=e+4+kl; int ok=1; for(int w=0;w<s->nwhere;w++) if(!eval_where(t,v,&s->where[w])){ok=0;break;} if(ok){ rsc_memcpy(keys[nk],k,kl); nk++; } } pn=n->next_leaf; }
        for(int i=0;i<nk;i++) btree_delete(db->pager,&t->root,keys[i],8);
        if(out&&cap){ rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    if(s->kind==STMT_UPDATE){
        int idx=find_table(db,s->table);
        if(idx<0){ rsc_debug_unknown_table(db,s,s->table); set_table_error(out,cap,s->table); return -1; }
        Table *t=&db->tables[idx];
        int colidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,s->cols[0].name)==0) colidx=i;
        if(colidx<0){ rsc_debug_unknown_column(db,s,s->cols[0].name,"update"); set_col_error(out,cap,s->cols[0].name); return -1; }
        for(int i=0;i<s->nwhere;i++) if(find_col(t,s->where[i].col)<0){ rsc_debug_unknown_column(db,s,s->where[i].col,"where"); set_col_error(out,cap,s->where[i].col); return -1; }
        if(colidx==t->pk_col){
            const char *newval=s->vals[0];
            // if more than one row would get same PK -> duplicate
            // check if new value exists outside matched rows later; quick check: if exists at all and matched rows don't already all have it, fail
            // full check done after scan; here pre-check for multi-row update
            (void)newval;
        }
        // similar scan
        u8 keys[256][8]; u8 rows[256][1024]; u16 rls[256]; int nk=0;
        u64 pn=t->root;
        while(pn){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n||n->is_leaf) break; if(n->nkeys==0) break; u8 *e=(u8*)n+n->offs[0]; u16 ek=*(u16*)e; pn=*(u64*)(e+2+ek); }
        while(pn&&nk<256){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n) break; for(int i=0;i<n->nkeys;i++){ u8 *e=(u8*)n+n->offs[i]; u16 kl=*(u16*)e; u16 vl=*(u16*)(e+2); u8 *k=e+4; u8 *v=e+4+kl; int ok=1; for(int w=0;w<s->nwhere;w++) if(!eval_where(t,v,&s->where[w])){ok=0;break;} if(ok){ rsc_memcpy(keys[nk],k,kl); rsc_memcpy(rows[nk],v,vl); rls[nk]=vl; nk++; } } pn=n->next_leaf; }
        if(colidx==t->pk_col && nk>0){
            const char *newval=s->vals[0];
            if(nk>1){
                extern void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
                rsc_debug_duplicate_pk(db,s,newval);
                set_pk_error(out,cap,newval);
                return -1;
            }
            // nk==1: check if new value differs from current and already exists elsewhere
            {
                u8 *cur=rows[0];
                u8 *cp=cur;
                for(int k=0;k<colidx;k++){ if(t->cols[k].type==COL_INT) cp+=8; else { u16 l=*(u16*)cp; cp+=2+l; } }
                int same=0;
                if(t->cols[colidx].type==COL_INT){
                    long nv=parse_int_val(newval);
                    i64 cv=0; for(int k=0;k<8;k++) cv|=(i64)cp[k]<<(k*8);
                    same=(cv==(i64)nv);
                } else {
                    u16 l=*(u16*)cp; usize vl=rsc_strlen(newval);
                    same=(l==vl && rsc_memcmp(cp+2,newval,l)==0);
                }
                if(!same && pk_value_exists(db,t,newval)){
                    extern void rsc_debug_duplicate_pk(Db *db, Stmt *s, const char *val);
                    rsc_debug_duplicate_pk(db,s,newval);
                    set_pk_error(out,cap,newval);
                    return -1;
                }
            }
        }
        for(int i=0;i<nk;i++){
            // decode row, patch col
            u8 newrow[1024]; rsc_memcpy(newrow,rows[i],rls[i]);
            u8 *p=newrow; for(int k=0;k<colidx;k++){ if(t->cols[k].type==COL_INT) p+=8; else {u16 l=*(u16*)p; p+=2+l;} }
            if(t->cols[colidx].type==COL_INT){
                long v=0; int neg=0; const char *pp=s->vals[0]; if(*pp=='-'){neg=1;pp++;} while(*pp&&rsc_isdigit(*pp)) v=v*10+(*pp++-'0'); if(neg) v=-v;
                for(int k=0;k<8;k++) p[k]=(v>>(k*8))&0xFF;
            } else {
                usize nl=rsc_strlen(s->vals[0]);
                u8 rebuilt[1024]; int roff=0;
                u8 *op=rows[i];
                for(int c=0;c<t->ncols;c++){
                    if(c==colidx){
                        *(u16*)(rebuilt+roff)=(u16)nl; roff+=2; rsc_memcpy(rebuilt+roff,s->vals[0],nl); roff+=nl;
                        if(t->cols[c].type==COL_INT) op+=8; else {u16 l=*(u16*)op; op+=2+l;}
                    } else {
                        if(t->cols[c].type==COL_INT){ rsc_memcpy(rebuilt+roff,op,8); roff+=8; op+=8; }
                        else {u16 l=*(u16*)op; *(u16*)(rebuilt+roff)=l; roff+=2; rsc_memcpy(rebuilt+roff,op+2,l); roff+=l; op+=2+l;}
                    }
                }
                rsc_memcpy(newrow,rebuilt,roff);
                btree_insert(db->pager,&t->root,keys[i],8,newrow,roff);
                continue;
            }
            btree_insert(db->pager,&t->root,keys[i],8,newrow,rls[i]);
        }
        if(out&&cap){ rsc_memcpy(out,"OK\n",3); out[3]=0; }
        return 0;
    }
    return -1;
}
int db_query(Db *db, const char *sql, RscResult *res){
    rsc_memset(res,0,sizeof(*res));
    Stmt s; if(sql_parse(sql,&s)!=0) return -1;
    if(s.kind!=STMT_SELECT) return -1;
    int idx=find_table(db,s.table);
    if(idx<0){ rsc_debug_unknown_table(db,&s,s.table); return -1; }
    Table *t=&db->tables[idx];
    if(!s.is_star && !s.is_count && !s.is_avg && s.nselect>0){
        for(int i=0;i<s.nselect;i++) if(find_col(t,s.select_cols[i])<0){ rsc_debug_unknown_column(db,&s,s.select_cols[i],"select"); return -1; }
    }
    for(int i=0;i<s.nwhere;i++) if(find_col(t,s.where[i].col)<0){ rsc_debug_unknown_column(db,&s,s.where[i].col,"where"); return -1; }
    if(s.has_order) if(find_col(t,s.order_by)<0){ rsc_debug_unknown_column(db,&s,s.order_by,"order"); return -1; }
    if(s.is_avg) if(find_col(t,s.agg_col)<0){ rsc_debug_unknown_column(db,&s,s.agg_col,"avg"); return -1; }
    if(s.is_count && s.agg_col[0] && rsc_strcmp(s.agg_col,"*")!=0) if(find_col(t,s.agg_col)<0){ rsc_debug_unknown_column(db,&s,s.agg_col,"count"); return -1; }
    ScanCtx ctx; rsc_memset(&ctx,0,sizeof(ctx)); ctx.db=db; ctx.t=t; ctx.st=&s;
    btree_scan(db->pager,t->root,scan_cb,&ctx);
    if(s.has_order){
        int oidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,s.order_by)==0) oidx=i;
        if(oidx>=0){
            for(int i=0;i<ctx.nrows;i++) for(int j=i+1;j<ctx.nrows;j++){
                u8 *pa=ctx.rows[i],*pb=ctx.rows[j];
                for(int k=0;k<oidx;k++){ if(t->cols[k].type==COL_INT) pa+=8,pb+=8; else {u16 la=*(u16*)pa,lb=*(u16*)pb; pa+=2+la; pb+=2+lb; } }
                int cmp=0;
                if(t->cols[oidx].type==COL_INT){ i64 va=0,vb=0; for(int k=0;k<8;k++) va|=(i64)pa[k]<<(k*8), vb|=(i64)pb[k]<<(k*8); cmp=(va<vb?-1:(va>vb?1:0)); }
                else { u16 la=*(u16*)pa, lb=*(u16*)pb; usize m=la<lb?la:lb; cmp=rsc_memcmp(pa+2,pb+2,m); if(!cmp) cmp=(la<lb?-1:(la>lb?1:0)); }
                if(cmp>0){ u8 *tmp=ctx.rows[i]; ctx.rows[i]=ctx.rows[j]; ctx.rows[j]=tmp; u16 tl=ctx.lens[i]; ctx.lens[i]=ctx.lens[j]; ctx.lens[j]=tl; }
            }
        }
    }
    int lim=s.has_limit? s.limit : ctx.nrows;
    if(lim>ctx.nrows) lim=ctx.nrows;
    if(s.is_count){
        res->ncols=1; rsc_strcpy(res->cols[0],"COUNT");
        res->nrows=1;
        char val[32]; int pos=0; int v=ctx.nrows; char rev[16]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
        rsc_strcpy(res->cells[0][0],val);
        return 0;
    }
    if(s.is_avg){
        int cidx=-1; for(int c=0;c<t->ncols;c++) if(rsc_strcmp(t->cols[c].name,s.agg_col)==0) cidx=c;
        if(cidx<0) return -1;
        i64 sum=0;
        for(int r=0;r<ctx.nrows;r++){ u8 *p=ctx.rows[r]; for(int k=0;k<cidx;k++){ if(t->cols[k].type==COL_INT) p+=8; else {u16 l=*(u16*)p; p+=2+l; } } i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8); sum+=v; }
        i64 avg=ctx.nrows? sum/ctx.nrows : 0;
        res->ncols=1; rsc_strcpy(res->cols[0],"AVG("); rsc_strcpy(res->cols[0]+4,s.agg_col); res->cols[0][4+rsc_strlen(s.agg_col)]=')'; res->cols[0][5+rsc_strlen(s.agg_col)]=0;
        res->nrows=1;
        char val[32]; int pos=0; int neg=0; i64 v=avg; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
        rsc_strcpy(res->cells[0][0],val);
        return 0;
    }
    int proj_idx[16]; int proj_n=0;
    int use_all = s.is_star || (s.nselect==0 && !s.is_count && !s.is_avg);
    if(use_all){ for(int c=0;c<t->ncols;c++) proj_idx[proj_n++]=c; }
    else { for(int i=0;i<s.nselect;i++) proj_idx[proj_n++]=find_col(t,s.select_cols[i]); }
    res->ncols=proj_n;
    for(int pi=0;pi<proj_n;pi++) rsc_strcpy(res->cols[pi],t->cols[proj_idx[pi]].name);
    res->nrows=lim;
    for(int r=0;r<lim;r++){
        u8 *row=ctx.rows[r];
        char tmpvals[16][64];
        {
            u8 *p=row;
            for(int c=0;c<t->ncols;c++){
                char *dst=tmpvals[c];
                if(t->cols[c].type==COL_INT){
                    i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8);
                    int pos=0; int neg=0; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) dst[pos++]=rev[k]; dst[pos]=0; p+=8;
                } else { u16 l=*(u16*)p; if(l>63) l=63; rsc_memcpy(dst,p+2,l); dst[l]=0; p+=2+l; }
            }
        }
        for(int pi=0;pi<proj_n;pi++) rsc_strcpy(res->cells[r][pi], tmpvals[proj_idx[pi]]);
    }
    return 0;
}
