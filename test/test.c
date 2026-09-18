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
    db_exec(&d,"SELECT COUNT() FROM t",out,sizeof(out));
    ASSERT(strstr(out,"3")!=0,"bare count counts rows");
    db_exec(&d,"SELECT AVG(DISTINCT a) FROM t",out,sizeof(out));
    ASSERT(strstr(out,"1")!=0,"avg distinct");
    db_exec(&d,"SELECT SUM(a) FROM t",out,sizeof(out));
    ASSERT(strstr(out,"4")!=0,"sum skips null");
    db_exec(&d,"SELECT SUM(DISTINCT a) FROM t",out,sizeof(out));
    ASSERT(strstr(out,"3")!=0,"sum distinct");
    ASSERT(db_exec(&d,"SELECT SUM(a) FROM t WHERE a > 100",out,sizeof(out))==0,"sum empty set");
    ASSERT(strstr(out,"NULL")!=0,"sum empty is null");
    ASSERT(db_exec(&d,"SELECT SUM(b) FROM t",out,sizeof(out))!=0,"sum text rejected");
    ASSERT(db_exec(&d,"CREATE TABLE m (a INT, b TEXT)",out,sizeof(out))==0,"minmax create");
    ASSERT(db_exec(&d,"INSERT INTO m VALUES (3, 'pear')",out,sizeof(out))==0,"minmax i1");
    ASSERT(db_exec(&d,"INSERT INTO m VALUES (NULL, 'apple')",out,sizeof(out))==0,"minmax i2");
    ASSERT(db_exec(&d,"INSERT INTO m VALUES (-5, NULL)",out,sizeof(out))==0,"minmax i3");
    db_exec(&d,"SELECT MAX(a) FROM m",out,sizeof(out));
    ASSERT(strstr(out,"3")!=0,"max int skips null");
    db_exec(&d,"SELECT MIN(a) FROM m",out,sizeof(out));
    ASSERT(strstr(out,"-5")!=0,"min int negative");
    db_exec(&d,"SELECT MAX(b) FROM m",out,sizeof(out));
    ASSERT(strstr(out,"pear")!=0,"max text");
    db_exec(&d,"SELECT MIN(b) FROM m",out,sizeof(out));
    ASSERT(strstr(out,"apple")!=0,"min text skips null");
    db_exec(&d,"SELECT MAX(DISTINCT a) FROM m",out,sizeof(out));
    ASSERT(strstr(out,"3")!=0,"max distinct");
    ASSERT(db_exec(&d,"SELECT MAX(a) FROM t WHERE a > 100",out,sizeof(out))==0,"max empty set");
    ASSERT(strstr(out,"NULL")!=0,"max empty is null");
    ASSERT(db_exec(&d,"SELECT MIN(nope) FROM m",out,sizeof(out))!=0,"min bad column rejected");
    {
        RscResult rr2; rsc_memset(&rr2,0,sizeof(rr2));
        ASSERT(db_query(&d,"SELECT MIN(a) FROM m",&rr2)==0,"min query");
        ASSERT(rr2.nrows==1 && strcmp(rr2.cells[0][0],"-5")==0,"min query value");
    }
    {
        RscResult rr3; rsc_memset(&rr3,0,sizeof(rr3));
        ASSERT(db_query(&d,"SELECT MAX(b) FROM m",&rr3)==0,"max text query");
        ASSERT(rr3.nrows==1 && strcmp(rr3.cells[0][0],"pear")==0,"max text query value");
    }
    {
        RscResult rr4; rsc_memset(&rr4,0,sizeof(rr4));
        ASSERT(db_query(&d,"SELECT SUM(a) FROM m",&rr4)==0,"sum query");
        ASSERT(rr4.nrows==1 && strcmp(rr4.cells[0][0],"-2")==0,"sum query value");
    }
    {
        RscResult rr; rsc_memset(&rr,0,sizeof(rr));
        ASSERT(db_query(&d,"SELECT DISTINCT b FROM t",&rr)==0,"distinct query");
        ASSERT(rr.nrows==2,"distinct query rows");
    }
    db_close(&d);
    // regression: freelist exhausted by 3 CREATEs (4-page mem db),
    // first INSERT must survive pager grow/remap
    Db g; db_init(&g);
    ASSERT(db_exec(&g,"CREATE TABLE g1(id INT PRIMARY KEY, name TEXT)",out,sizeof(out))==0,"grow create 1");
    ASSERT(db_exec(&g,"CREATE TABLE g2(id INT PRIMARY KEY, name TEXT)",out,sizeof(out))==0,"grow create 2");
    ASSERT(db_exec(&g,"CREATE TABLE g3(id INT PRIMARY KEY, name TEXT)",out,sizeof(out))==0,"grow create 3");
    ASSERT(db_exec(&g,"INSERT INTO g1 VALUES (1, 'a')",out,sizeof(out))==0,"grow insert after 3 creates");
    db_exec(&g,"SELECT * FROM g1 WHERE id = 1",out,sizeof(out));
    ASSERT(strstr(out,"a")!=0,"grow insert visible");
    ASSERT(db_exec(&g,"INSERT INTO g2 VALUES (2, 'b')",out,sizeof(out))==0,"grow insert t2");
    ASSERT(db_exec(&g,"INSERT INTO g3 VALUES (3, 'c')",out,sizeof(out))==0,"grow insert t3");
    db_close(&g);
    Db vv; db_init(&vv);
    ASSERT(db_exec(&vv,"CREATE TABLE docs(id INT PRIMARY KEY, v VECTOR[3])",out,sizeof(out))==0,"vec create");
    ASSERT(db_exec(&vv,"INSERT INTO docs VALUES (1, [1.0,0.0,0.0])",out,sizeof(out))==0,"vec i1");
    ASSERT(db_exec(&vv,"INSERT INTO docs VALUES (2, [0.0,1.0,0.0])",out,sizeof(out))==0,"vec i2");
    ASSERT(db_exec(&vv,"INSERT INTO docs VALUES (3, [0.7,0.7,0.0])",out,sizeof(out))==0,"vec i3");
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0.1)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0 && strstr(out,"| 2 ")==0 && strstr(out,"| 3 ")==0,"vec cossim narrow");
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0 && strstr(out,"| 3 ")==0,"vec exact match thresh 0");
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0.5)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0 && strstr(out,"| 3 ")!=0 && strstr(out,"| 2 ")==0,"vec cossim wide");
    ASSERT(db_exec(&vv,"INSERT INTO docs VALUES (4, [1.0,0.0])",out,sizeof(out))!=0,"vec dims mismatch");
    ASSERT(db_exec(&vv,"INSERT INTO docs VALUES (4, [x,y,z])",out,sizeof(out))!=0,"vec bad float");
    ASSERT(db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0], 0.1)",out,sizeof(out))!=0,"vec query dims");
    ASSERT(db_exec(&vv,"SELECT * FROM docs WHERE v = 5",out,sizeof(out))!=0,"vec no compare");
    ASSERT(db_exec(&vv,"CREATE TABLE uq(id INT, v VECTOR[2] UNIQUE)",out,sizeof(out))==0,"vec unique create");
    ASSERT(db_exec(&vv,"INSERT INTO uq VALUES (1, [1.0,2.0])",out,sizeof(out))==0,"vec unique ok");
    ASSERT(db_exec(&vv,"INSERT INTO uq VALUES (2, [1.0,2.0])",out,sizeof(out))!=0,"vec unique dup");
    ASSERT(db_exec(&vv,"UPDATE docs SET v = [0.0,0.0,1.0] WHERE id = 2",out,sizeof(out))==0,"vec update");
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [0.0,0.0,1.0], 0.01)",out,sizeof(out));
    ASSERT(strstr(out,"| 2 ")!=0,"vec updated visible");
    {
        RscResult rr; rsc_memset(&rr,0,sizeof(rr));
        ASSERT(db_query(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0.5)",&rr)==0,"vec db_query");
        ASSERT(rr.nrows==2,"vec db_query rows");
    }
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0.5, 1)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0 && strstr(out,"| 3 ")==0,"vec top-k 1");
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0.5, 5)",out,sizeof(out));
    { char *p1=strstr(out,"| 1 "), *p3=strstr(out,"| 3 ");
      ASSERT(p1&&p3&&p1<p3,"vec top-k order"); }
    ASSERT(db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [1.0,0.0,0.0], 0.5, 0)",out,sizeof(out))!=0,"vec top-k 0 rejected");
    ASSERT(db_exec(&vv,"INSERT INTO docs VALUES (9, [100000000000000000000.0,100000000000000000000.0,0.0])",out,sizeof(out))==0,"vec large i");
    db_exec(&vv,"SELECT * FROM docs WHERE cossim(v, [100000000000000000000.0,100000000000000000000.0,0.0], 0)",out,sizeof(out));
    ASSERT(strstr(out,"| 9 ")!=0,"vec large self-match thresh 0");
    db_close(&vv);
    Db qq; db_init(&qq);
    ASSERT(db_exec(&qq,"CREATE TABLE q32(id INT PRIMARY KEY, v VECTOR[3])",out,sizeof(out))==0,"quant fp32 default");
    ASSERT(db_exec(&qq,"CREATE TABLE q16(id INT PRIMARY KEY, v VECTOR[3] AS FP16)",out,sizeof(out))==0,"quant fp16");
    ASSERT(db_exec(&qq,"CREATE TABLE q8(id INT PRIMARY KEY, v VECTOR[3] AS Q8)",out,sizeof(out))==0,"quant q8");
    ASSERT(db_exec(&qq,"CREATE TABLE q4(id INT PRIMARY KEY, v VECTOR[3] AS Q4)",out,sizeof(out))==0,"quant q4");
    ASSERT(db_exec(&qq,"CREATE TABLE q2(id INT PRIMARY KEY, v VECTOR[3] AS Q2)",out,sizeof(out))==0,"quant q2");
    ASSERT(db_exec(&qq,"CREATE TABLE q1(id INT PRIMARY KEY, v VECTOR[3] AS Q1)",out,sizeof(out))==0,"quant q1");
    ASSERT(db_exec(&qq,"CREATE TABLE qb(id INT PRIMARY KEY, v VECTOR[3] AS FP8)",out,sizeof(out))!=0,"quant bad type rejected");
    ASSERT(db_exec(&qq,"CREATE TABLE qd(id INT PRIMARY KEY, v VECTOR[0] AS Q8)",out,sizeof(out))!=0,"quant bad dims rejected");
    ASSERT(db_exec(&qq,"INSERT INTO q16 VALUES (1, [1.0,0.0,0.0])",out,sizeof(out))==0,"quant fp16 insert");
    ASSERT(db_exec(&qq,"INSERT INTO q8 VALUES (1, [1.0,0.0,0.0])",out,sizeof(out))==0,"quant q8 insert");
    ASSERT(db_exec(&qq,"INSERT INTO q4 VALUES (1, [1.0,0.0,0.0])",out,sizeof(out))==0,"quant q4 insert");
    ASSERT(db_exec(&qq,"INSERT INTO q2 VALUES (1, [1.0,0.0,0.0])",out,sizeof(out))==0,"quant q2 insert");
    ASSERT(db_exec(&qq,"INSERT INTO q1 VALUES (1, [1.0,0.0,0.0])",out,sizeof(out))==0,"quant q1 insert");
    db_exec(&qq,"SELECT * FROM q16 WHERE cossim(v, [1.0,0.0,0.0], 0)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0,"quant fp16 exact");
    db_exec(&qq,"SELECT * FROM q8 WHERE cossim(v, [1.0,0.0,0.0], 0)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0,"quant q8 exact");
    db_exec(&qq,"SELECT * FROM q4 WHERE cossim(v, [1.0,0.0,0.0], 0)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0,"quant q4 exact");
    db_exec(&qq,"SELECT * FROM q2 WHERE cossim(v, [1.0,0.0,0.0], 0.2)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0,"quant q2 near");
    db_exec(&qq,"SELECT * FROM q2 WHERE cossim(v, [1.0,0.0,0.0], 0.01)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")==0,"quant q2 narrow excludes");
    db_exec(&qq,"SELECT * FROM q1 WHERE cossim(v, [1.0,0.0,0.0], 0.5)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0,"quant q1 near");
    db_exec(&qq,"SELECT * FROM q1 WHERE cossim(v, [1.0,0.0,0.0], 0.1)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")==0,"quant q1 narrow excludes");
    ASSERT(db_exec(&qq,"CREATE TABLE qu(id INT PRIMARY KEY, v VECTOR[3] AS Q8 UNIQUE)",out,sizeof(out))==0,"quant unique create");
    ASSERT(db_exec(&qq,"INSERT INTO qu VALUES (1, [1.0,2.0,3.0])",out,sizeof(out))==0,"quant unique ok");
    ASSERT(db_exec(&qq,"INSERT INTO qu VALUES (2, [1.0,2.0,3.0])",out,sizeof(out))!=0,"quant unique dup");
    ASSERT(db_exec(&qq,"UPDATE qu SET v = [4.0,5.0,6.0] WHERE id = 1",out,sizeof(out))==0,"quant update");
    db_exec(&qq,"SELECT * FROM qu WHERE cossim(v, [4.0,5.0,6.0], 0.001)",out,sizeof(out));
    ASSERT(strstr(out,"| 1 ")!=0,"quant updated visible");
    db_close(&qq);
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
