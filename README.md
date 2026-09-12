# ReScaleDB

Fast embedded SQL database for Linux. Core is freestanding (no libc), mmap-backed, B+Tree storage.

## Architecture

- `src/libs/` - freestanding replacements for libc (string, mem, ctype, types)
- `src/arch/linux/` - syscall wrappers (mmap, msync, open, etc.) - only place allowed to use libc/syscalls, supports x86_64 and aarch64
- `src/core/` - database core, no libc includes:
  - `pager` - 4KB page management over mmap single file
  - `btree` - B+Tree order 64, copy-on-write, linked leaves
  - `txn` - MVCC single-writer / multi-reader
  - `dump` - save/load all databases to one file
  - `sql` - parser and executor
- `repl/` - interactive shell, only `arch` + `libs`, no stdio

## Build

```sh
make              # native (x86_64)
make ARCH=aarch64 # cross for ARM64
```

Outputs in `out/<arch>/`:
- `librsc.a` - static library
- `librsc.so` - shared library
- `repl` - interactive shell
- `include/` - headers for `sql.h`

Requires GCC on Linux.

## Test

```sh
make
./test_runner
```

## SQL

In-memory by default, files only on `DUMP`/`LOAD`:

```sql
CREATE DATABASE app;
USE app;
CREATE TABLE users(id INT, name TEXT);
CREATE INDEX idx ON users(id);
INSERT INTO users VALUES (1, 'alice');
SELECT * FROM users WHERE id > 1 ORDER BY name LIMIT 10;
UPDATE users SET name = 'bob' WHERE id = 1;
DELETE FROM users WHERE id = 1;
DROP DATABASE app;
DUMP ALL TO 'state.dump';
LOAD ALL FROM 'state.dump';
```

No files touch FS until `DUMP ALL` / `.save`. `DUMP` saves entire in-memory state (all DBs) to one file.

`SELECT` prints as table:

```
+----+-------+
| id | name  |
+----+-------+
| 1  | alice |
+----+-------+
```

## REPL

```sh
./out/x86_64/repl
```

```
rsc> CREATE DATABASE app;
rsc> USE app;
rsc> SELECT * FROM users;
rsc> .help
```

Dot commands are shortcuts for SQL `DUMP`/`LOAD`:

```
  .save [file]           - shortcut for DUMP ALL TO 'file' (default: state.rsc.dump)
  .load [file]           - shortcut for LOAD ALL FROM 'file' (default: state.rsc.dump)
  .help                  - show this help
  .exit                  - quit
```

## C API

```c
#include "src/core/sql/sql.h"

Db db;
db_init(&db); // in-memory
char out[4096];
db_exec(&db, "CREATE DATABASE app;", out, sizeof(out));
db_exec(&db, "CREATE TABLE users (id INT, name TEXT)", out, sizeof(out));
db_exec(&db, "INSERT INTO users VALUES (1, 'alice')", out, sizeof(out));
db_exec(&db, "SELECT * FROM users", out, sizeof(out));
db_exec(&db, "DUMP ALL TO 'state.dump';", out, sizeof(out));
db_exec(&db, "LOAD ALL FROM 'state.dump';", out, sizeof(out));
db_close(&db);

// low-level pager save/load
pager_save(db.pager, "state.dump");
pager_load(db.pager, "state.dump");
```

Link with `out/<arch>/librsc.a` or `librsc.so`. `db_open(path)` still available for file-backed mode.

## File Format

In-memory: `mmap(ANON)` 4KB pages, header (magic, version, root, freelist, catalog).
On `DUMP`: raw pager image saved to `state.dump` (single file for all DBs).
