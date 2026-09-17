#include "sql.h"
#include "../btree.h"
#include "../dump.h"
#include "../debug.h"
#include "../../libs/string.h"
#include "../../libs/ctype.h"
#include "../../arch/arch.h"
#include "exec_int.h"
int exec_select(Db *db, Stmt *s, char *out, usize cap){
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
        if(s->is_sum){
            if(find_col(t,s->agg_col)<0){ rsc_debug_unknown_column(db,s,s->agg_col,"sum"); set_col_error(out,cap,s->agg_col); return -1; }
        }
        if(s->is_max || s->is_min){
            if(find_col(t,s->agg_col)<0){ rsc_debug_unknown_column(db,s,s->agg_col,s->is_max?"max":"min"); set_col_error(out,cap,s->agg_col); return -1; }
        }
        if(s->is_count && s->agg_col[0] && rsc_strcmp(s->agg_col,"*")!=0){
            if(find_col(t,s->agg_col)<0){ rsc_debug_unknown_column(db,s,s->agg_col,"count"); set_col_error(out,cap,s->agg_col); return -1; }
        }
        int proj_idx[16]; int proj_n=0;
        int use_all = s->is_star || (s->nselect==0 && !s->is_count && !s->is_avg && !s->is_sum && !s->is_max && !s->is_min);
        if(use_all){ for(int c=0;c<t->ncols;c++) proj_idx[proj_n++]=c; }
        else if(!s->is_count && !s->is_avg && !s->is_sum && !s->is_max && !s->is_min){ for(int i=0;i<s->nselect;i++) proj_idx[proj_n++]=find_col(t,s->select_cols[i]); }
        ScanCtx ctx; rsc_memset(&ctx,0,sizeof(ctx)); ctx.db=db; ctx.t=t; ctx.st=s; ctx.out=out; ctx.cap=cap; ctx.off=0;
        btree_scan(db->pager,t->root,scan_cb,&ctx);
        // simple order by (bubble, NULLs last)
        if(s->has_order){
            int oidx=-1; for(int i=0;i<t->ncols;i++) if(rsc_strcmp(t->cols[i].name,s->order_by)==0) oidx=i;
            if(oidx>=0){
                for(int i=0;i<ctx.nrows;i++) for(int j=i+1;j<ctx.nrows;j++){
                    int na=row_is_null(t,ctx.rows[i],oidx);
                    int nb=row_is_null(t,ctx.rows[j],oidx);
                    int cmp=0;
                    if(na && nb) cmp=0;
                    else if(na) cmp=1;
                    else if(nb) cmp=-1;
                    else {
                        char ca[64]={0}, cb[64]={0};
                        row_cell_str(t,ctx.rows[i],oidx,ca,0);
                        row_cell_str(t,ctx.rows[j],oidx,cb,0);
                        if(t->cols[oidx].type==COL_INT){
                            long va=parse_int_val(ca), vb=parse_int_val(cb);
                            cmp=(va<vb?-1:(va>vb?1:0));
                        } else {
                            cmp=rsc_strcmp(ca,cb);
                            if(cmp<0) cmp=-1; else if(cmp>0) cmp=1;
                        }
                    }
                    if(cmp>0){ u8 *tmp=ctx.rows[i]; ctx.rows[i]=ctx.rows[j]; ctx.rows[j]=tmp; u16 tl=ctx.lens[i]; ctx.lens[i]=ctx.lens[j]; ctx.lens[j]=tl; }
                }
            }
        }
        if(s->is_count || s->is_avg || s->is_sum || s->is_max || s->is_min){
            int cnt=ctx.nrows;
            usize off=0;
            #define OUTC2(c) do{ if(off+1<cap) out[off++]=c; }while(0)
            #define OUTS2(s) do{ usize _l=rsc_strlen(s); if(off+_l<cap){ rsc_memcpy(out+off,s,_l); off+=_l; } }while(0)
            if(s->is_count){
                int v=cnt;
                if(s->agg_col[0] && rsc_strcmp(s->agg_col,"*")!=0){
                    int cc=find_col(t,s->agg_col);
                    if(s->is_distinct){
                        char seen[256][64]; int nseen=0;
                        for(int r=0;r<cnt;r++){
                            if(row_is_null(t,ctx.rows[r],cc)) continue;
                            char cb[64]={0}; row_cell_str(t,ctx.rows[r],cc,cb,0);
                            int dup=0;
                            for(int k=0;k<nseen;k++) if(rsc_strcmp(seen[k],cb)==0){ dup=1; break; }
                            if(!dup && nseen<256){ rsc_strcpy(seen[nseen++],cb); }
                        }
                        v=nseen;
                    } else {
                        v=0;
                        for(int r=0;r<cnt;r++) if(!row_is_null(t,ctx.rows[r],cc)) v++;
                    }
                } else if(s->is_distinct){
                    // COUNT(DISTINCT *) -> distinct rows
                    char seen[256][16][64]; int seen_null[256][16]; int nseen=0;
                    for(int r=0;r<cnt;r++){
                        char tv[16][64]; int tn[16]={0};
                        decode_row_all(t,ctx.rows[r],tv,tn);
                        int dup=0;
                        for(int k=0;k<nseen;k++){
                            int eq=1;
                            for(int c=0;c<t->ncols;c++){
                                if(tn[c]!=seen_null[k][c]){ eq=0; break; }
                                if(!tn[c] && rsc_strcmp(tv[c],seen[k][c])!=0){ eq=0; break; }
                            }
                            if(eq){ dup=1; break; }
                        }
                        if(!dup && nseen<256){
                            for(int c=0;c<t->ncols;c++){ rsc_strcpy(seen[nseen][c],tv[c]); seen_null[nseen][c]=tn[c]; }
                            nseen++;
                        }
                    }
                    v=nseen;
                }
                char hdr2[64]="COUNT"; char val[32]; int pos=0; char rev[16]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
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
                i64 sum=0; int n=0;
                char seen[256][64]; int nseen=0;
                for(int r=0;r<cnt;r++){
                    if(row_is_null(t,ctx.rows[r],cidx)) continue;
                    char cb[64]={0}; row_cell_str(t,ctx.rows[r],cidx,cb,0);
                    if(s->is_distinct){
                        int dup=0;
                        for(int k=0;k<nseen;k++) if(rsc_strcmp(seen[k],cb)==0){ dup=1; break; }
                        if(dup) continue;
                        if(nseen<256) rsc_strcpy(seen[nseen++],cb);
                    }
                    sum+=parse_int_val(cb); n++;
                }
                char hdr2[64]; rsc_strcpy(hdr2,"AVG("); rsc_strcpy(hdr2+4,s->agg_col); rsc_strcpy(hdr2+4+rsc_strlen(s->agg_col),")");
                char val[32];
                if(n==0){ rsc_strcpy(val,"NULL"); }
                else {
                    i64 avg=sum/n;
                    int pos=0; int neg=0; i64 v=avg; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
                }
                int w=(int)rsc_strlen(hdr2); int wl=(int)rsc_strlen(val); if(wl>w) w=wl;
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(hdr2); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(val); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
            }
            if(s->is_sum){
                int cidx=-1; for(int c=0;c<t->ncols;c++) if(rsc_strcmp(t->cols[c].name,s->agg_col)==0) cidx=c;
                if(cidx<0) return -1;
                if(t->cols[cidx].type!=COL_INT) return -1;
                i64 sum=0; int n=0;
                char seen[256][64]; int nseen=0;
                for(int r=0;r<cnt;r++){
                    if(row_is_null(t,ctx.rows[r],cidx)) continue;
                    char cb[64]={0}; row_cell_str(t,ctx.rows[r],cidx,cb,0);
                    if(s->is_distinct){
                        int dup=0;
                        for(int k=0;k<nseen;k++) if(rsc_strcmp(seen[k],cb)==0){ dup=1; break; }
                        if(dup) continue;
                        if(nseen<256) rsc_strcpy(seen[nseen++],cb);
                    }
                    sum+=parse_int_val(cb); n++;
                }
                char hdr2[64]; rsc_strcpy(hdr2,"SUM("); rsc_strcpy(hdr2+4,s->agg_col); rsc_strcpy(hdr2+4+rsc_strlen(s->agg_col),")");
                char val[32];
                if(n==0){ rsc_strcpy(val,"NULL"); }
                else {
                    int pos=0; int neg=0; i64 v=sum; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
                }
                int w=(int)rsc_strlen(hdr2); int wl=(int)rsc_strlen(val); if(wl>w) w=wl;
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(hdr2); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(val); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
            }
            if(s->is_max || s->is_min){
                int cidx=-1; for(int c=0;c<t->ncols;c++) if(rsc_strcmp(t->cols[c].name,s->agg_col)==0) cidx=c;
                if(cidx<0) return -1;
                // DISTINCT is accepted but cannot change an extremum, so it is ignored.
                int is_int=(t->cols[cidx].type==COL_INT);
                i64 best_n=0; char best_s[64]={0}; int n=0;
                for(int r=0;r<cnt;r++){
                    if(row_is_null(t,ctx.rows[r],cidx)) continue;
                    char cb[64]={0}; row_cell_str(t,ctx.rows[r],cidx,cb,0);
                    if(n==0){
                        if(is_int) best_n=parse_int_val(cb); else rsc_strcpy(best_s,cb);
                        n=1; continue;
                    }
                    if(is_int){
                        i64 v=parse_int_val(cb);
                        if(s->is_max ? v>best_n : v<best_n) best_n=v;
                    } else {
                        int cmp=rsc_strcmp(cb,best_s);
                        if(s->is_max ? cmp>0 : cmp<0) rsc_strcpy(best_s,cb);
                    }
                }
                char hdr2[64]; rsc_strcpy(hdr2,s->is_max?"MAX(":"MIN("); rsc_strcpy(hdr2+4,s->agg_col); rsc_strcpy(hdr2+4+rsc_strlen(s->agg_col),")");
                char val[64];
                if(n==0){ rsc_strcpy(val,"NULL"); }
                else if(!is_int){ rsc_strcpy(val,best_s); }
                else {
                    int pos=0; int neg=0; i64 v=best_n; if(v<0){neg=1; v=-v;} char rev[32]; int rp=0; if(v==0) rev[rp++]='0'; while(v>0){rev[rp++]='0'+(v%10); v/=10;} if(neg) rev[rp++]='-'; for(int k=rp-1;k>=0;k--) val[pos++]=rev[k]; val[pos]=0;
                }
                int w=(int)rsc_strlen(hdr2); int wl=(int)rsc_strlen(val); if(wl>w) w=wl;
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(hdr2); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                OUTS2(val); OUTC2('\n');
                for(int k=0;k<w+2;k++) OUTC2('-'); OUTC2('-'); OUTC2('\n');
                if(off<cap) out[off]=0; else out[cap-1]=0; return 0;
            }
        }
        int nrows_all=ctx.nrows;
        if(nrows_all>256) nrows_all=256;
        int widths[SQL_MAX_COLS]={0};
        for(int pi=0;pi<proj_n;pi++){ int c=proj_idx[pi]; widths[pi]=(int)rsc_strlen(t->cols[c].name); }
        static char cells[256][16][64];
        for(int r=0;r<nrows_all;r++){
            u8 *row=ctx.rows[r];
            char tmpvals[16][64]; int tmpnull[16]={0};
            decode_row_all(t,row,tmpvals,tmpnull);
            for(int pi=0;pi<proj_n;pi++){
                int c=proj_idx[pi];
                if(tmpnull[c]) rsc_strcpy(cells[r][pi], "NULL");
                else rsc_strcpy(cells[r][pi], tmpvals[c]);
                int cl=(int)rsc_strlen(cells[r][pi]);
                if(cl>widths[pi]) widths[pi]=cl;
            }
        }
        int nout=nrows_all;
        if(s->is_distinct && !s->is_count && !s->is_avg && !s->is_sum && !s->is_max && !s->is_min){
            int w=0;
            for(int r=0;r<nout;r++){
                int dup=0;
                for(int k=0;k<w;k++){
                    int eq=1;
                    for(int pi=0;pi<proj_n;pi++) if(rsc_strcmp(cells[r][pi],cells[k][pi])!=0){ eq=0; break; }
                    if(eq){ dup=1; break; }
                }
                if(!dup){
                    if(w!=r) for(int pi=0;pi<proj_n;pi++) rsc_strcpy(cells[w][pi],cells[r][pi]);
                    w++;
                }
            }
            nout=w;
        }
        int lim = s->has_limit? s->limit : nout;
        if(lim>nout) lim=nout;
        usize off=0;
        #define OUTC(c) do{ if(off+1<cap) out[off++]=c; }while(0)
        #define OUTS(s) do{ usize _l=rsc_strlen(s); if(off+_l<cap){ rsc_memcpy(out+off,s,_l); off+=_l; } }while(0)
        #define OUTN(s,n) do{ if(off+(n)<cap){ rsc_memcpy(out+off,s,n); off+=n; } }while(0)
        if(lim==0 && nout==0){
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
