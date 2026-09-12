# rescaledb

Safe Rust bindings for ReScaleDB - fast embedded SQL database.

```rust
use rescaledb::Database;

let mut db = Database::new().unwrap();
db.exec("CREATE TABLE users(id INT, name TEXT);").unwrap();
db.exec("INSERT INTO users VALUES (1, 'alice');").unwrap();

// String table (REPL style)
let table = db.exec("SELECT * FROM users;").unwrap();
println!("{}", table);

// Structured (code-friendly)
let res = db.query("SELECT * FROM users;").unwrap();
println!("{:?}", res.columns);
for row in res.rows {
    println!("{:?}", row);
}

let cnt = db.query("SELECT COUNT(*) FROM t;").unwrap();
let avg = db.query("SELECT AVG(val) FROM t;").unwrap();

db.exec("DUMP ALL TO 'state.dump';").unwrap();
```

File-backed:

```rust
let mut db = Database::open("mydb.rsc.db").unwrap();
```

Run tests:

```sh
cargo test
```
