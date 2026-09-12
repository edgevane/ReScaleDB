#include "../src/libs/string.h"
#include "../src/libs/types.h"
#include "../src/libs/ctype.h"
#include "../src/arch/arch.h"
#include "../src/core/sql/sql.h"
static void put(const char *s){ arch_write(1,s,rsc_strlen(s)); }
static int read_line(char *buf, usize cap){
    usize off=0;
    while(off+1<cap){
        char c; i64 r=arch_read(0,&c,1);
        if(r<=0) return off? (int)off : -1;
        if(c=='\n') break;
        buf[off++]=c;
    }
    buf[off]=0; return (int)off;
}
static void print_help(){
    put("  .save [file]           - shortcut for DUMP ALL TO 'file' (default: state.rsc.dump)\n");
    put("  .load [file]           - shortcut for LOAD ALL FROM 'file' (default: state.rsc.dump)\n");
    put("  .help                  - show this help\n");
    put("  .exit                  - quit\n");
}
int main(){
    Db db;
    char cur_path[128]={0}; rsc_strcpy(cur_path,"/tmp/repl.rsc.db");
    if(db_init(&db)!=0){ put("open fail\n"); return 1; }
    put("ReScaleDB repl - type .help for help\n");
    char line[1024]; char out[4096];
    while(1){
        put("rsc> ");
        int n=read_line(line,sizeof(line));
        if(n<0) break;
        if(rsc_strcmp(line,".exit")==0||rsc_strcmp(line,".quit")==0) break;
        if(rsc_strcmp(line,".help")==0||rsc_strcmp(line,"help")==0){ print_help(); continue; }
        if(rsc_strlen(line)==0) continue;
        {
            char low[1024]; for(usize i=0;i<rsc_strlen(line)+1;i++) low[i]=rsc_tolower(line[i]);
            if(rsc_strncmp(low,".save",5)==0){
                char f[128]={0};
                const char *p=line+5; while(*p==' ') p++;
                int i=0; while(*p&&*p!=';'&&i<127) f[i++]=*p++; f[i]=0; while(i>0&&f[i-1]==' ') f[--i]=0;
                if(f[0]==0) rsc_strcpy(f,"state.rsc.dump");
                char sql[160]={0}; rsc_strcpy(sql,"DUMP ALL TO '"); rsc_strcpy(sql+rsc_strlen(sql),f); rsc_strcpy(sql+rsc_strlen(sql),"';");
                rsc_memset(out,0,sizeof(out));
                int rc=db_exec(&db,sql,out,sizeof(out));
                if(rc==0) put(out[0]?out:"OK saved\n"); else put(out[0]?out:"ERR save\n");
                continue;
            }
            if(rsc_strncmp(low,".load",5)==0){
                char f[128]={0};
                const char *p=line+5; while(*p==' ') p++;
                int i=0; while(*p&&*p!=';'&&i<127) f[i++]=*p++; f[i]=0; while(i>0&&f[i-1]==' ') f[--i]=0;
                if(f[0]==0) rsc_strcpy(f,"state.rsc.dump");
                char sql[160]={0}; rsc_strcpy(sql,"LOAD ALL FROM '"); rsc_strcpy(sql+rsc_strlen(sql),f); rsc_strcpy(sql+rsc_strlen(sql),"';");
                rsc_memset(out,0,sizeof(out));
                int rc=db_exec(&db,sql,out,sizeof(out));
                if(rc==0) put(out[0]?out:"OK loaded\n"); else put(out[0]?out:"ERR load\n");
                continue;
            }
        }
        if(rsc_strncmp(line,".open ",6)==0){
            db_close(&db);
            const char *p=line+6; while(*p==' ') p++;
            rsc_strcpy(cur_path,p);
            if(db_open(&db,p)!=0) put("open fail\n");
            continue;
        }
        rsc_memset(out,0,sizeof(out));
        int rc=db_exec(&db,line,out,sizeof(out));
        if(rc==0){ if(out[0]) put(out); else put("OK\n"); }
        else { put(out[0]?out:"ERR\n"); }
    }
    db_close(&db);
    put("bye\n");
    return 0;
}
