# C API

Link against `out/<arch>/librsc.a` (static) or `out/<arch>/librsc.so`
(shared) and include `src/core/sql/sql.h`. Your program may be a normal
libc program — only the engine core itself avoids libc.

## Lifecycle

```c
#include "src/core/sql/sql.h"

Db db;
db_init(&db);              // empty in-memory database; never fails on FS
// Db fdb; db_open(&fdb, "file.rsc.db");  // file-backed alternative

db_close(&db);             // munmap + close; call once at the end
```

`db_init` never touches the filesystem. `db_open` mmaps an existing
file (or creates it). In both cases the database lives in mapped memory
until `DUMP` writes it out (see [SQL](sql.md) and
[file format](file-format.md)).

## Executing SQL

```c
char out[4096];
int rc = db_exec(&db, "INSERT INTO t VALUES (1, 'a');", out, sizeof(out));
// rc == 0  -> out is "OK\n" (or a SELECT table, or empty for CREATE)
// rc == -1 -> out is one line: ERR parse | ERR column 'x' does not exist
//             | ERR table 'x' does not exist | ERR duplicate primary key '1'
//             | ERR duplicate value 'v' for UNIQUE 'c'
//             | ERR column 'c' cannot be null | ...
```

`db_exec` handles every statement. For `SELECT` it formats the rows as
an ASCII table into `out` — the same text the [REPL](repl.md) prints.
`out` always fits in `out_cap` and is NUL-terminated.

## Structured results

For programmatic access use `db_query` (SELECT only):

```c
RscResult r; // { ncols, cols[16][32], nrows, cells[256][16][64] }
if (db_query(&db, "SELECT id, name FROM t WHERE id > 0 ORDER BY id;", &r) == 0) {
    for (int i = 0; i < r.ncols; i++) printf("%s ", r.cols[i]);
    printf("\n");
    for (int rr = 0; rr < r.nrows; rr++) {
        for (int c = 0; c < r.ncols; c++) printf("%s ", r.cells[rr][c]);
        printf("\n");
    }
}
```

`NULL` cells come back as the string `"NULL"`. `COUNT`/`AVG` return a
single cell (`"NULL"` for `AVG` over no non-null rows). At most 256 rows
are returned; add `LIMIT` to page through larger tables.

## Complete example

```c
#include "src/core/sql/sql.h"
#include <stdio.h>

int main(void) {
    Db db; db_init(&db);
    char out[4096];

    db_exec(&db, "CREATE TABLE t(id INT PRIMARY KEY, name TEXT);", out, sizeof(out));
    db_exec(&db, "INSERT INTO t VALUES (1, 'a');", out, sizeof(out));

    RscResult r;
    if (db_query(&db, "SELECT * FROM t;", &r) == 0)
        for (int i = 0; i < r.nrows; i++)
            printf("%s %s\n", r.cells[i][0], r.cells[i][1]);

    db_exec(&db, "DUMP ALL TO 'state.dump';", out, sizeof(out));
    db_close(&db);
    return 0;
}
```

Compile: `gcc -Isrc example.c out/x86_64/librsc.a -o example`.

## Errors and debug

Check the return value of every call. To get a human-readable
explanation of failures on stderr (query, table, columns, resolution,
error) instead of silent `-1`, enable debug once:

```c
rsc_enable_debug(1); // from sql.h; 0 disables again
```

Raw pager save/load (`pager_save(db.pager, path)` /
`pager_load(db.pager, path)`) exists but prefer the SQL
`DUMP ALL` / `LOAD ALL` statements.
