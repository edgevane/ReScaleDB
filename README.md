# ReScaleDB

Fast embedded SQL database for Linux. Core is freestanding (no libc), mmap-backed, B+Tree storage. In-memory by default, explicit persistence via `DUMP`/`LOAD`.

```sql
CREATE TABLE users(id INT PRIMARY KEY, name TEXT NOT NULL);
INSERT INTO users VALUES (1, 'alice');
SELECT * FROM users WHERE id > 0 ORDER BY id;
SELECT COUNT(DISTINCT name) FROM users;
DUMP ALL TO 'state.dump';
```

## Features

- In-memory `mmap(ANON)` + B+Tree (order 64, 4KB pages, copy-on-write), MVCC single-writer / multi-reader
- SQL: `PRIMARY KEY` / `UNIQUE` / `NOT NULL` / `DEFAULT`, `NULL`, `DISTINCT`, `WHERE` (`= != <> < > <= >=`, `IS NULL`), `ORDER BY`, `LIMIT`, `COUNT` / `AVG` / `MIN` / `MAX` (skip `NULL`s)
- REPL prints tables, code gets structured results (`db_query`)
- Non-libc core (`src/libs` + `src/arch`), Linux x86_64 and aarch64, GCC freestanding
- Bindings: Rust (`bindings/rust`), Java (`bindings/java`, multiarch jar)

## Docs

- [SQL reference](docs/sql.md)
- [REPL](docs/repl.md)
- [C API](docs/c-api.md)
- [Rust API](docs/rust-api.md)
- [Java API](docs/java-api.md)
- [File format](docs/file-format.md)
- [Build & test](docs/build-test.md)

## Quickstart

```sh
make          # -> out/x86_64.so, out/aarch64.so, out/multiarch.jar
./test_runner # C tests
```

```c
Db db; db_init(&db);
char out[4096];
db_exec(&db, "CREATE TABLE t(id INT, name TEXT);", out, sizeof(out));
db_exec(&db, "SELECT * FROM t;", out, sizeof(out)); // table string
RscResult r; db_query(&db, "SELECT * FROM t;", &r); // structured
db_close(&db);
```

No files touch FS until `DUMP ALL` / `.save`. One dump file holds entire in-memory state.

## Architecture

```
src/libs/        freestanding libc replacements (string, mem, ctype, types)
src/arch/linux/  syscall wrappers (x86_64 + aarch64) — only place with syscalls
src/core/        pager, btree, txn, dump, sql (parser/executor/db)
repl/            interactive shell, only arch+libs, no stdio
bindings/        rust (crates.io-ready), java (Maven-ready, os.arch natives)
```
