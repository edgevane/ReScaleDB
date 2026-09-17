#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
// float/vector helpers (defined at the bottom of this file)
int parse_float(const char *s, float *out);
void format_float(float f, char *dst, int cap);
float fsqrt_f(float a);
int vec_parse(const char *s, float *out, int maxn);
int vec_encode_str(const char *s, int dims, u8 *out);
int row_vec_get(Table *t, u8 *row, int col, float *out, int maxn);
float vec_cosine_dist(const float *a, const float *b, int n);
int eval_cossim_row(Table *t, u8 *row, int col, const float *q, int qn, float thresh);
float cossim_dist_row(Table *t, u8 *row, int col, const float *q, int qn);int cossim_prepare(Db *db, Stmt *s, Table *t, char *out, usize cap, float *qf, int *qn, float *thresh, int *qcol);
int row_replace_col(Table *t, u8 *oldrow, int oldlen, int col, const char *newval, int new_is_null, u8 *newrow, int *newlen);
void set_vector_error(char *out, usize cap, const char *col);
void set_cossim_error(char *out, usize cap, const char *msg);
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
        else if(t->cols[k].type==COL_VECTOR) p+=(usize)t->cols[k].dims*4;
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
    } else if(t->cols[col].type==COL_VECTOR){
        // display as [f,f,...], truncated to 63 chars like TEXT
        int dims=t->cols[col].dims;
        int pos=0;
        if(pos<63) dst[pos++]='[';
        for(int d=0;d<dims && pos<63;d++){
            u32 bits=0; for(int k=0;k<4;k++) bits|=(u32)p[d*4+k]<<(k*8);
            union { u32 u; float f; } cv; cv.u=bits;
            char fb[32]; format_float(cv.f,fb,sizeof(fb));
            if(d>0 && pos<63) dst[pos++]=',';
            for(int k=0;fb[k]&&pos<63;k++) dst[pos++]=fb[k];
        }
        if(pos<63) dst[pos++]=']';
        dst[pos<63?pos:63]=0;
    } else {
        u16 l=*(u16*)p; if(l>63) l=63;
        rsc_memcpy(dst,p+2,l); dst[l]=0;
    }
}
void decode_row_all(Table *t, u8 *row, char out[16][64], int isnull[16]){
    for(int c=0;c<t->ncols;c++) row_cell_str(t,row,c,out[c],isnull? &isnull[c] : 0);
}
int encode_row_new(Table *t, char vals[16][RSC_VEC_MAX], int is_null[16], u8 *out, int *out_len){
    int off=0;
    u16 map=0;
    for(int c=0;c<t->ncols;c++) if(is_null[c]) map|=(u16)(1u<<c);
    out[off++]=(u8)(map&0xFF); out[off++]=(u8)((map>>8)&0xFF);
    for(int i=0;i<t->ncols;i++){
        if(is_null[i]) continue;
        if(t->cols[i].type==COL_INT){
            long v=parse_int_val(vals[i]);
            for(int k=0;k<8;k++) out[off++]=(u8)((v>>(k*8))&0xFF);
        } else if(t->cols[i].type==COL_VECTOR){
            if(vec_encode_str(vals[i],t->cols[i].dims,out+off)!=0) return -1;
            off+=t->cols[i].dims*4;
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
    u8 want_vec[RSC_VEC_MAX_DIMS*4]; int want_vec_len=0;
    if(t->cols[col].type==COL_INT) want_int=parse_int_val(val_str);
    else if(t->cols[col].type==COL_VECTOR){
        if(vec_encode_str(val_str,t->cols[col].dims,want_vec)!=0) return 0;
        want_vec_len=t->cols[col].dims*4;
    }
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
            } else if(t->cols[col].type==COL_VECTOR){
                if(rsc_memcmp(p,want_vec,(usize)want_vec_len)==0) return 1;
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
    if(ok && c->cossim_q){
        if(!eval_cossim_row(c->t,(u8*)v,c->cossim_col,c->cossim_q,c->cossim_qn,c->cossim_thresh)) ok=0;
    }
    if(!ok) return;
    // copy row
    static u8 store[256][1024];
    if(c->nrows<256){ rsc_memcpy(store[c->nrows],v,vl); c->rows[c->nrows]=store[c->nrows]; c->lens[c->nrows]=vl; c->nrows++; }
}
// ---- floats & vectors (freestanding: no libc strtof/sqrt) ----
int parse_float(const char *s, float *out){
    if(!s||!*s) return -1;
    const char *p=s;
    int neg=0;
    if(*p=='-'||*p=='+'){ neg=(*p=='-'); p++; }
    double v=0; int digits=0;
    while(rsc_isdigit(*p)){ v=v*10.0+(*p-'0'); p++; digits++; }
    if(*p=='.'){
        p++;
        double div=1;
        while(rsc_isdigit(*p)){ v=v*10.0+(*p-'0'); div*=10.0; p++; digits++; }
        v/=div;
    }
    if(!digits) return -1;
    if(*p=='e'||*p=='E'){
        p++;
        int eneg=0;
        if(*p=='-'||*p=='+'){ eneg=(*p=='-'); p++; }
        int e=0, ed=0;
        while(rsc_isdigit(*p)){ if(e<10000) e=e*10+(*p-'0'); p++; ed++; }
        if(!ed) return -1;
        double pw=1;
        for(int i=0;i<e;i++){ pw*=10.0; if(pw>1e39) return -1; }
        v=eneg?v/pw:v*pw;
    }
    if(*p!=0) return -1;
    if(neg) v=-v;
    if(v>3.4028235e38||v<-3.4028235e38) return -1;
    *out=(float)v;
    return 0;
}
void format_float(float f, char *dst, int cap){
    if(cap<=0) return;
    int neg=(f<0.0f)?1:0;
    double a=neg?-(double)f:(double)f;
    if(!(a<1e19)){ rsc_strncpy(dst,neg?"-inf":"inf",cap-1); dst[cap-1]=0; return; }
    long ip=(long)a;
    long fp=(long)((a-(double)ip)*1000000.0+0.5);
    if(fp>=1000000){ ip++; fp-=1000000; }
    char ibuf[24]; int il=0;
    if(ip==0) ibuf[il++]='0';
    else { char rev[24]; int rp=0; long t=ip; while(t>0){ rev[rp++]='0'+(t%10); t/=10; } for(int k=rp-1;k>=0;k--) ibuf[il++]=rev[k]; }
    int pos=0;
    if(neg&&pos<cap-1) dst[pos++]='-';
    for(int k=0;k<il&&pos<cap-1;k++) dst[pos++]=ibuf[k];
    if(pos<cap-1) dst[pos++]='.';
    long div=100000;
    for(int k=0;k<6&&pos<cap-1;k++){ dst[pos++]='0'+(fp/div); fp%=div; div/=10; }
    dst[pos]=0;
}
float fsqrt_f(float a){
    if(a<=0) return 0;
    double x=(double)a;
    for(int i=0;i<100;i++){ double nx=0.5*(x+(double)a/x); if(nx==x) break; x=nx; }
    return (float)x;
}
// Parse "1.0, 2.5, -3e2" into out[]. Returns count or -1.
int vec_parse(const char *s, float *out, int maxn){
    int n=0;
    const char *p=s;
    while(1){
        while(rsc_isspace(*p)) p++;
        if(!*p) break;
        const char *e=p;
        while(*e&&*e!=',') e++;
        int len=(int)(e-p);
        while(len>0&&rsc_isspace(p[len-1])) len--;
        if(len<=0||len>=64) return -1;
        char tok[64]; for(int k=0;k<len;k++) tok[k]=p[k]; tok[len]=0;
        if(n>=maxn) return -1;
        if(parse_float(tok,&out[n])!=0) return -1;
        n++;
        p=e;
        if(*p==',') p++;
    }
    return n;
}
// Encode "1.0,2.0" into dims*4 LE float32 bytes. 0 ok, -1 bad/count mismatch.
int vec_encode_str(const char *s, int dims, u8 *out){
    if(dims<=0||dims>RSC_VEC_MAX_DIMS) return -1;
    float tmp[RSC_VEC_MAX_DIMS];
    int n=vec_parse(s,tmp,dims);
    if(n!=dims) return -1;
    for(int i=0;i<dims;i++){
        union { float f; u32 u; } c; c.f=tmp[i];
        out[i*4+0]=(u8)(c.u&0xFF); out[i*4+1]=(u8)((c.u>>8)&0xFF);
        out[i*4+2]=(u8)((c.u>>16)&0xFF); out[i*4+3]=(u8)((c.u>>24)&0xFF);
    }
    return 0;
}
int row_vec_get(Table *t, u8 *row, int col, float *out, int maxn){
    if(col<0||col>=t->ncols) return -1;
    if(t->cols[col].type!=COL_VECTOR) return -1;
    if(row_is_null(t,row,col)) return -1;
    int dims=t->cols[col].dims;
    if(dims>maxn) return -1;
    u8 *p=row_col_ptr(t,row,col);
    for(int i=0;i<dims;i++){
        u32 bits=0;
        for(int k=0;k<4;k++) bits|=(u32)p[i*4+k]<<(k*8);
        union { u32 u; float f; } c; c.u=bits;
        out[i]=c.f;
    }
    return dims;
}
float vec_cosine_dist(const float *a, const float *b, int n){
    double dot=0, na=0, nb=0;
    for(int i=0;i<n;i++){ dot+=(double)a[i]*(double)b[i]; na+=(double)a[i]*(double)a[i]; nb+=(double)b[i]*(double)b[i]; }
    if(na==0||nb==0) return 1.0f;
    double sim=dot/((double)fsqrt_f((float)na)*(double)fsqrt_f((float)nb));
    if(sim>1) sim=1; if(sim<-1) sim=-1;
    return (float)(1.0-sim);
}
int eval_cossim_row(Table *t, u8 *row, int col, const float *q, int qn, float thresh){
    if(col<0||col>=t->ncols) return 0;
    if(t->cols[col].type!=COL_VECTOR) return 0;
    if(row_is_null(t,row,col)) return 0;
    if(qn!=t->cols[col].dims) return 0;
    float cur[RSC_VEC_MAX_DIMS];
    if(row_vec_get(t,row,col,cur,t->cols[col].dims)!=t->cols[col].dims) return 0;
    // epsilon: float32 rounding makes self-distance ~1e-8, so an exact
    // match would fail a threshold of 0 without tolerance.
    return vec_cosine_dist(cur,q,t->cols[col].dims)<=thresh+1e-6f;
}
// Distance only (2.0 = not comparable, sorts last). Scan pre-filters rows.
float cossim_dist_row(Table *t, u8 *row, int col, const float *q, int qn){
    if(col<0||col>=t->ncols) return 2.0f;
    if(t->cols[col].type!=COL_VECTOR) return 2.0f;
    if(row_is_null(t,row,col)) return 2.0f;
    if(qn!=t->cols[col].dims) return 2.0f;
    float cur[RSC_VEC_MAX_DIMS];
    if(row_vec_get(t,row,col,cur,t->cols[col].dims)!=t->cols[col].dims) return 2.0f;
    return vec_cosine_dist(cur,q,t->cols[col].dims);
}
// Validate s->has_cossim against table; parse query+threshold. 0 ok, -1 + ERR.
int cossim_prepare(Db *db, Stmt *s, Table *t, char *out, usize cap, float *qf, int *qn, float *thresh, int *qcol){
    (void)db;
    int c=find_col(t,s->cossim_col);
    if(c<0){ rsc_debug_unknown_column(db,s,s->cossim_col,"cossim"); set_col_error(out,cap,s->cossim_col); return -1; }
    if(t->cols[c].type!=COL_VECTOR){ set_cossim_error(out,cap,"ERR cossim column is not VECTOR"); return -1; }
    float th=0;
    if(parse_float(s->cossim_thresh,&th)!=0||th<0){ set_cossim_error(out,cap,"ERR invalid cossim threshold"); return -1; }
    int n=vec_parse(s->cossim_qvec,qf,RSC_VEC_MAX_DIMS);
    if(n!=t->cols[c].dims){ set_cossim_error(out,cap,"ERR cossim dimension mismatch"); return -1; }
    *qcol=c; *qn=n; *thresh=th;
    return 0;
}
// Byte-level single-column replace (no decode round-trip, vector-safe).
// newval is the raw literal (vector CSV without brackets). 0 ok, -1 bad.
int row_replace_col(Table *t, u8 *oldrow, int oldlen, int col, const char *newval, int new_is_null, u8 *newrow, int *newlen){
    if(col<0||col>=t->ncols||oldlen<0||oldlen>1024) return -1;
    u8 field[1024]; int flen=0;
    if(!new_is_null){
        if(t->cols[col].type==COL_INT){
            long v=parse_int_val(newval);
            for(int k=0;k<8;k++) field[flen++]=(u8)((v>>(k*8))&0xFF);
        } else if(t->cols[col].type==COL_VECTOR){
            if(vec_encode_str(newval,t->cols[col].dims,field)!=0) return -1;
            flen=t->cols[col].dims*4;
        } else {
            usize l=rsc_strlen(newval); if(l>255) l=255;
            field[0]=(u8)(l&0xFF); field[1]=(u8)((l>>8)&0xFF); flen=2;
            rsc_memcpy(field+2,newval,l); flen+=l;
        }
    }
    int base=t->has_nullmap?2:0;
    if(!t->has_nullmap&&new_is_null) return -1;
    // locate old field
    int off=base;
    for(int k=0;k<col;k++){
        if(t->has_nullmap&&((*(u16*)oldrow>>k)&1)) continue;
        if(t->cols[k].type==COL_INT) off+=8;
        else if(t->cols[k].type==COL_VECTOR) off+=(int)t->cols[k].dims*4;
        else { u16 l=*(u16*)(oldrow+off); off+=2+l; }
    }
    int oldflen=0;
    if(!(t->has_nullmap&&((*(u16*)oldrow>>col)&1))){
        if(t->cols[col].type==COL_INT) oldflen=8;
        else if(t->cols[col].type==COL_VECTOR) oldflen=(int)t->cols[col].dims*4;
        else oldflen=2+*(u16*)(oldrow+off);
    }
    if(off+oldflen>oldlen) return -1;
    int nlen=oldlen-oldflen+flen;
    if(nlen>1024) return -1;
    if(t->has_nullmap){
        u16 map=*(u16*)oldrow;
        if(new_is_null) map|=(u16)(1u<<col); else map&=(u16)~(1u<<col);
        newrow[0]=(u8)(map&0xFF); newrow[1]=(u8)((map>>8)&0xFF);
    }
    rsc_memcpy(newrow+base,oldrow+base,(usize)(off-base));
    rsc_memcpy(newrow+off,field,(usize)flen);
    rsc_memcpy(newrow+off+flen,oldrow+off+oldflen,(usize)(oldlen-off-oldflen));
    *newlen=nlen;
    return 0;
}
void set_vector_error(char *out, usize cap, const char *col){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,"ERR invalid vector for column '",cap-1);
    usize l=rsc_strlen(out);
    if(l+32<cap){ rsc_strcpy(out+l,col); l+=rsc_strlen(col); }
    if(l+3<cap) rsc_strcpy(out+l,"'\n");
    out[cap-1]=0;
}
void set_cossim_error(char *out, usize cap, const char *msg){
    if(!out||!cap) return;
    rsc_memset(out,0,cap);
    rsc_strncpy(out,msg,cap-1);
    usize l=rsc_strlen(out);
    if(l+1<cap){ out[l]='\n'; out[l+1]=0; }
    out[cap-1]=0;
}
