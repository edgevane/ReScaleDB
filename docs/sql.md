# SQL

ReScaleDB speaks a deliberately small subset of SQL: one in-memory database,
tables with `INT`/`TEXT` columns, and the statements below. Anything outside
this page is not supported — the engine returns `ERR parse` or `ERR exec`.

Conventions used here: `INT` is a signed 64-bit integer, `TEXT` is a
variable-length string (max 255 bytes on insert). String literals use
single quotes (`'alice'`). `NULL` is a value, not a string — unquoted
`NULL` means null, quoted `'NULL'` means the 4-letter text.

## Databases

The engine holds one in-memory database. Nothing touches the filesystem
until you ask (see Persistence).

```sql
CREATE DATABASE app;   -- reset to a fresh empty database
USE app;               -- no-op, accepted for compatibility
DROP DATABASE app;     -- no-op, accepted for compatibility
```

## Tables

```sql
CREATE TABLE users(
  id    INT PRIMARY KEY,
  email TEXT UNIQUE NOT NULL,
  name  TEXT NOT NULL,
  nick  TEXT DEFAULT 'anon',
  age   INT  DEFAULT 18,
  note  TEXT               -- nullable, no default
);
```

Column constraints, inline or as a table constraint:

```sql
CREATE TABLE t(id INT, name TEXT, PRIMARY KEY (id));
CREATE TABLE t(id INT, email TEXT, UNIQUE (email));
```

Rules:

- one `PRIMARY KEY` per table (`INT` or `TEXT`); implies `UNIQUE` + `NOT NULL`
- `UNIQUE` allows multiple `NULL`s, `PRIMARY KEY` rejects `NULL`
- `NOT NULL` rejects `NULL` on `INSERT` and `UPDATE`
- `DEFAULT <literal>` / `DEFAULT NULL` fills the value when the statement
  uses the `DEFAULT` keyword for that column
- a second `CREATE TABLE` with the same name fails

`CREATE INDEX name ON table(col)` is accepted but currently a no-op
(the primary B+Tree serves all scans).

## Vectors

```sql
CREATE TABLE docs(id INT PRIMARY KEY, v VECTOR[128]);
INSERT INTO docs VALUES (1, [0.1,0.2,0.3]);
SELECT * FROM docs WHERE cossim(v, [0.1,0.2,0.4], 0.1);
```

- `VECTOR[N]` stores `N` float32 components (`1`–`255`; one row holds
  ~1000 bytes total, so `N * 4` plus the other columns must fit).
- Vector literals are unquoted `[1.0,2.0,...]` (up to 1024 chars by
  default; see `rsc_set_vector_limit`). Inserts with a wrong element
  count or non-numeric elements fail with `ERR invalid vector`.
- `cossim(col, [query...], max_distance)` keeps rows whose cosine
  distance (`1 - cosine similarity`) to the query vector is `<=`
  max_distance. The query vector must have exactly `N` elements
  (`ERR cossim dimension mismatch` otherwise). `NULL` vectors never
  match. A tiny epsilon applies, so an exact match passes a threshold
  of `0`. Matching is a brute-force scan (no ANN index).
- Optional 4th argument: `cossim(col, [query...], max_distance, k)`
  returns at most `k` rows, nearest first (`k >= 1`). Applies to
  row-returning `SELECT` (ignored by aggregates and by `UPDATE` /
  `DELETE`, which filter only).
- `UNIQUE` is allowed on `VECTOR` (exact match); `PRIMARY KEY`,
  `DEFAULT`, `ORDER BY` and aggregates over `VECTOR` are rejected.
- In `SELECT` output and `db_query` cells vectors print as
  `[f,f,...]`, truncated to 63 chars like long `TEXT`.

## Insert

`VALUES` are positional and must match the column count.
Use the unquoted keywords `NULL` and `DEFAULT`:

```sql
INSERT INTO users VALUES (1, 'a@x', 'Ann', 'ann', 20, 'hi');
INSERT INTO users VALUES (2, 'b@x', 'Bob', DEFAULT, DEFAULT, NULL);
```

Failures:

- `ERR duplicate primary key '1'` — PK value already exists
- `ERR duplicate value 'a@x' for UNIQUE 'email'` — UNIQUE violation
- `ERR column 'name' cannot be null` — NULL into NOT NULL / PK
- `ERR no default value for column 'nick'` — `DEFAULT` keyword with no default declared
- `ERR column count` — wrong number of values

## Select

```sql
SELECT * FROM users;
SELECT id, name FROM users;
SELECT * FROM users WHERE id > 1 AND name = 'bob' ORDER BY id LIMIT 10;
SELECT * FROM users WHERE nick IS NULL;
SELECT * FROM users WHERE age IS NOT NULL;
SELECT * FROM users WHERE name != 'Ann';
```

- `WHERE` operators: `= != <> < > <= >=`, combined with `AND`.
  `= NULL` never matches — use `IS NULL` / `IS NOT NULL`.
- `ORDER BY <col>` sorts ascending, `NULL`s last.
- `LIMIT <n>` caps the rows.
- `SELECT` of a missing column/table fails with
  `ERR column 'x' does not exist` / `ERR table 'x' does not exist`.
- In the REPL and `db_exec` the result prints as an ASCII table;
  `NULL` cells print as `NULL`; empty results print `(empty)`.

`DISTINCT` removes duplicate rows after projection:

```sql
SELECT DISTINCT nick FROM users;
SELECT DISTINCT * FROM users WHERE age > 18 ORDER BY nick LIMIT 10;
```

## Aggregates

```sql
SELECT COUNT(*) FROM users;
SELECT COUNT() FROM users;                -- same as COUNT(*)
SELECT COUNT(nick) FROM users;            -- non-null only
SELECT COUNT(DISTINCT nick) FROM users;
SELECT AVG(age) FROM users;               -- ignores NULL, NULL if none
SELECT AVG(DISTINCT age) FROM users;
SELECT SUM(age) FROM users;               -- ignores NULL, NULL if none
SELECT SUM(DISTINCT age) FROM users;
SELECT MAX(age) FROM users;               -- ignores NULL, NULL if none
SELECT MIN(nick) FROM users;              -- lexicographic for TEXT
SELECT MAX(DISTINCT age) FROM users;      -- DISTINCT accepted, same result
```

`COUNT(*)` (or bare `COUNT()`) counts rows. `COUNT(col)` / `AVG(col)` /
`SUM(col)` / `MAX(col)` / `MIN(col)` skip `NULL`s. `AVG` and `SUM`
require an `INT` column. `MAX`/`MIN` work on both `INT` (numeric) and
`TEXT` (lexicographic) columns. All except `COUNT` return `NULL`
when no non-null rows match.

## Update / Delete

```sql
UPDATE users SET nick = NULL WHERE id = 1;
UPDATE users SET age = DEFAULT WHERE id = 1;
DELETE FROM users WHERE id = 1;
```

Setting a PK/`UNIQUE` column to an existing value fails like on insert.
Setting `NOT NULL` to `NULL` fails. Updating several rows to the same
PK/`UNIQUE` value fails.

## Persistence

```sql
DUMP ALL TO 'state.dump';    -- write the whole in-memory state to one file
LOAD ALL FROM 'state.dump';  -- replace in-memory state from that file
```

## Errors and debug

Every failure returns `-1` and a one-line `ERR ...` message.
With `rsc_enable_debug(1)` the engine also describes the failure on
stderr: the query, the table, the available columns (with `PRIMARY KEY` /
`UNIQUE` / `NOT NULL` / `DEFAULT` markers), per-column resolution, and
the error — for example a duplicate PK:

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
