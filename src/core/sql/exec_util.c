#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
int find_table(Db *db,const char *name){ for(int i=0;i<db->ntables;i++) if(rsc_strcmp(db->tables[i].name,name)==0) return i; return -1; }
int find_col(Table *t, const char *name){ for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,name)==0) return i; return -1; }
void set_col_error(char *out, usize cap, const char *col){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR column '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,col); l+=rsc_strlen(col); }
    if(l+20<cap) rsc_strcpy(out+l,"' does not exist\n");
    out[cap-1]=0;
}
void set_table_error(char *out, usize cap, const char *tab){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR table '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,tab); l+=rsc_strlen(tab); }
    if(l+20<cap) rsc_strcpy(out+l,"' does not exist\n");
    out[cap-1]=0;
}
void set_pk_error(char *out, usize cap, const char *val){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR duplicate primary key '",cap-1);
    usize l=rsc_strlen(out);
    if(l+64<cap){ rsc_strcpy(out+l,val); l+=rsc_strlen(val); }
    if(l+3<cap) rsc_strcpy(out+l,"'\n");
    out[cap-1]=0;
}
long parse_int_val(const char *p){
    long v=0; int neg=0;
    if(*p=='-'){neg=1;p++;}
    while(*p&&rsc_isdigit(*p)) v=v*10+(*p++-'0');
    if(neg) v=-v;
    return v;
}
int row_is_null(Table *t, u8 *row, int col){
    if(t->has_nullmap){
        u16 map=*(u16*)row;
        return (map>>col)&1;
    }
    return 0;
}
u8 *row_col_ptr(Table *t, u8 *row, int col){
    u8 *p=row + (t->has_nullmap?2:0);
    for(int k=0;k<col;k++){
        if(t->has_nullmap && ((*(u16*)row>>k)&1)) continue;
        if(t->cols[k].type==COL_INT) p+=8;
        else { u16 l=*(u16*)p; p+=2+l; }
    }
    return p;
}
void row_cell_str(Table *t, u8 *row, int col, char *dst, int *isnull){
    if(row_is_null(t,row,col)){ if(isnull) *isnull=1; dst[0]=0; return; }
    if(isnull) *isnull=0;
    u8 *p=row_col_ptr(t,row,col);
    if(t->cols[col].type==COL_INT){
        i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8);
        int pos=0; int neg=0; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0;
        if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-';
        for(int k=rp-1;k>=0;k--) dst[pos++]=rev[k]; dst[pos]=0;
    } else {
        u16 l=*(u16*)p; if(l>63) l=63;
        rsc_memcpy(dst,p+2,l); dst[l]=0;
    }
}
void decode_row_all(Table *t, u8 *row, char out[16][64], int isnull[16]){
    for(int c=0;c<t->ncols;c++) row_cell_str(t,row,c,out[c],isnull? &isnull[c] : 0);
}
int encode_row_new(Table *t, char vals[16][64], int is_null[16], u8 *out, int *out_len){
    int off=0;
    u16 map=0;
    for(int c=0;c<t->ncols;c++) if(is_null[c]) map|=(u16)(1u<<c);
    out[off++]=(u8)(map&0xFF); out[off++]=(u8)((map>>8)&0xFF);
    for(int i=0;i<t->ncols;i++){
        if(is_null[i]) continue;
        if(t->cols[i].type==COL_INT){
            long v=parse_int_val(vals[i]);
            for(int k=0;k<8;k++) out[off++]=(u8)((v>>(k*8))&0xFF);
        } else {
            usize l=rsc_strlen(vals[i]); if(l>255) l=255;
            *(u16*)(out+off)=(u16)l; off+=2;
            rsc_memcpy(out+off,vals[i],l); off+=l;
        }
    }
    *out_len=off;
    return 0;
}
int col_value_exists(Db *db, Table *t, int col, const char *val_str){
    long want_int=0;
    if(t->cols[col].type==COL_INT) want_int=parse_int_val(val_str);
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
            if(row_is_null(t,row,col)) continue;
            u8 *p=row_col_ptr(t,row,col);
            if(t->cols[col].type==COL_INT){
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
int pk_value_exists(Db *db, Table *t, const char *val_str){
    if(t->pk_col<0) return 0;
    return col_value_exists(db,t,t->pk_col,val_str);
}
void set_unique_error(char *out, usize cap, const char *col, const char *val){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR duplicate value '",cap-1);
    usize l=rsc_strlen(out);
    if(l+64<cap){ rsc_strcpy(out+l,val); l+=rsc_strlen(val); }
    if(l+16<cap) rsc_strcpy(out+l,"' for UNIQUE '");
    l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,col); l+=rsc_strlen(col); }
    if(l+3<cap) rsc_strcpy(out+l,"'\n");
    out[cap-1]=0;
}
void set_notnull_error(char *out, usize cap, const char *col){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR column '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,col); l+=rsc_strlen(col); }
    if(l+20<cap) rsc_strcpy(out+l,"' cannot be null\n");
    out[cap-1]=0;
}
void set_default_error(char *out, usize cap, const char *col){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR no default value for column '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,col); l+=rsc_strlen(col); }
    if(l+3<cap) rsc_strcpy(out+l,"'\n");
    out[cap-1]=0;
}
u64 enc_key_rowid(u64 id, u8 *out){ for(int i=7;i>=0;i--) out[i]=id&0xFF, id>>=8; return 8; }
int eval_where(Table *t, u8 *row, WhereClause *w){
    int cidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,w->col)==0) cidx=i;
    if(cidx<0) return 0;
    int cell_null=row_is_null(t,row,cidx);
    if(w->is_null_check==1) return cell_null;
    if(w->is_null_check==2) return !cell_null;
    if(cell_null) return 0;
    if(w->val_is_null){
        if(rsc_strcmp(w->op,"=")==0) return cell_null;
        if(rsc_strcmp(w->op,"!=")==0) return !cell_null;
        return 0;
    }
    char cell[64]={0};
    row_cell_str(t,row,cidx,cell,0);
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
    if(rsc_strcmp(w->op,"!=")==0) return cmp!=0;
    if(rsc_strcmp(w->op,"<")==0) return cmp<0;
    if(rsc_strcmp(w->op,">")==0) return cmp>0;
    if(rsc_strcmp(w->op,"<=")==0) return cmp<=0;
    if(rsc_strcmp(w->op,">=")==0) return cmp>=0;
    return cmp==0;
}
void append_row_text(Table *t, u8 *row, u16 rlen, char *out, usize cap, usize *off){
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
void scan_cb(const void *k,u16 kl,const void *v,u16 vl,void *ctx){
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
