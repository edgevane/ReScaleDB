fn main() {
    let mut b = cc::Build::new();
    b.include("../../src");
    b.include("../../src/libs");
    b.include("../../src/arch");
    b.include("../../src/core");
    b.file("../../src/libs/string.c");
    b.file("../../src/libs/mem.c");
    b.file("../../src/arch/linux/arch.c");
    b.file("../../src/core/pager.c");
    b.file("../../src/core/btree.c");
    b.file("../../src/core/txn.c");
    b.file("../../src/core/dump.c");
    b.file("../../src/core/sql/parser.c");
    b.file("../../src/core/sql/executor.c");
    b.file("../../src/core/sql/db.c");
    b.flag("-fPIC");
    b.flag("-O2");
    b.compile("rsc");
}
