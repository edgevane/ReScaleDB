use rescaledb::Database;
fn main() -> Result<(), String> {
    let mut db = Database::new()?;
    db.exec("CREATE TABLE t(id INT, val INT);")?;
    db.exec("INSERT INTO t VALUES (1,10);")?;
    db.exec("INSERT INTO t VALUES (2,20);")?;
    let res = db.query("SELECT * FROM t WHERE val>15;")?;
    println!("cols {:?} rows {:?}", res.columns, res.rows);
    let cnt = db.query("SELECT COUNT(*) FROM t;")?;
    println!("count {:?}", cnt.rows);
    let avg = db.query("SELECT AVG(val) FROM t;")?;
    println!("avg {:?}", avg.rows);
    Ok(())
}
