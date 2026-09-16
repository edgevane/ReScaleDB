# Java API

Maven coordinates: `dev.aplominski:rescaledb:0.1.0`. Same two result
modes as the [C API](c-api.md): `exec` returns text, `query` returns
a structured object.

```java
import dev.aplominski.rescaledb.Database;
import dev.aplominski.rescaledb.QueryResult;

try (Database db = new Database()) {          // in-memory
    db.exec("CREATE TABLE t(id INT PRIMARY KEY, name TEXT);");
    db.exec("INSERT INTO t VALUES (1, 'a');");

    System.out.println(db.exec("SELECT * FROM t;"));  // table text

    QueryResult r = db.query("SELECT id, name FROM t WHERE id > 0;");
    System.out.println(r);                    // columns + rows

    db.exec("DUMP ALL TO 'state.dump';");
    db.exec("LOAD ALL FROM 'state.dump';");
}
```

Notes:

- Failures throw `RuntimeException` carrying the engine's `ERR ...` line.
- `QueryResult` exposes `ncols`/`cols[]`, `nrows`/`cells[][]`;
  `NULL` cells are the string `"NULL"`, at most 256 rows.
- `db.enableDebug(true)` turns on the C engine's stderr diagnostics.
- The jar bundles `librsc_jni.so` natives for `linux-x86_64` and
  `linux-aarch64`, picked at runtime via `os.arch` (falls back to
  `System.loadLibrary("rsc_jni")`). The pure C `out/*.so` from
  [Build](build-test.md) contains no JNI symbols — JNI lives only in
  the jar's bundled library.
- Develop/publish: `make java`, `make java-tests`,
  `make java-publish-local`, `make java-publish` (needs
  `MAVEN_USERNAME` / `MAVEN_PASSWORD` for Central).
