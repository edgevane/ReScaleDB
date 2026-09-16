# Rust API

Crate `rescaledb` mirrors the [C API](c-api.md): `exec` returns the
table/error string, `query` returns structured rows.

```toml
[dependencies]
rescaledb = "0.1"
# or: rescaledb = { path = "../bindings/rust" }
```

```rust
use rescaledb::Database;

fn main() -> Result<(), String> {
    let mut db = Database::new()?; // in-memory
    // let mut db = Database::open("file.rsc.db")?; // file-backed

    db.exec("CREATE TABLE t(id INT PRIMARY KEY, name TEXT);")?;
    db.exec("INSERT INTO t VALUES (1, 'a');")?;

    // string table (same text as the REPL prints)
    println!("{}", db.exec("SELECT * FROM t;")?);

    // structured rows
    let res = db.query("SELECT id, name FROM t WHERE id > 0;")?;
    println!("cols {:?}", res.columns);
    for row in &res.rows {
        println!("{:?}", row);
    }

    let cnt = db.query("SELECT COUNT(*) FROM t;")?;
    println!("count {}", cnt.rows[0][0]);

    db.exec("DUMP ALL TO 'state.dump';")?;
    db.exec("LOAD ALL FROM 'state.dump';")?;
    Ok(())
}
```

Notes:

- `exec` returns `Ok(String)` on success, `Err(String)` with the engine's
  `ERR ...` line on failure.
- `query` returns `Ok(QueryResult { columns: Vec<String>, rows: Vec<Vec<String>> })`;
  `NULL` cells are the string `"NULL"`, at most 256 rows.
- `Database` closes the DB on drop; `db.enable_debug(true)` turns on the
  C engine's stderr diagnostics.
- Develop: `make rust`, `make rust-tests`, or plain `cargo test`.
