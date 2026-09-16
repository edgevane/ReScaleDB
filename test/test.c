#include "../src/core/sql/sql.h"
#include "../src/core/btree.h"
#include "../src/libs/string.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int fail=0;
#define ASSERT(cond,msg) do{ if(!(cond)){ printf("FAIL: %s\n",msg); fail++; } else { printf("PASS: %s\n",msg);} }while(0)
#define ASSERT_STR(a,b,msg) ASSERT(rsc_strcmp(a,b)==0||strcmp(a,b)==0,msg)
int main(){
    unlink("/tmp/test.rsc.db");
    Db db; db_open(&db,"/tmp/test.rsc.db");
    char out[4096];
    ASSERT(db_exec(&db,"CREATE TABLE t (id INT, name TEXT)",out,sizeof(out))==0,"create table");
    ASSERT(db_exec(&db,"INSERT INTO t VALUES (1, 'a')",out,sizeof(out))==0,"insert 1");
    ASSERT(db_exec(&db,"INSERT INTO t VALUES (2, 'b')",out,sizeof(out))==0,"insert 2");
    ASSERT(db_exec(&db,"CREATE INDEX idx ON t(id)",out,sizeof(out))==0,"create index");
    ASSERT(db_exec(&db,"SELECT * FROM t WHERE id = 1",out,sizeof(out))==0,"select where");
    ASSERT(strstr(out,"a")!=0,"select returns a");
    ASSERT(db_exec(&db,"SELECT * FROM t ORDER BY id LIMIT 1",out,sizeof(out))==0,"order+limit");
    printf("out: %s\n",out);
    ASSERT(db_exec(&db,"UPDATE t SET name = 'bb' WHERE id = 2",out,sizeof(out))==0,"update");
    db_exec(&db,"SELECT * FROM t WHERE id = 2",out,sizeof(out)); printf("after update: %s\n",out);
    ASSERT(strstr(out,"bb")!=0,"update reflected");
    db_close(&db);
    Db m; db_init(&m);
    ASSERT(db_exec(&m,"CREATE TABLE u (id INT PRIMARY KEY, email TEXT UNIQUE NOT NULL, nick TEXT DEFAULT 'anon', age INT DEFAULT 18, note TEXT)",out,sizeof(out))==0,"create constraints");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (1, 'a@x', 'ann', 20, 'hi')",out,sizeof(out))==0,"insert pk/unique");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (1, 'b@x', 'b', 1, NULL)",out,sizeof(out))!=0,"dup pk rejected");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (2, 'a@x', 'b', 1, NULL)",out,sizeof(out))!=0,"dup unique rejected");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (3, NULL, 'c', 1, NULL)",out,sizeof(out))!=0,"not null rejected");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (4, 'd@x', DEFAULT, DEFAULT, NULL)",out,sizeof(out))==0,"default applied");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (5, NULL, 'e', 1, NULL)",out,sizeof(out))!=0,"null unique notnull rejected");
    db_exec(&m,"SELECT * FROM u WHERE id = 4",out,sizeof(out));
    ASSERT(strstr(out,"anon")!=0 && strstr(out,"18")!=0,"default values stored");
    ASSERT(db_exec(&m,"INSERT INTO u VALUES (6, 'f@x', NULL, 2, NULL)",out,sizeof(out))==0,"null allowed");
    db_exec(&m,"SELECT * FROM u WHERE nick IS NULL",out,sizeof(out));
    ASSERT(strstr(out,"f@x")!=0,"is null works");
    db_exec(&m,"SELECT COUNT(nick) FROM u",out,sizeof(out));
    ASSERT(strstr(out,"2")!=0,"count skips nulls");
    ASSERT(db_exec(&m,"UPDATE u SET email = NULL WHERE id = 1",out,sizeof(out))!=0,"update to null rejected");
    db_close(&m);
    Db d; db_init(&d);
    ASSERT(db_exec(&d,"CREATE TABLE t (a INT, b TEXT)",out,sizeof(out))==0,"distinct create");
    ASSERT(db_exec(&d,"INSERT INTO t VALUES (1, 'x')",out,sizeof(out))==0,"distinct i1");
    ASSERT(db_exec(&d,"INSERT INTO t VALUES (1, 'x')",out,sizeof(out))==0,"distinct i2");
    ASSERT(db_exec(&d,"INSERT INTO t VALUES (2, NULL)",out,sizeof(out))==0,"distinct i3");
    db_exec(&d,"SELECT DISTINCT a FROM t",out,sizeof(out));
    ASSERT(strstr(out,"1")!=0 && strstr(out,"2")!=0,"distinct rows");
    db_exec(&d,"SELECT DISTINCT b FROM t",out,sizeof(out));
    ASSERT(strstr(out,"x")!=0 && strstr(out,"NULL")!=0,"distinct null shown");
    db_exec(&d,"SELECT COUNT(DISTINCT a) FROM t",out,sizeof(out));
    ASSERT(strstr(out,"2")!=0,"count distinct skips null");
    db_exec(&d,"SELECT AVG(DISTINCT a) FROM t",out,sizeof(out));
    ASSERT(strstr(out,"1")!=0,"avg distinct");
    {
        RscResult rr; rsc_memset(&rr,0,sizeof(rr));
        ASSERT(db_query(&d,"SELECT DISTINCT b FROM t",&rr)==0,"distinct query");
        ASSERT(rr.nrows==2,"distinct query rows");
    }
    db_close(&d);
    // btree direct isolated
    Pager p2; pager_open(&p2,"/tmp/bt_test.rsc.db");
    u64 root=0; btree_create(&p2,&root);
    ASSERT(btree_insert(&p2,&root,"k1",2,"v1",2)==0,"btree insert");
    char vb[16]; u16 vl; ASSERT(btree_search(&p2,root,"k1",2,vb,&vl)==0,"btree search"); ASSERT(vl==2&&memcmp(vb,"v1",2)==0,"btree value");
    for(int i=0;i<100;i++){ char k[8],v[8]; k[0]='k';k[1]='0'+i%10;k[2]=0; v[0]='v';v[1]='0'+i%10;v[2]=0; btree_insert(&p2,&root,k,rsc_strlen(k),v,rsc_strlen(v)); }
    ASSERT(btree_search(&p2,root,"k1",2,vb,&vl)==0,"btree after split");
    pager_close(&p2); unlink("/tmp/bt_test.rsc.db");
    printf("\n%d failures\n",fail);
    return fail?1:0;
}
