package dev.aplominski.rescaledb;

import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

public class Database implements AutoCloseable {
    static {
        try {
            String arch = System.getProperty("os.arch").toLowerCase();
            String os = System.getProperty("os.name").toLowerCase();
            String plat = "linux-x86_64";
            if (arch.contains("aarch64") || arch.contains("arm64")) plat = "linux-aarch64";
            else if (arch.contains("arm")) plat = "linux-arm";
            if (!os.contains("linux")) plat = "linux-x86_64";
            String lib = "native/" + plat + "/librsc_jni.so";
            File tmp = File.createTempFile("librsc_jni", ".so");
            tmp.deleteOnExit();
            try (InputStream in = Database.class.getResourceAsStream("/" + lib)) {
                if (in != null) {
                    Files.copy(in, tmp.toPath(), StandardCopyOption.REPLACE_EXISTING);
                    System.load(tmp.getAbsolutePath());
                } else {
                    try (InputStream in2 = Database.class.getResourceAsStream("/native/librsc_jni.so")) {
                        if (in2 != null) {
                            Files.copy(in2, tmp.toPath(), StandardCopyOption.REPLACE_EXISTING);
                            System.load(tmp.getAbsolutePath());
                        } else {
                            System.loadLibrary("rsc_jni");
                        }
                    }
                }
            }
        } catch (Exception e) {
            try { System.loadLibrary("rsc_jni"); } catch (Throwable t) {}
        }
    }

    private long handle;

    private static native long nInit();
    private static native int nExec(long handle, String sql, byte[] out);
    private static native int nQuery(long handle, String sql, QueryResult res);
    private static native void nClose(long handle);
    private static native void nEnableDebug(int on);

    public Database() {
        handle = nInit();
        if (handle == 0) throw new RuntimeException("init failed");
    }

    public String exec(String sql) {
        byte[] out = new byte[8192];
        int rc = nExec(handle, sql, out);
        String s = new String(out).trim();
        int z = s.indexOf('\0');
        if (z >= 0) s = s.substring(0, z);
        if (rc != 0) throw new RuntimeException(s.isEmpty() ? "exec failed" : s);
        return s;
    }

    public QueryResult query(String sql) {
        QueryResult r = new QueryResult();
        int rc = nQuery(handle, sql, r);
        if (rc != 0) throw new RuntimeException("query failed: " + sql);
        return r;
    }

    public void enableDebug(boolean on) {
        nEnableDebug(on ? 1 : 0);
    }

    @Override
    public void close() {
        if (handle != 0) {
            nClose(handle);
            handle = 0;
        }
    }
}
