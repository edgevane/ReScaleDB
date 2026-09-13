# ReScaleDB

Fast embedded SQL database for Linux. Core is freestanding (no libc), mmap-backed, B+Tree storage. In-memory by default, explicit persistence via `DUMP`/`LOAD`.

## Features

- In-memory with `mmap(ANON)` + B+Tree (order 64, 4KB pages, copy-on-write)
- MVCC single-writer / multi-reader
- SQL: `CREATE/DROP DATABASE`, `USE`, `CREATE TABLE` with `PRIMARY KEY`, `CREATE INDEX`, `INSERT`, `SELECT` with `WHERE` (`= </> <= >=`), `AND`, `ORDER BY`, `LIMIT`, `UPDATE`, `DELETE`
- `PRIMARY KEY` (single `INT` or `TEXT` column): uniqueness enforced on `INSERT`/`UPDATE`, persists in catalog (header v2)
- Aggregates: `COUNT(*)`, `COUNT(col)`, `AVG(col)` (INT)
- REPL shows tables, code gets structured results
- Non-libc core (`src/libs` + `src/arch`), `arch/` only place with syscalls
- Linux x86_64 and aarch64, GCC freestanding (`-nostdlib -ffreestanding`)
- Rust bindings (`bindings/rust`)

## Architecture

```
src/libs/        freestanding libc replacements (string, mem, ctype, types)
src/arch/linux/  syscall wrappers (mmap, msync, open, read, write, unlink, getdents)
                 x86_64 (syscall) and aarch64 (svc) supported
src/core/
  pager.*        page management (4KB, freelist, anon or file-backed)
  btree.*        B+Tree, linked leaves for range scan
  txn.*          MVCC
  dump.*         save/load pager image
  sql/
    parser.c     lexer/parser for medium SQL subset
    executor.c   planner + executor, table output and structured results
    db.c         catalog persistence in header, db_init/db_open/db_exec/db_query
repl/            interactive shell, only arch+libs, no stdio
bindings/rust/   safe wrapper
```

No files touch FS until `DUMP ALL` / `.save`. One dump file holds entire in-memory state.

## Build

```sh
make              # native x86_64 -> out/x86_64/librsc.a, librsc.so, repl
make ARCH=aarch64 # cross ARM64
make rust         # cargo build Rust bindings
make rust-tests   # cargo test
make tests        # ./test_runner
```

Outputs in `out/<arch>/`:
- `librsc.a` / `librsc.so`
- `repl`
- `include/` (headers)

Requires GCC, Linux, Rust (for bindings).

## SQL Reference

```sql
CREATE DATABASE app;
USE app;
DROP DATABASE app;

CREATE TABLE users(id INT PRIMARY KEY, name TEXT);
-- or: CREATE TABLE users(id INT, name TEXT, PRIMARY KEY (id));
CREATE INDEX idx ON users(id);

INSERT INTO users VALUES (1, 'alice');
-- INSERT with duplicate PK fails: ERR duplicate primary key '1'

SELECT * FROM users;
SELECT * FROM users WHERE id > 1 AND name = 'bob' ORDER BY id LIMIT 10;
SELECT COUNT(*) FROM users;
SELECT COUNT(*) FROM users WHERE val > 10;
SELECT AVG(val) FROM t;

UPDATE users SET name = 'bob' WHERE id = 1;
DELETE FROM users WHERE id = 1;

DUMP ALL TO 'state.dump';
LOAD ALL FROM 'state.dump';
```

`SELECT` in REPL prints as table:

```
+----+-------+
| id | name  |
+----+-------+
| 1  | alice |
| 2  | bob   |
+----+-------+
```

In code, `SELECT` returns structured data (see C/Rust API).

## Primary key

```sql
CREATE TABLE users(id INT PRIMARY KEY, name TEXT);
CREATE TABLE keys(name TEXT PRIMARY KEY, v INT);
CREATE TABLE t(id INT, name TEXT, PRIMARY KEY (id));
```

Rules:

- one `PRIMARY KEY` per table, `INT` or `TEXT`
- `INSERT` with existing PK value fails: `ERR duplicate primary key '1'`
- `UPDATE` of the PK column to an existing value fails the same way
- PK survives `DUMP ALL` / `LOAD ALL` (catalog header v2, old v1 files load with no PK)
- `rsc_enable_debug(1)` prints PK info on duplicate:

