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
