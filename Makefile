CC=gcc
AR=ar
ARCH ?= $(shell uname -m)
ifeq ($(ARCH),aarch64)
  CC=aarch64-linux-gnu-gcc
  AR=aarch64-linux-gnu-ar
endif
ifeq ($(ARCH),arm)
  CC=arm-linux-gnueabihf-gcc
  AR=arm-linux-gnueabihf-ar
endif
CFLAGS_CORE=-Wall -Wextra -O2 -ffreestanding -nostdlib -nostdinc -fno-builtin -Isrc -fPIC
CFLAGS_TEST=-Wall -Wextra -O2 -Isrc -g
SRC_LIBS=src/libs/string.c src/libs/mem.c
SRC_ARCH=src/arch/linux/arch.c
SRC_CORE=src/core/pager.c src/core/btree.c src/core/txn.c src/core/dump.c src/core/sql/parser.c src/core/sql/executor.c src/core/sql/db.c
OBJ_LIBS=$(SRC_LIBS:.c=.o)
OBJ_ARCH=$(SRC_ARCH:.c=.o)
OBJ_CORE=$(SRC_CORE:.c=.o)
OBJS=$(OBJ_LIBS) $(OBJ_ARCH) $(OBJ_CORE)

OUT=out/$(ARCH)
LIB_A=$(OUT)/librsc.a
LIB_SO=$(OUT)/librsc.so

HEADERS=src/libs/types.h src/libs/string.h src/libs/mem.h src/libs/ctype.h src/arch/arch.h src/core/pager.h src/core/btree.h src/core/txn.h src/core/dump.h src/core/sql/sql.h

REPL_SRC=repl/repl.c
REPL_BIN=$(OUT)/repl

all: $(LIB_A) $(LIB_SO) headers test_runner $(REPL_BIN)

$(OUT):
	mkdir -p $(OUT)

headers: | $(OUT)
	mkdir -p $(OUT)/include
	cp --parents $(HEADERS) $(OUT)/include/

src/libs/%.o: src/libs/%.c
	$(CC) $(CFLAGS_CORE) -c $< -o $@

src/arch/linux/%.o: src/arch/linux/%.c
	$(CC) $(CFLAGS_CORE) -c $< -o $@

src/core/%.o: src/core/%.c
	$(CC) $(CFLAGS_CORE) -c $< -o $@

src/core/sql/%.o: src/core/sql/%.c
	$(CC) $(CFLAGS_CORE) -c $< -o $@

$(LIB_A): $(OBJS) | $(OUT)
	$(AR) rcs $@ $^

$(LIB_SO): $(OBJS) | $(OUT)
	$(CC) -shared -o $@ $^

test_runner: $(LIB_A) test/test.c
	$(CC) $(CFLAGS_TEST) test/test.c $(LIB_A) -o test_runner

$(REPL_BIN): $(LIB_A) $(REPL_SRC) | $(OUT)
	$(CC) $(CFLAGS_TEST) $(REPL_SRC) $(LIB_A) -o $@

rust:
	cargo build --manifest-path bindings/rust/Cargo.toml

rust-tests: rust
	cargo test --manifest-path bindings/rust/Cargo.toml

rust_tests: rust-tests

tests: test_runner
	./test_runner

clean:
	rm -rf out src/libs/*.o src/arch/linux/*.o src/core/*.o src/core/sql/*.o test_runner demo /tmp/test.rsc.db /tmp/bt_test.rsc.db
	cargo clean --manifest-path bindings/rust/Cargo.toml 2>/dev/null; true

.PHONY: all clean rust rust-tests rust_tests tests