```
[ReScaleDB DEBUG]

Query:
    INSERT INTO users VALUES (1, 'b');

Table: users

Available columns:
    0: id  INT  PRIMARY KEY
    1: name  TEXT

Primary key:
    id (must be unique)

Error:
    duplicate primary key '1'
```

## REPL

```sh
./out/x86_64/repl
```

```
rsc> CREATE DATABASE app;
OK
rsc> SELECT * FROM users;
+----+-------+
| id | name  |
+----+-------+
| 1  | alice |
+----+-------+
rsc> .help
  .save [file]           - shortcut for DUMP ALL TO 'file' (default: state.rsc.dump)
  .load [file]           - shortcut for LOAD ALL FROM 'file' (default: state.rsc.dump)
  .help                  - show this help
  .exit                  - quit
```

Dot commands are shortcuts for SQL `DUMP`/`LOAD`.

## C API

Two result modes: string table (REPL-friendly) and structured (code-friendly).

```c
#include "src/core/sql/sql.h"

Db db;
db_init(&db); // in-memory, no file

char out[4096];
db_exec(&db, "CREATE TABLE t(id INT, name TEXT);", out, sizeof(out));
db_exec(&db, "INSERT INTO t VALUES (1, 'a');", out, sizeof(out));

// string table (REPL style)
db_exec(&db, "SELECT * FROM t;", out, sizeof(out));
printf("%s\n", out);

// structured (code-friendly)
RscResult res;
db_query(&db, "SELECT * FROM t WHERE id > 0 ORDER BY id;", &res);
for (int i = 0; i < res.ncols; i++) printf("%s ", res.cols[i]);
for (int r = 0; r < res.nrows; r++) {
    for (int c = 0; c < res.ncols; c++) printf("%s ", res.cells[r][c]);
}

db_query(&db, "SELECT COUNT(*) FROM t;", &res);
printf("count %s\n", res.cells[0][0]);

db_query(&db, "SELECT AVG(val) FROM t;", &res);
printf("avg %s\n", res.cells[0][0]);

db_exec(&db, "DUMP ALL TO 'state.dump';", out, sizeof(out));
db_exec(&db, "LOAD ALL FROM 'state.dump';", out, sizeof(out));

db_close(&db);

// file-backed alternative
Db fdb;
db_open(&fdb, "file.rsc.db");
```

Link with `out/<arch>/librsc.a` or `librsc.so`.

Low-level pager:

```c
pager_save(db.pager, "state.dump");
pager_load(db.pager, "state.dump");
```

## Rust API

```toml
[dependencies]
rescaledb = { path = "../bindings/rust" }
```

```rust
use rescaledb::Database;

let mut db = Database::new()?; // in-memory
// let mut db = Database::open("file.rsc.db")?; // file-backed

db.exec("CREATE TABLE t(id INT, name TEXT);")?;
db.exec("INSERT INTO t VALUES (1, 'a');")?;

// string table (REPL style)
let table = db.exec("SELECT * FROM t;")?;
println!("{}", table);

// structured (code-friendly)
let res = db.query("SELECT * FROM t WHERE id > 0;")?;
println!("cols {:?}", res.columns);
for row in res.rows {
    println!("{:?}", row);
}

let cnt = db.query("SELECT COUNT(*) FROM t;")?;
println!("count {}", cnt.rows[0][0]);

let avg = db.query("SELECT AVG(val) FROM t;")?;
println!("avg {}", avg.rows[0][0]);

db.exec("DUMP ALL TO 'state.dump';")?;
db.exec("LOAD ALL FROM 'state.dump';")?;
```

```sh
cargo run --example demo
cargo test
```

## File Format

- In-memory: `mmap(ANON)` 4KB pages, header at page 0 (magic `0x52534344`, version, page_count, root, freelist, txn_id, catalog at offset 64)
- Catalog: `ntables` + per-table `name, ncols, cols(name, type, is_pk), root, rowid_seq, pk_col` stored in header (v2)
- `DUMP`: raw `mmap` image (`map_len` bytes) saved to file, single file for all DBs
- `LOAD`: file read back into anon `mmap`, catalog reloaded

## Testing

```sh
make tests        # C tests
make rust-tests   # Rust tests
./test_runner
cargo test --manifest-path bindings/rust/Cargo.toml
```
