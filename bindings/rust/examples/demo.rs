use rescaledb::Database;
fn main() -> Result<(), String> {
    let mut db = Database::new()?;
    db.exec("CREATE TABLE users(id INT, name TEXT);")?;
    db.exec("INSERT INTO users VALUES (1, 'alice');")?;
    db.exec("INSERT INTO users VALUES (2, 'bob');")?;
    let out = db.exec("SELECT * FROM users;")?;
    println!("{}", out);
    let cnt = db.exec("SELECT COUNT(*) FROM users;")?;
    println!("count: {}", cnt);
    db.exec("DUMP ALL TO 'demo.dump';")?;
    println!("saved");
    Ok(())
}
