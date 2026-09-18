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
| 4      | 4    | version      | `5` (current)                      |
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
  `name[32]`, `u32 type` (`0` INT, `1` TEXT, `2` VECTOR), `u32 dims`
  (`VECTOR` length, `0` otherwise), `u32 is_pk`,
  `u32 is_unique`, `u32 is_not_null`, `u32 has_default`,
  `u32 default_is_null`, `default_val[64]`, `u32 quant`
  (`0` FP32, `1` FP16, `2` Q8, `3` Q4, `4` Q2, `5` Q1; v5+,
  older images read as FP32),
  then `u64 root` (B+Tree root page), `u64 rowid_seq`,
  `i32 pk_col` (`-1` = none), `i32 has_nullmap`.

The catalog is rewritten on every successful statement and `msync`ed
for file-backed databases.

## Rows (v4 tables)

Each row stored in a B+Tree leaf entry:

- `u16 null-bitmap`, bit `i` = column `i` is `NULL` (max 16 columns)
- then each non-null column in order: `INT` = 8 bytes little-endian,
  `TEXT` = `u16` length + bytes, `VECTOR` = `vec_storage_bytes`
  (see below, no length prefix):
  - `FP32`: `dims` float32 little-endian
  - `FP16`: `dims` binary16 little-endian
  - `Q8`/`Q4`/`Q2`/`Q1`: one float32 scale little-endian, then packed
    codes (`dims` bytes / `ceil(dims/2)` / `ceil(dims/4)` /
    `ceil(dims/8)`; element 0 in the least-significant bits)

## Versions

- v1: no constraints, rows without bitmap.
- v2: `PRIMARY KEY` (`is_pk`, `pk_col`).
- v3: `UNIQUE` / `NOT NULL` / `DEFAULT`, null-bitmap rows.
- v4: `VECTOR` columns (`type = 2`, `dims` per column).
- v5 (current): `VECTOR` storage precision (`quant` per column,
  `0` = FP32 with byte-identical rows to v4).

New code reads old images: v1/v2 tables load with the new constraint
fields zeroed and `has_nullmap = 0`, and their rows decode as all
non-null; v3 tables load with `dims = 0`; v4 tables load with
`quant = 0` (FP32). Going the other way is not
supported — don't `LOAD` a v5 dump with an older binary.
