#include "sql.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
static int eqi(const char *a,const char *b){
    while(*a&&*b){ char ca=rsc_tolower(*a), cb=rsc_tolower(*b); if(ca!=cb) return 0; a++; b++; }
    return *a==0 && *b==0;
}
static void trim(char *s){ char *p=s; while(rsc_isspace(*p)) p++; if(p!=s) rsc_memmove(s,p,rsc_strlen(p)+1); usize n=rsc_strlen(s); while(n&&rsc_isspace(s[n-1])) s[--n]=0; }
static const char *skip_sp(const char *p){ while(rsc_isspace(*p)) p++; return p; }
static int parse_where(const char *p, Stmt *s){
    s->nwhere=0;
    while(*p){
        p=skip_sp(p); if(!*p||*p==';') break;
        char col[32]={0},op[3]={0},val[64]={0};
        int i=0; while(*p&&!rsc_isspace(*p)&&*p!='='&&*p!='<'&&*p!='>'&&*p!='!'&&i<31) col[i++]=*p++;
        col[i]=0; p=skip_sp(p);
        // IS [NOT] NULL
        {
            char w1[8]={0}; int wi=0; const char *qq=p;
            while(*qq&&!rsc_isspace(*qq)&&wi<7) w1[wi++]=rsc_tolower(*qq++);
            w1[wi]=0;
            if(eqi(w1,"is")){
                p=skip_sp(qq);
                char w2[8]={0}; wi=0; qq=p;
                while(*qq&&!rsc_isspace(*qq)&&wi<7) w2[wi++]=rsc_tolower(*qq++);
                w2[wi]=0;
                if(eqi(w2,"not")){
                    p=skip_sp(qq);
                    char w3[8]={0}; wi=0; qq=p;
                    while(*qq&&!rsc_isspace(*qq)&&wi<7) w3[wi++]=rsc_tolower(*qq++);
                    w3[wi]=0;
                    if(eqi(w3,"null")){
                        p=qq;
                        if(col[0] && s->nwhere<8){ rsc_strcpy(s->where[s->nwhere].col,col); s->where[s->nwhere].is_null_check=2; s->nwhere++; }
                        p=skip_sp(p);
                        char tmp[8]={0}; int ti=0; const char *qq2=p;
                        while(*qq2&&!rsc_isspace(*qq2)&&ti<7) tmp[ti++]=rsc_tolower(*qq2++);
                        tmp[ti]=0;
                        if(eqi(tmp,"and")) p=qq2; else if(eqi(tmp,"order")||eqi(tmp,"limit")) break; else if(*p==';') break;
                        continue;
                    }
                } else if(eqi(w2,"null")){
                    p=qq;
                    if(col[0] && s->nwhere<8){ rsc_strcpy(s->where[s->nwhere].col,col); s->where[s->nwhere].is_null_check=1; s->nwhere++; }
                    p=skip_sp(p);
                    char tmp[8]={0}; int ti=0; const char *qq2=p;
                    while(*qq2&&!rsc_isspace(*qq2)&&ti<7) tmp[ti++]=rsc_tolower(*qq2++);
                    tmp[ti]=0;
                    if(eqi(tmp,"and")) p=qq2; else if(eqi(tmp,"order")||eqi(tmp,"limit")) break; else if(*p==';') break;
                    continue;
                }
            }
        }
        if(*p=='='||*p=='<'||*p=='>'||*p=='!'){ int oi=0; op[oi++]=*p++; if(*p=='='||*p=='>') { if(oi<2) op[oi++]=*p++; } op[oi]=0; }
        p=skip_sp(p);
        int quoted=0;
        i=0; if(*p=='\''){ quoted=1; p++; while(*p&&*p!='\''&&i<63) val[i++]=*p++; if(*p=='\'') p++; } else { while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<63) val[i++]=*p++; } val[i]=0;
        int val_is_null=0;
        if(!quoted){
            char vl[64]={0}; for(int k=0;val[k]&&k<63;k++) vl[k]=rsc_tolower(val[k]);
            if(eqi(vl,"null")) val_is_null=1;
        }
        if(col[0]){
            if(s->nwhere>=8) break;
            rsc_strcpy(s->where[s->nwhere].col,col);
            // normalize <> to !=
            if(op[0]=='<'&&op[1]=='>'){ op[0]='!'; op[1]='='; op[2]=0; }
            rsc_strcpy(s->where[s->nwhere].op,op[0]?op:"=");
            rsc_strcpy(s->where[s->nwhere].val,val);
            s->where[s->nwhere].val_is_null=val_is_null;
            s->where[s->nwhere].is_null_check=0;
            s->nwhere++;
        }
        p=skip_sp(p);
        char tmp[8]={0}; int ti=0; const char *q=p; while(*q&&!rsc_isspace(*q)&&ti<7) tmp[ti++]=rsc_tolower(*q++); tmp[ti]=0;
        if(eqi(tmp,"and")) p=q; else if(eqi(tmp,"order")||eqi(tmp,"limit")) break; else if(*p==';') break;
    }
    return 0;
}
int sql_parse(const char *sql, Stmt *out){
    rsc_memset(out,0,sizeof(*out));
    rsc_strncpy(out->orig_sql, sql, 255); out->orig_sql[255]=0;
    trim(out->orig_sql);
    char buf[1024]; rsc_strncpy(buf,sql,1023); buf[1023]=0;
    const char *p=skip_sp(buf);
    char first[16]={0}; int i=0; while(*p&&!rsc_isspace(*p)&&i<15) first[i++]=rsc_tolower(*p++); first[i]=0;
    if(eqi(first,"create")){
        p=skip_sp(p); char second[16]={0}; i=0; while(*p&&!rsc_isspace(*p)&&i<15) second[i++]=rsc_tolower(*p++); second[i]=0;
        if(eqi(second,"table")){
            out->kind=STMT_CREATE;
            p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!='('&&i<31) out->table[i++]=*p++; out->table[i]=0;
            // manual find '('
            const char *q=p; while(*q&&*q!='(') q++;
            if(*q=='('){
                q++;
                char pk_name[32]={0};
                char uniq_name[32]={0};
                while(1){
                    q=skip_sp(q);
                    if(*q==')'||!*q) break;
                    // check for table-level PRIMARY KEY / UNIQUE
                    {
                        char w1[16]={0}; int wi=0; const char *qq=q;
                        while(*qq&&!rsc_isspace(*qq)&&*qq!=','&&*qq!=')'&&*qq!='('&&wi<15) w1[wi++]=rsc_tolower(*qq++);
                        w1[wi]=0;
                        if(eqi(w1,"primary")){
                            q=qq; q=skip_sp(q);
                            char w2[16]={0}; wi=0; while(*q&&!rsc_isspace(*q)&&*q!=','&&*q!=')'&&*q!='('&&wi<15) w2[wi++]=rsc_tolower(*q++);
                            w2[wi]=0;
                            if(eqi(w2,"key")){
                                q=skip_sp(q);
                                if(*q=='('){
                                    q++; q=skip_sp(q);
                                    int pi=0; while(*q&&*q!=')'&&*q!=','&&pi<31 && !rsc_isspace(*q)) pk_name[pi++]=*q++;
                                    pk_name[pi]=0; trim(pk_name);
                                    while(*q&&*q!=')'&&*q!=',') q++;
                                    if(*q==')') q++;
                                }
                                q=skip_sp(q);
                                if(*q==',') q++;
                                continue;
                            }
                        } else if(eqi(w1,"unique")){
                            q=qq; q=skip_sp(q);
                            if(*q=='('){
                                q++; q=skip_sp(q);
                                int pi=0; while(*q&&*q!=')'&&*q!=','&&pi<31 && !rsc_isspace(*q)) uniq_name[pi++]=*q++;
                                uniq_name[pi]=0; trim(uniq_name);
                                while(*q&&*q!=')'&&*q!=',') q++;
                                if(*q==')') q++;
                            }
                            q=skip_sp(q);
                            if(*q==',') q++;
                            continue;
                        }
                    }
                    char cn[32]={0}; int ci=0;
                    while(*q&&*q!=')'&&!rsc_isspace(*q)&&*q!=','&&ci<31) cn[ci++]=*q++;
                    cn[ci]=0; q=skip_sp(q);
                    char ct[16]={0}; int cti=0;
                    while(*q&&!rsc_isspace(*q)&&*q!=','&&*q!=')'&&cti<15) ct[cti++]=rsc_tolower(*q++);
                    ct[cti]=0;
                    int is_pk=0, is_unique=0, is_not_null=0, has_default=0, default_is_null=0;
                    char default_val[64]={0};
                    while(1){
                        const char *qs=skip_sp(q);
                        char w1[16]={0}; int wi=0; const char *qq=qs;
                        while(*qq&&!rsc_isspace(*qq)&&*qq!=','&&*qq!=')'&&wi<15) w1[wi++]=rsc_tolower(*qq++);
                        w1[wi]=0;
                        if(!w1[0]) break;
                        if(eqi(w1,"primary")){
                            const char *qr=skip_sp(qq);
                            char w2[16]={0}; wi=0; while(*qr&&!rsc_isspace(*qr)&&*qr!=','&&*qr!=')'&&wi<15) w2[wi++]=rsc_tolower(*qr++);
                            w2[wi]=0;
                            if(eqi(w2,"key")){ is_pk=1; q=qr; continue; }
                            break;
                        } else if(eqi(w1,"unique")){
                            is_unique=1; q=qq; continue;
                        } else if(eqi(w1,"not")){
                            const char *qr=skip_sp(qq);
                            char w2[16]={0}; wi=0; while(*qr&&!rsc_isspace(*qr)&&*qr!=','&&*qr!=')'&&wi<15) w2[wi++]=rsc_tolower(*qr++);
                            w2[wi]=0;
                            if(eqi(w2,"null")){ is_not_null=1; q=qr; continue; }
                            break;
                        } else if(eqi(w1,"null")){
                            q=qq; continue;
                        } else if(eqi(w1,"default")){
                            const char *qr=skip_sp(qq);
                            has_default=1;
                            if(*qr=='\''){
                                qr++; int di=0;
                                while(*qr&&*qr!='\''&&di<63) default_val[di++]=*qr++;
                                default_val[di]=0;
                                if(*qr=='\'') qr++;
                                default_is_null=0;
                            } else {
                                char dw[64]={0}; int di=0;
                                while(*qr&&!rsc_isspace(*qr)&&*qr!=','&&*qr!=')'&&di<63) dw[di++]=*qr++;
                                dw[di]=0;
                                char dwl[64]={0}; for(int k=0;dw[k]&&k<63;k++) dwl[k]=rsc_tolower(dw[k]);
                                if(eqi(dwl,"null")){ default_is_null=1; default_val[0]=0; }
                                else { default_is_null=0; rsc_strcpy(default_val,dw); }
                            }
                            q=qr; continue;
                        }
                        break;
                    }
                    if(is_pk){ is_unique=1; is_not_null=1; }
                    if(cn[0] && out->ncols<SQL_MAX_COLS){
                        rsc_strcpy(out->cols[out->ncols].name,cn);
                        out->cols[out->ncols].type=eqi(ct,"int")?COL_INT:COL_TEXT;
                        out->cols[out->ncols].is_pk=is_pk;
                        out->cols[out->ncols].is_unique=is_unique;
                        out->cols[out->ncols].is_not_null=is_not_null;
                        out->cols[out->ncols].has_default=has_default;
                        out->cols[out->ncols].default_is_null=default_is_null;
                        rsc_strcpy(out->cols[out->ncols].default_val,default_val);
                        out->ncols++;
                    }
                    while(*q&&rsc_isspace(*q)) q++;
                    if(*q==',') q++;
                    else if(*q==')') break;
                }
                if(pk_name[0]){
                    for(int k=0;k<out->ncols;k++) if(rsc_strcmp(out->cols[k].name,pk_name)==0){ out->cols[k].is_pk=1; out->cols[k].is_unique=1; out->cols[k].is_not_null=1; }
                }
                if(uniq_name[0]){
                    for(int k=0;k<out->ncols;k++) if(rsc_strcmp(out->cols[k].name,uniq_name)==0) out->cols[k].is_unique=1;
                }
            }
            return 0;
        } else if(eqi(second,"index")){
            out->kind=STMT_CREATE_IDX;
            p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&i<31) out->idx_name[i++]=*p++; out->idx_name[i]=0;
            while(*p&&rsc_isspace(*p)) p++;
            char tmp[8]={0}; int ti=0; const char *qq=p; while(*qq&&!rsc_isspace(*qq)&&ti<7) tmp[ti++]=rsc_tolower(*qq++); if(eqi(tmp,"on")) p=qq;
            p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!='('&&i<31) out->table[i++]=*p++; out->table[i]=0;
            const char *qq2=p; while(*qq2&&*qq2!='(') qq2++; if(*qq2=='('){ qq2++; qq2=skip_sp(qq2); i=0; while(*qq2&&*qq2!=')'&&i<31) out->idx_col[i++]=*qq2++; out->idx_col[i]=0; }
            return 0;
        } else if(eqi(second,"database")){
            out->kind=STMT_CREATE_DB;
            p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0;
            for(int k=0;out->table[k];k++) if(out->table[k]==';'){ out->table[k]=0; break; }
            trim(out->table);
            return 0;
        }
    } else if(eqi(first,"drop")){
        p=skip_sp(p); char second2[16]={0}; i=0; while(*p&&!rsc_isspace(*p)&&i<15) second2[i++]=rsc_tolower(*p++); second2[i]=0;
        if(eqi(second2,"database")){
            out->kind=STMT_DROP_DB;
            p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0;
            for(int k=0;out->table[k];k++) if(out->table[k]==';'){ out->table[k]=0; break; }
            trim(out->table); return 0;
        }
    } else if(eqi(first,"use")){
        out->kind=STMT_USE;
        p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0;
        for(int k=0;out->table[k];k++) if(out->table[k]==';'){ out->table[k]=0; break; }
        trim(out->table); return 0;
    } else if(eqi(first,"dump")){
        out->kind=STMT_DUMP_ALL;
        p=skip_sp(p);
        if(*p=='\'' || *p==0){ }
        else {
            const char *q=p; char tmp2[16]={0}; i=0; while(*q&&!rsc_isspace(*q)&&i<15) tmp2[i++]=rsc_tolower(*q++); tmp2[i]=0;
            if(eqi(tmp2,"all")){
                p=q; p=skip_sp(p);
                if(*p!='\'' && *p!=0 && *p!=';'){
                    const char *r=p; char to[8]={0}; int ti=0; while(*r&&!rsc_isspace(*r)&&ti<7) to[ti++]=rsc_tolower(*r++); to[ti]=0;
                    if(eqi(to,"to")){ p=r; p=skip_sp(p); }
                }
            } else if(eqi(tmp2,"to")){
                p=q; p=skip_sp(p);
            } else if(tmp2[0]){
                p=q;
            }
        }
        p=skip_sp(p);
        if(*p=='\''){ p++; i=0; while(*p&&*p!='\''&&i<31) out->table[i++]=*p++; out->table[i]=0; if(*p=='\'') p++; } else { i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0; for(int k=0;out->table[k];k++) if(out->table[k]==';'){ out->table[k]=0; break; } }
        trim(out->table); return 0;
    } else if(eqi(first,"load")){
        out->kind=STMT_LOAD_ALL;
        p=skip_sp(p);
        if(*p=='\'' || *p==0){ }
        else {
            const char *q=p; char tmp2[16]={0}; i=0; while(*q&&!rsc_isspace(*q)&&i<15) tmp2[i++]=rsc_tolower(*q++); tmp2[i]=0;
            if(eqi(tmp2,"all")){
                p=q; p=skip_sp(p);
                if(*p!='\'' && *p!=0 && *p!=';'){
                    const char *r=p; char from[8]={0}; int ti=0; while(*r&&!rsc_isspace(*r)&&ti<7) from[ti++]=rsc_tolower(*r++); from[ti]=0;
                    if(eqi(from,"from")){ p=r; p=skip_sp(p); }
                }
            } else if(eqi(tmp2,"from")){
                p=q; p=skip_sp(p);
            } else if(tmp2[0]){
                p=q;
            }
        }
        p=skip_sp(p);
        if(*p=='\''){ p++; i=0; while(*p&&*p!='\''&&i<31) out->table[i++]=*p++; out->table[i]=0; if(*p=='\'') p++; } else { i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0; for(int k=0;out->table[k];k++) if(out->table[k]==';'){ out->table[k]=0; break; } }
        trim(out->table); return 0;
    } else if(eqi(first,"insert")){
        out->kind=STMT_INSERT;
        // find INTO
        const char *q=p; char tok[16]={0}; // skip "into"
        // naive: find table name after "into"
        // search for "into"
        const char *search=buf; char low[1024]; for(int k=0;buf[k];k++) low[k]=rsc_tolower(buf[k]); low[rsc_strlen(buf)]=0;
        const char *pos=0; for(const char *t=low;*t;t++) if(t[0]=='i'&&t[1]=='n'&&t[2]=='t'&&t[3]=='o'){pos=t;break;}
        if(pos){ int off=pos-low; p=buf+off+4; }
        p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!='('&&*p!='v'&&i<31) out->table[i++]=*p++; out->table[i]=0;
        trim(out->table);
        // if table incorrectly includes "values", fix
        // find VALUES
        for(const char *t=low;*t;t++) if(t[0]=='v'&&t[1]=='a'&&t[2]=='l'&&t[3]=='u'&&t[4]=='e'&&t[5]=='s'){ int off=t-low; p=buf+off+6; break; }
        p=skip_sp(p); if(*p=='(') p++;
        // parse vals
        out->nvals=0;
        while(*p&&*p!=')'&&out->nvals<SQL_MAX_COLS){
            int cur=out->nvals;
            out->vals_is_null[cur]=0; out->vals_is_default[cur]=0;
            p=skip_sp(p); if(*p==',') {p++; continue;} if(*p==')') break;
            if(*p=='\''){p++; i=0; while(*p&&*p!='\''&&i<63) out->vals[cur][i++]=*p++; out->vals[cur][i]=0; if(*p=='\'') p++; }
            else {
                i=0; while(*p&&*p!=','&&*p!=')'&&i<63) out->vals[cur][i++]=*p++;
                out->vals[cur][i]=0; trim(out->vals[cur]);
                char vl[64]={0}; for(int k=0;out->vals[cur][k]&&k<63;k++) vl[k]=rsc_tolower(out->vals[cur][k]);
                if(eqi(vl,"null")) out->vals_is_null[cur]=1;
                else if(eqi(vl,"default")) out->vals_is_default[cur]=1;
            }
            out->nvals++;
            p=skip_sp(p); if(*p==',') p++;
        }
        return 0;
    } else if(eqi(first,"select")){
        out->kind=STMT_SELECT;
        char low[1024]; for(int k=0;buf[k];k++) low[k]=rsc_tolower(buf[k]); low[rsc_strlen(buf)]=0;
        const char *from=0; for(const char *t=low;*t;t++) if(t[0]=='f'&&t[1]=='r'&&t[2]=='o'&&t[3]=='m'){from=t;break;}
        {
            int sel_start=(int)(p-buf);
            int sel_end=from? (int)(from-low) : (int)rsc_strlen(buf);
            char sel[256]={0}; int si=0;
            for(int k=sel_start;k<sel_end&&si<255;k++) sel[si++]=buf[k]; sel[si]=0;
            char *s=sel; while(rsc_isspace(*s)) s++;
            char sl[256]={0}; for(int k=0;s[k];k++) sl[k]=rsc_tolower(s[k]);
            char *slp=sl; while(rsc_isspace(*slp)) slp++;
            if(rsc_strncmp(slp,"count",5)==0){
                out->is_count=1;
                char *po=0,*pc=0; for(char *c=slp;*c;c++){ if(*c=='('&&!po) po=c; if(*c==')') pc=c; }
                if(po&&pc){ int ci=0; for(char *c=po+1;c<pc&&ci<31;c++) if(!rsc_isspace(*c)) out->agg_col[ci++]=rsc_tolower(*c); out->agg_col[ci]=0; }
                else rsc_strcpy(out->agg_col,"*");
            } else if(rsc_strncmp(slp,"avg",3)==0){
                out->is_avg=1;
                char *po=0,*pc=0; for(char *c=slp;*c;c++){ if(*c=='('&&!po) po=c; if(*c==')') pc=c; }
                if(po&&pc){ int ci=0; for(char *c=po+1;c<pc&&ci<31;c++) if(!rsc_isspace(*c)) out->agg_col[ci++]=*c; out->agg_col[ci]=0; }
            } else {
                char tmp[256]; rsc_strcpy(tmp, s);
                trim(tmp);
                if(tmp[0]=='*' && (tmp[1]==0 || rsc_isspace(tmp[1]))){
                    out->is_star=1;
                } else if(tmp[0]){
                    out->nselect=0;
                    char *cur=tmp;
                    while(*cur && out->nselect<16){
                        while(rsc_isspace(*cur)) cur++;
                        if(!*cur) break;
                        char *end=cur;
                        while(*end && *end!=',') end++;
                        char col[32]={0}; int ci=0;
                        for(char *c=cur;c<end && ci<31;c++){
                            if(!rsc_isspace(*c)) col[ci++]=*c;
                        }
                        col[ci]=0;
                        // strip trailing ; if any
                        for(int k=0;col[k];k++) if(col[k]==';'){ col[k]=0; break; }
                        trim(col);
                        if(col[0]){
                            rsc_strcpy(out->select_cols[out->nselect], col);
                            out->nselect++;
                        }
                        if(*end==',') cur=end+1;
                        else break;
                    }
                    if(out->nselect==1 && rsc_strcmp(out->select_cols[0],"*")==0){
                        out->is_star=1; out->nselect=0;
                    }
                }
            }
        }
        if(from){ int off=from-low; p=buf+off+4; p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0; p=skip_sp(p);
            // WHERE
            char nxt[8]={0}; int ti=0; const char *qq=p; while(*qq&&!rsc_isspace(*qq)&&ti<7) nxt[ti++]=rsc_tolower(*qq++); nxt[ti]=0;
            if(eqi(nxt,"where")){ p=qq; p=skip_sp(p); // parse where until order/limit/;
                // need to slice where part
                char where_buf[512]={0}; int wi=0;
                // copy until order/limit/;
                const char *wp=p;
                // find order/limit pos in low
                int where_start=p-buf;
                int where_end=rsc_strlen(buf);
                for(const char *t=low+where_start;*t;t++){ if((t[0]=='o'&&t[1]=='r'&&t[2]=='d'&&t[3]=='e'&&t[4]=='r')||(t[0]=='l'&&t[1]=='i'&&t[2]=='m'&&t[3]=='i'&&t[4]=='t')||*t==';'){ where_end=t-low; break; } }
                for(int k=where_start;k<where_end&&wi<511;k++) where_buf[wi++]=buf[k]; where_buf[wi]=0;
                parse_where(where_buf,out);
                p=buf+where_end;
            }
            p=skip_sp(p);
            // ORDER BY
            char ob[8]={0}; ti=0; qq=p; while(*qq&&!rsc_isspace(*qq)&&ti<7) ob[ti++]=rsc_tolower(*qq++); ob[ti]=0;
            if(eqi(ob,"order")){ // skip order by
                p=qq; p=skip_sp(p); // skip "by"
                char by[8]={0}; ti=0; qq=p; while(*qq&&!rsc_isspace(*qq)&&ti<7) by[ti++]=rsc_tolower(*qq++); by[ti]=0; if(eqi(by,"by")) p=qq;
                p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->order_by[i++]=*p++; out->order_by[i]=0; out->has_order=1; p=skip_sp(p);
            }
            // LIMIT
            char lim[8]={0}; ti=0; qq=p; while(*qq&&!rsc_isspace(*qq)&&ti<7) lim[ti++]=rsc_tolower(*qq++); lim[ti]=0;
            if(eqi(lim,"limit")){ p=qq; p=skip_sp(p); char nb[16]={0}; i=0; while(*p&&rsc_isdigit(*p)&&i<15) nb[i++]=*p++; nb[i]=0; int v=0; for(int k=0;nb[k];k++) v=v*10+(nb[k]-'0'); out->limit=v; out->has_limit=1; }
        }
        return 0;
    } else if(eqi(first,"delete")){
        out->kind=STMT_DELETE;
        char low[1024]; for(int k=0;buf[k];k++) low[k]=rsc_tolower(buf[k]); low[rsc_strlen(buf)]=0;
        const char *from=0; for(const char *t=low;*t;t++) if(t[0]=='f'&&t[1]=='r'&&t[2]=='o'&&t[3]=='m'){from=t;break;}
        if(from){ int off=from-low; p=buf+off+4; p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<31) out->table[i++]=*p++; out->table[i]=0; p=skip_sp(p);
            char nxt[8]={0}; int ti=0; const char *qq=p; while(*qq&&!rsc_isspace(*qq)&&ti<7) nxt[ti++]=rsc_tolower(*qq++); nxt[ti]=0;
            if(eqi(nxt,"where")){ p=qq; char wb[512]={0}; int wi=0; int ws=p-buf; int we=rsc_strlen(buf); for(int k=ws;k<we&&wi<511;k++) wb[wi++]=buf[k]; wb[wi]=0; parse_where(wb,out); }
        }
        return 0;
    } else if(eqi(first,"update")){
        out->kind=STMT_UPDATE;
        p=skip_sp(p); i=0; while(*p&&!rsc_isspace(*p)&&i<31) out->table[i++]=*p++; out->table[i]=0;
        char low[1024]; for(int k=0;buf[k];k++) low[k]=rsc_tolower(buf[k]); low[rsc_strlen(buf)]=0;
        const char *set=0; for(const char *t=low;*t;t++) if(t[0]=='s'&&t[1]=='e'&&t[2]=='t'){set=t;break;}
        if(set){ int off=set-low; p=buf+off+3; p=skip_sp(p); // parse col=val
            char col[32]={0},val[64]={0}; i=0; while(*p&&*p!='='&&i<31) col[i++]=*p++; col[i]=0; trim(col); if(*p=='=') p++; p=skip_sp(p);
            int quoted=0;
            if(*p=='\''){quoted=1; p++; i=0; while(*p&&*p!='\''&&i<63) val[i++]=*p++; val[i]=0; if(*p=='\'') p++;}
            else {i=0; while(*p&&!rsc_isspace(*p)&&*p!=';'&&i<63) val[i++]=*p++; val[i]=0;} trim(val);
            // store as first val with col name in where? reuse vals
            rsc_strcpy(out->cols[0].name,col); rsc_strcpy(out->vals[0],val); out->ncols=1; out->nvals=1;
            out->vals_is_null[0]=0; out->vals_is_default[0]=0;
            if(!quoted){
                char vl[64]={0}; for(int k=0;val[k]&&k<63;k++) vl[k]=rsc_tolower(val[k]);
                if(eqi(vl,"null")) out->vals_is_null[0]=1;
                else if(eqi(vl,"default")) out->vals_is_default[0]=1;
            }
            // where
            for(const char *t=low+(p-buf);*t;t++) if(t[0]=='w'&&t[1]=='h'&&t[2]=='e'&&t[3]=='r'&&t[4]=='e'){ int off2=t-low; p=buf+off2+5; char wb[512]={0}; int wi=0; int ws=p-buf; int we=rsc_strlen(buf); for(int k=ws;k<we&&wi<511;k++) wb[wi++]=buf[k]; wb[wi]=0; parse_where(wb,out); break; }
        }
        return 0;
    }
    out->kind=STMT_UNKNOWN; return -1;
}

