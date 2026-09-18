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
int vec_encode_str(const char *s, int dims, int quant, u8 *out);
int vec_storage_bytes(int dims, int quant);
const char *vec_quant_name(int quant);
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
        else if(t->cols[k].type==COL_VECTOR) p+=(usize)vec_storage_bytes(t->cols[k].dims,t->cols[k].quant);
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
        // display as [f,f,...], truncated to 63 chars like TEXT (dequantized)
        int dims=t->cols[col].dims;
        float dec[RSC_VEC_MAX_DIMS];
        if(row_vec_get(t,row,col,dec,dims)!=dims){ dst[0]=0; return; }
        int pos=0;
        if(pos<63) dst[pos++]='[';
        for(int d=0;d<dims && pos<63;d++){
            char fb[32]; format_float(dec[d],fb,sizeof(fb));
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
            if(vec_encode_str(vals[i],t->cols[i].dims,t->cols[i].quant,out+off)!=0) return -1;
            off+=vec_storage_bytes(t->cols[i].dims,t->cols[i].quant);
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
        if(vec_encode_str(val_str,t->cols[col].dims,t->cols[col].quant,want_vec)!=0) return 0;
        want_vec_len=vec_storage_bytes(t->cols[col].dims,t->cols[col].quant);
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
static double dsqrt_d(double a){
    if(!(a>0)) return 0;
    union { double d; u64 u; } v = {a};
    int exp = (int)((v.u>>52)&0x7FFu);
    if(exp==0x7FF) return 0;
    if(exp==0){
        v.d = a * 4503599627370496.0;
        exp = (int)((v.u>>52)&0x7FFu);
        int ne = ((exp-1023-52)>>1)+1023;
        v.u = ((u64)(ne)<<52) | (v.u & 0xFFFFFFFFFFFFFull);
        double x = v.d;
        for(int i=0;i<20;i++){ double nx=0.5*(x+a/x); if(nx==x) break; x=nx; }
        return x;
    }
    int ne = ((exp-1023)>>1)+1023;
    union { double d; u64 u; } g;
    g.u = ((u64)ne<<52);
    double x = g.d;
    for(int i=0;i<20;i++){ double nx=0.5*(x+a/x); if(nx==x) break; x=nx; }
    return x;
}
float fsqrt_f(float a){
    if(a<=0) return 0;
    return (float)dsqrt_d((double)a);
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
int vec_storage_bytes(int dims, int quant){
    if(dims<=0) return 0;
    if(dims>RSC_VEC_MAX_DIMS) dims=RSC_VEC_MAX_DIMS;
    switch(quant){
        case VQ_FP16: return dims*2;
        case VQ_Q8: return 4+dims;
        case VQ_Q4: return 4+(dims+1)/2;
        case VQ_Q2: return 4+(dims+3)/4;
        case VQ_Q1: return 4+(dims+7)/8;
        default: return dims*4;
    }
}
const char *vec_quant_name(int quant){
    switch(quant){
        case VQ_FP16: return "FP16";
        case VQ_Q8: return "Q8";
        case VQ_Q4: return "Q4";
        case VQ_Q2: return "Q2";
        case VQ_Q1: return "Q1";
        default: return "FP32";
    }
}
// IEEE-754 binary16, round-to-nearest-even, integer ops only (freestanding).
static u16 f32_to_f16_bits(float f){
    union { float f; u32 u; } v; v.f=f;
    u32 x=v.u;
    u32 sign=(x>>16)&0x8000u;
    u32 exp=(x>>23)&0xFFu;
    u32 mant=x&0x7FFFFFu;
    if(exp==255u){
        if(mant==0) return (u16)(sign|0x7C00u);
        return (u16)(sign|0x7E00u);
    }
    int he=(int)exp-112;
    if(he>=31) return (u16)(sign|0x7C00u);
    if(he<=0){
        if(he<-10) return (u16)sign;
        mant|=0x800000u;
        int shift=14-he;
        u32 half=mant>>shift;
        u32 rem=mant&(((u32)1<<shift)-1u);
        u32 halfway=(u32)1<<(shift-1);
        if(rem>halfway||(rem==halfway&&(half&1u))) half++;
        return (u16)(sign|half);
    }
    u32 half=((u32)he<<10)|(mant>>13);
    u32 rem=mant&0x1FFFu;
    if(rem>0x1000u||(rem==0x1000u&&(half&1u))) half++;
    return (u16)(sign|half);
}
static float f16_bits_to_f32(u16 h){
    u32 sign=((u32)h&0x8000u)<<16;
    u32 exp=(((u32)h>>10)&0x1Fu);
    u32 mant=(u32)h&0x3FFu;
    u32 f;
    if(exp==0){
        if(mant==0) f=sign;
        else {
            int e=113;
            while(!(mant&0x400u)){ mant<<=1; e--; }
            mant&=0x3FFu;
            f=sign|((u32)e<<23)|(mant<<13);
        }
    } else if(exp==31){
        f=sign|0x7F800000u|(mant<<13);
    } else {
        f=sign|((exp+112u)<<23)|(mant<<13);
    }
    union { u32 u; float f; } v; v.u=f;
    return v.f;
}
static void store_f32_le(u8 *out, float f){
    union { float f; u32 u; } c; c.f=f;
    out[0]=(u8)(c.u&0xFF); out[1]=(u8)((c.u>>8)&0xFF);
    out[2]=(u8)((c.u>>16)&0xFF); out[3]=(u8)((c.u>>24)&0xFF);
}
static float load_f32_le(const u8 *p){
    u32 b=(u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
    union { u32 u; float f; } c; c.u=b;
    return c.f;
}
// Q2 codebook: symmetric 4 levels, ties resolve to the lower index.
static double q2_level(int code){
    static const double lv[4]={-1.0,-0.3333333333333333,0.3333333333333333,1.0};
    if(code<0) code=0; if(code>3) code=3;
    return lv[code];
}
// Encode "1.0,2.0" into vec_storage_bytes(dims,quant) bytes. 0 ok, -1 bad.
// FP32/FP16 are direct; Q8/Q4/Q2/Q1 store an FP32-LE scale (max abs,
// 0 when the vector is all zeros) followed by packed codes.
int vec_encode_str(const char *s, int dims, int quant, u8 *out){
    if(dims<=0||dims>RSC_VEC_MAX_DIMS) return -1;
    if(quant<VQ_FP32||quant>VQ_Q1) quant=VQ_FP32;
    float tmp[RSC_VEC_MAX_DIMS];
    int n=vec_parse(s,tmp,dims);
    if(n!=dims) return -1;
    if(quant==VQ_FP32){
        for(int i=0;i<dims;i++) store_f32_le(out+i*4,tmp[i]);
        return 0;
    }
    if(quant==VQ_FP16){
        for(int i=0;i<dims;i++){
            u16 h=f32_to_f16_bits(tmp[i]);
            out[i*2]=(u8)(h&0xFF); out[i*2+1]=(u8)((h>>8)&0xFF);
        }
        return 0;
    }
    double mx=0;
    for(int i=0;i<dims;i++){ double a=(double)tmp[i]; if(a<0) a=-a; if(a>mx) mx=a; }
    float scale=(float)mx;
    store_f32_le(out,scale);
    u8 *d=out+4;
    int paylen=vec_storage_bytes(dims,quant)-4;
    for(int i=0;i<paylen;i++) d[i]=0;
    if(scale==0) return 0;
    if(quant==VQ_Q8){
        for(int i=0;i<dims;i++){
            double nrm=(double)tmp[i]/(double)scale;
            if(nrm>1) nrm=1; if(nrm<-1) nrm=-1;
            int q=(int)(nrm*127.0+(nrm>=0?0.5:-0.5));
            if(q>127) q=127; if(q<-127) q=-127;
            d[i]=(u8)(q&0xFF);
        }
        return 0;
    }
    if(quant==VQ_Q4){
        for(int i=0;i<dims;i++){
            double nrm=(double)tmp[i]/(double)scale;
            if(nrm>1) nrm=1; if(nrm<-1) nrm=-1;
            int q=(int)(nrm*7.0+(nrm>=0?0.5:-0.5));
            if(q>7) q=7; if(q<-7) q=-7;
            if((i&1)==0) d[i>>1]=(u8)(d[i>>1]|(q&0xF));
            else d[i>>1]=(u8)(d[i>>1]|((q&0xF)<<4));
        }
        return 0;
    }
    if(quant==VQ_Q2){
        for(int i=0;i<dims;i++){
            double nrm=(double)tmp[i]/(double)scale;
            if(nrm>1) nrm=1; if(nrm<-1) nrm=-1;
            int best=0; double bd=4.0;
            for(int k=0;k<4;k++){ double dd=nrm-q2_level(k); if(dd<0) dd=-dd; if(dd<bd){ bd=dd; best=k; } }
            d[i>>2]=(u8)(d[i>>2]|(best<<(2*(i&3))));
        }
        return 0;
    }
    for(int i=0;i<dims;i++){
        int bit=(tmp[i]>=0)?1:0;
        d[i>>3]=(u8)(d[i>>3]|(bit<<(i&7)));
    }
    return 0;
}
static int vec_decode_payload(const u8 *p, int dims, int quant, float *out){
    if(quant<VQ_FP32||quant>VQ_Q1) quant=VQ_FP32;
    if(quant==VQ_FP32){
        for(int i=0;i<dims;i++) out[i]=load_f32_le(p+i*4);
        return dims;
    }
    if(quant==VQ_FP16){
        for(int i=0;i<dims;i++){
            u16 h=(u16)((u16)p[i*2]|((u16)p[i*2+1]<<8));
            out[i]=f16_bits_to_f32(h);
        }
        return dims;
    }
    float scale=load_f32_le(p);
    const u8 *d=p+4;
    if(quant==VQ_Q8){
        for(int i=0;i<dims;i++){
            int q=(int)((signed char)d[i]);
            out[i]=(float)((double)q/127.0*(double)scale);
        }
        return dims;
    }
    if(quant==VQ_Q4){
        for(int i=0;i<dims;i++){
            u8 b=d[i>>1];
            int q=((i&1)==0)?(b&0xF):((b>>4)&0xF);
            if(q&8) q-=16;
            out[i]=(float)((double)q/7.0*(double)scale);
        }
        return dims;
    }
    if(quant==VQ_Q2){
        for(int i=0;i<dims;i++){
            int code=(d[i>>2]>>(2*(i&3)))&3;
            out[i]=(float)(q2_level(code)*(double)scale);
        }
        return dims;
    }
    for(int i=0;i<dims;i++){
        int bit=(d[i>>3]>>(i&7))&1;
        out[i]=bit?scale:-scale;
    }
    return dims;
}
int row_vec_get(Table *t, u8 *row, int col, float *out, int maxn){
    if(col<0||col>=t->ncols) return -1;
    if(t->cols[col].type!=COL_VECTOR) return -1;
    if(row_is_null(t,row,col)) return -1;
    int dims=t->cols[col].dims;
    if(dims>maxn) return -1;
    u8 *p=row_col_ptr(t,row,col);
    return vec_decode_payload(p,dims,t->cols[col].quant,out);
}
float vec_cosine_dist(const float *a, const float *b, int n){
    double dot=0, na=0, nb=0;
    for(int i=0;i<n;i++){ dot+=(double)a[i]*(double)b[i]; na+=(double)a[i]*(double)a[i]; nb+=(double)b[i]*(double)b[i]; }
    if(na==0||nb==0) return 1.0f;
    double sim=dot/(dsqrt_d(na)*dsqrt_d(nb));
    if(sim>1.0) sim=1.0; if(sim<-1.0) sim=-1.0;
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
            if(vec_encode_str(newval,t->cols[col].dims,t->cols[col].quant,field)!=0) return -1;
            flen=vec_storage_bytes(t->cols[col].dims,t->cols[col].quant);
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
        else if(t->cols[k].type==COL_VECTOR) off+=(int)vec_storage_bytes(t->cols[k].dims,t->cols[k].quant);
        else { u16 l=*(u16*)(oldrow+off); off+=2+l; }
    }
    int oldflen=0;
    if(!(t->has_nullmap&&((*(u16*)oldrow>>col)&1))){
        if(t->cols[col].type==COL_INT) oldflen=8;
        else if(t->cols[col].type==COL_VECTOR) oldflen=(int)vec_storage_bytes(t->cols[col].dims,t->cols[col].quant);
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
