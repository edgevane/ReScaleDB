package dev.aplominski.rescaledb;

public class QueryResult {
    public int ncols;
    public String[] cols = new String[16];
    public int nrows;
    public String[][] cells = new String[256][16];

    public QueryResult() {
        for (int i = 0; i < 16; i++) cols[i] = "";
        for (int r = 0; r < 256; r++) for (int c = 0; c < 16; c++) cells[r][c] = "";
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();
        for (int c = 0; c < ncols; c++) {
            if (c > 0) sb.append(" | ");
            sb.append(cols[c]);
        }
        sb.append("\n");
        for (int r = 0; r < nrows; r++) {
            for (int c = 0; c < ncols; c++) {
                if (c > 0) sb.append(" | ");
                sb.append(cells[r][c]);
            }
            sb.append("\n");
        }
        return sb.toString();
    }
}
