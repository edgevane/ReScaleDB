package dev.aplominski.rescaledb;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
public class DatabaseTest {
    @Test
    public void testBasic() {
        try (Database db = new Database()) {
            db.exec("CREATE TABLE t(id INT, name TEXT);");
            db.exec("INSERT INTO t VALUES (1, 'a');");
            QueryResult r = db.query("SELECT * FROM t;");
            assertEquals(1, r.nrows);
            assertEquals("a", r.cells[0][1]);
            QueryResult c = db.query("SELECT COUNT(*) FROM t;");
            assertEquals("1", c.cells[0][0]);
        }
    }
}
