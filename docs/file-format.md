# File Format

ReScaleDB is in-memory first: tables live in `mmap`ped 4KB pages and no
file is created until you run `DUMP ALL` (see [SQL](sql.md)). A dump is
a byte-for-byte image of the pager mapping, so `LOAD ALL` just reads it
back. There is exactly one dump file for the whole in-memory state.

## Page 0: header

Offset 0 holds the file header (all integers little-endian):

| Offset | Size | Field        | Value                              |
|--------|------|--------------|------------------------------------|
| 0      | 4    | magic        | `0x52534344` (`"RSCD"`)            |
| 4      | 4    | version      | `3` (current)                      |
| 8      | 4    | page_size    | `4096`                             |
| 12     | 4    | page_count   | number of mapped pages             |
| 16     | 8    | root_pageno  | reserved                           |
| 24     | 8    | freelist_head| first free page, `0` = none        |
| 32     | 8    | txn_id       | last committed transaction id      |

The rest of page 0 is padding. Pages 1..N are 4KB B+Tree nodes or free
pages (a free page stores the next free page number in its first 8 bytes).

## Catalog (offset 64)

Right after the fixed header fields, at byte offset 64 of page 0:

- `u32 ntables`
- per table: `name[32]`, `u32 ncols`, per column
  `name[32]`, `u32 type` (`0` INT, `1` TEXT), `u32 is_pk`,
  `u32 is_unique`, `u32 is_not_null`, `u32 has_default`,
  `u32 default_is_null`, `default_val[64]`,
  then `u64 root` (B+Tree root page), `u64 rowid_seq`,
  `i32 pk_col` (`-1` = none), `i32 has_nullmap`.

The catalog is rewritten on every successful statement and `msync`ed
for file-backed databases.

## Rows (v3 tables)

Each row stored in a B+Tree leaf entry:

- `u16 null-bitmap`, bit `i` = column `i` is `NULL` (max 16 columns)
- then each non-null column in order: `INT` = 8 bytes little-endian,
  `TEXT` = `u16` length + bytes

## Versions

- v1: no constraints, rows without bitmap.
- v2: `PRIMARY KEY` (`is_pk`, `pk_col`).
- v3 (current): `UNIQUE` / `NOT NULL` / `DEFAULT`, null-bitmap rows.

New code reads old images: v1/v2 tables load with the new constraint
fields zeroed and `has_nullmap = 0`, and their rows decode as all
non-null. Going the other way is not supported — don't `LOAD` a v3
dump with an older binary.
