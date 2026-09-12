use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_ulong};

#[repr(C)]
struct Pager {
    fd: i32,
    _pad: i32,
    map: *mut u8,
    map_len: c_ulong,
    hdr: *mut u8,
}

#[repr(C)]
struct Db {
    pager: *mut Pager,
    _opaque: [u8; 11000],
}

#[repr(C)]
pub struct RscResult {
    pub ncols: i32,
    pub cols: [[u8; 32]; 16],
    pub nrows: i32,
    pub cells: [[[u8; 64]; 16]; 256],
}

extern "C" {
    fn db_init(db: *mut Db) -> i32;
    fn db_open(db: *mut Db, path: *const c_char) -> i32;
    fn db_close(db: *mut Db) -> i32;
    fn db_exec(db: *mut Db, sql: *const c_char, out: *mut c_char, cap: usize) -> i32;
    fn db_query(db: *mut Db, sql: *const c_char, res: *mut RscResult) -> i32;
}

pub struct QueryResult {
    pub columns: Vec<String>,
    pub rows: Vec<Vec<String>>,
}

pub struct Database {
    db: Box<Db>,
}

impl Database {
    pub fn new() -> Result<Self, String> {
        let mut db = Box::new(unsafe { std::mem::zeroed::<Db>() });
        let rc = unsafe { db_init(db.as_mut() as *mut Db) };
        if rc != 0 {
            return Err("db_init failed".into());
        }
        Ok(Self { db })
    }

    pub fn open(path: &str) -> Result<Self, String> {
        let mut db = Box::new(unsafe { std::mem::zeroed::<Db>() });
        let cpath = CString::new(path).unwrap();
        let rc = unsafe { db_open(db.as_mut() as *mut Db, cpath.as_ptr()) };
        if rc != 0 {
            return Err(format!("open failed: {}", path));
        }
        Ok(Self { db })
    }

    pub fn exec(&mut self, sql: &str) -> Result<String, String> {
        let csql = CString::new(sql).unwrap();
        let mut out = vec![0u8; 8192];
        let rc = unsafe {
            db_exec(
                self.db.as_mut() as *mut Db,
                csql.as_ptr(),
                out.as_mut_ptr() as *mut c_char,
                out.len(),
            )
        };
        let cstr = unsafe { CStr::from_ptr(out.as_ptr() as *const c_char) };
        let s = cstr.to_string_lossy().into_owned();
        if rc != 0 {
            Err(s)
        } else {
            Ok(s)
        }
    }

    pub fn query(&mut self, sql: &str) -> Result<QueryResult, String> {
        let csql = CString::new(sql).unwrap();
        let mut res = unsafe { std::mem::zeroed::<RscResult>() };
        let rc = unsafe { db_query(self.db.as_mut() as *mut Db, csql.as_ptr(), &mut res) };
        if rc != 0 {
            return Err("query failed".into());
        }
        let mut cols = Vec::new();
        for i in 0..res.ncols as usize {
            let c = unsafe { CStr::from_ptr(res.cols[i].as_ptr() as *const c_char) };
            cols.push(c.to_string_lossy().into_owned());
        }
        let mut rows = Vec::new();
        for r in 0..res.nrows as usize {
            let mut row = Vec::new();
            for c in 0..res.ncols as usize {
                let cell = unsafe { CStr::from_ptr(res.cells[r][c].as_ptr() as *const c_char) };
                row.push(cell.to_string_lossy().into_owned());
            }
            rows.push(row);
        }
        Ok(QueryResult {
            columns: cols,
            rows,
        })
    }

    pub fn execute(&mut self, sql: &str) -> Result<(), String> {
        self.exec(sql).map(|_| ())
    }

    pub fn query_raw(&mut self, sql: &str) -> Result<String, String> {
        self.exec(sql)
    }
}

impl Drop for Database {
    fn drop(&mut self) {
        unsafe { db_close(self.db.as_mut() as *mut Db) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn basic() {
        let mut db = Database::new().unwrap();
        db.exec("CREATE TABLE t(id INT, name TEXT);").unwrap();
        db.exec("INSERT INTO t VALUES (1, 'a');").unwrap();
        let out = db.exec("SELECT * FROM t;").unwrap();
        assert!(out.contains("a"));
        let c = db.exec("SELECT COUNT(*) FROM t;").unwrap();
        assert!(c.contains("1"));
    }
}
