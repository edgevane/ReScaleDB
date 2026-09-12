#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
static int find_table(Db *db,const char *name){ for(int i=0;i<db->ntables;i++) if(rsc_strcmp(db->tables[i].name,name)==0) return i; return -1; }
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
        Table *t=&db->tables[db->ntables++];
        rsc_strcpy(t->name,s->table);
        t->ncols=s->ncols; for(int i=0;i<s->ncols;i++) t->cols[i]=s->cols[i];
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
        int idx=find_table(db,s->table); if(idx<0) return -1;
        Table *t=&db->tables[idx];
        if(s->nvals != t->ncols) return -1;
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
        int idx=find_table(db,s->table); if(idx<0) return -1;
        Table *t=&db->tables[idx];
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
        int lim = s->has_limit? s->limit : ctx.nrows;
        if(lim>ctx.nrows) lim=ctx.nrows;
        int widths[SQL_MAX_COLS]={0};
        for(int c=0;c<t->ncols;c++) widths[c]=(int)rsc_strlen(t->cols[c].name);
        char cells[256][16][64];
        for(int r=0;r<lim;r++){
            u8 *row=ctx.rows[r];
            u8 *p=row;
            for(int c=0;c<t->ncols;c++){
                char *dst=cells[r][c];
                if(t->cols[c].type==COL_INT){
                    i64 v=0; for(int k=0;k<8;k++) v|=(i64)p[k]<<(k*8);
                    int pos=0; int neg=0; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) dst[pos++]=rev[k]; dst[pos]=0; p+=8;
                } else { u16 l=*(u16*)p; if(l>63) l=63; rsc_memcpy(dst,p+2,l); dst[l]=0; p+=2+l; }
                int cl=(int)rsc_strlen(dst);
                if(cl>widths[c]) widths[c]=cl;
            }
        }
        usize off=0;
        #define OUTC(c) do{ if(off+1<cap) out[off++]=c; }while(0)
        #define OUTS(s) do{ usize _l=rsc_strlen(s); if(off+_l<cap){ rsc_memcpy(out+off,s,_l); off+=_l; } }while(0)
        #define OUTN(s,n) do{ if(off+(n)<cap){ rsc_memcpy(out+off,s,n); off+=n; } }while(0)
        if(lim==0 && ctx.nrows==0){
            OUTS("(empty)\n"); if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
        }
        for(int c=0;c<t->ncols;c++){ OUTC('+'); for(int k=0;k<widths[c]+2;k++) OUTC('-'); } OUTC('+'); OUTC('\n');
        for(int c=0;c<t->ncols;c++){
            OUTC('|'); OUTC(' ');
            OUTS(t->cols[c].name);
            for(int k=(int)rsc_strlen(t->cols[c].name);k<widths[c];k++) OUTC(' ');
            OUTC(' ');
        } OUTC('|'); OUTC('\n');
        for(int c=0;c<t->ncols;c++){ OUTC('+'); for(int k=0;k<widths[c]+2;k++) OUTC('-'); } OUTC('+'); OUTC('\n');
        for(int r=0;r<lim;r++){
            for(int c=0;c<t->ncols;c++){
                OUTC('|'); OUTC(' ');
                OUTS(cells[r][c]);
                for(int k=(int)rsc_strlen(cells[r][c]);k<widths[c];k++) OUTC(' ');
                OUTC(' ');
            } OUTC('|'); OUTC('\n');
        }
        for(int c=0;c<t->ncols;c++){ OUTC('+'); for(int k=0;k<widths[c]+2;k++) OUTC('-'); } OUTC('+'); OUTC('\n');
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
        int idx=find_table(db,s->table); if(idx<0) return -1;
        Table *t=&db->tables[idx];
        int colidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,s->cols[0].name)==0) colidx=i;
        if(colidx<0) return -1;
        // similar scan
        u8 keys[256][8]; u8 rows[256][1024]; u16 rls[256]; int nk=0;
        u64 pn=t->root;
        while(pn){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n||n->is_leaf) break; if(n->nkeys==0) break; u8 *e=(u8*)n+n->offs[0]; u16 ek=*(u16*)e; pn=*(u64*)(e+2+ek); }
        while(pn&&nk<256){ BNode *n=(BNode*)pager_get(db->pager,pn); if(!n) break; for(int i=0;i<n->nkeys;i++){ u8 *e=(u8*)n+n->offs[i]; u16 kl=*(u16*)e; u16 vl=*(u16*)(e+2); u8 *k=e+4; u8 *v=e+4+kl; int ok=1; for(int w=0;w<s->nwhere;w++) if(!eval_where(t,v,&s->where[w])){ok=0;break;} if(ok){ rsc_memcpy(keys[nk],k,kl); rsc_memcpy(rows[nk],v,vl); rls[nk]=vl; nk++; } } pn=n->next_leaf; }
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
