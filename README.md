# ReScaleDB

Fast embedded SQL database for Linux. Core is freestanding (no libc), mmap-backed, B+Tree storage.

## Architecture

- `src/libs/` - freestanding replacements for libc (string, mem, ctype, types)
- `src/arch/linux/` - syscall wrappers (mmap, msync, open, etc.) - only place allowed to use libc/syscalls
- `src/core/` - database core, no libc includes:
  - `pager` - 4KB page management over mmap single file
  - `btree` - B+Tree order 64, copy-on-write, linked leaves
  - `txn` - MVCC single-writer / multi-reader
  - `sql` - parser and executor (CREATE TABLE, INSERT, SELECT WHERE, UPDATE, DELETE, ORDER BY, LIMIT, CREATE INDEX)

## Build

```sh
make
```

Outputs:
- `out/librsc.a` - static library
- `out/librsc.so` - shared library
- `test_runner` - tests

Requires GCC on Linux x86_64.

## Test

```sh
make
./test_runner
```

## Use

```c
#include "src/core/sql/sql.h"

Db db;
db_open(&db, "db.rsc");
char out[4096];
db_exec(&db, "CREATE TABLE users (id INT, name TEXT)", out, sizeof(out));
db_exec(&db, "INSERT INTO users VALUES (1, 'alice')", out, sizeof(out));
db_exec(&db, "SELECT * FROM users WHERE id > 1 ORDER BY name LIMIT 10", out, sizeof(out));
db_close(&db);
```

Link with `out/librsc.a` or `out/librsc.so`.

## File Format

Single file `*.rsc.db`: header (magic, version, root, freelist) + 4KB pages, msync on commit.
