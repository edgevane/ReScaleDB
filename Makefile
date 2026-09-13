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
SRC_CORE=src/core/pager.c src/core/btree.c src/core/txn.c src/core/dump.c src/core/debug.c src/core/sql/parser.c src/core/sql/executor.c src/core/sql/db.c
BUILD_DIR=build/$(ARCH)
OBJ_LIBS=$(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC_LIBS))
OBJ_ARCH=$(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC_ARCH))
OBJ_CORE=$(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC_CORE))
OBJS=$(OBJ_LIBS) $(OBJ_ARCH) $(OBJ_CORE)

OUT=out/$(ARCH)
LIB_A=$(OUT)/librsc.a
LIB_SO=$(OUT)/librsc.so

HEADERS=src/libs/types.h src/libs/string.h src/libs/mem.h src/libs/ctype.h src/arch/arch.h src/core/pager.h src/core/btree.h src/core/txn.h src/core/dump.h src/core/debug.h src/core/sql/sql.h

REPL_SRC=repl/repl.c
REPL_BIN=$(OUT)/repl

ifeq ($(ARCH),x86_64)
all: out/x86_64.so out/aarch64.so out/multiarch.jar test_runner
	@rm -rf out/x86_64 out/aarch64
	@echo "out:" && ls -lh out 2>&1 | head -20
else
all: out/x86_64.so out/aarch64.so out/multiarch.jar
	@rm -rf out/x86_64 out/aarch64
	@echo "out:" && ls -lh out 2>&1 | head -20
endif

$(OUT):
	mkdir -p $(OUT)

$(BUILD_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_CORE) -c $< -o $@

headers: | $(OUT)
	mkdir -p $(OUT)/include
	cp --parents $(HEADERS) $(OUT)/include/

$(LIB_A): $(OBJS) | $(OUT)
	$(AR) rcs $@ $^

$(LIB_SO): $(OBJS) | $(OUT)
	$(CC) -shared -o $@ $^

test_runner: $(BUILD_DIR)/libs/string.o $(BUILD_DIR)/libs/mem.o $(BUILD_DIR)/arch/linux/arch.o $(BUILD_DIR)/core/pager.o $(BUILD_DIR)/core/btree.o $(BUILD_DIR)/core/txn.o $(BUILD_DIR)/core/dump.o $(BUILD_DIR)/core/debug.o $(BUILD_DIR)/core/sql/parser.o $(BUILD_DIR)/core/sql/executor.o $(BUILD_DIR)/core/sql/db.o
	mkdir -p $(BUILD_DIR)
	$(AR) rcs $(BUILD_DIR)/librsc.a $^
	$(CC) $(CFLAGS_TEST) test/test.c $(BUILD_DIR)/librsc.a -o test_runner

$(REPL_BIN): $(OBJS) | $(OUT)
	$(CC) $(CFLAGS_TEST) $(REPL_SRC) $(OBJS) -o $@

out/x86_64.so:
	$(MAKE) ARCH=x86_64 BUILD_DIR=build/x86_64 OUT=out/x86_64 out/x86_64/librsc.so
	mkdir -p out
	cp out/x86_64/librsc.so out/x86_64.so

out/aarch64.so:
	@if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then \
		$(MAKE) ARCH=aarch64 BUILD_DIR=build/aarch64 OUT=out/aarch64 out/aarch64/librsc.so && mkdir -p out && cp out/aarch64/librsc.so out/aarch64.so; \
	else echo "aarch64 cross not found, creating dummy"; mkdir -p out; touch out/aarch64.so; fi

out/multiarch.jar: out/x86_64.so out/aarch64.so
	cd bindings/java && gradle -q build
	mkdir -p out
	MAIN_JAR=$$(ls bindings/java/build/libs/*.jar | grep -v -e '-sources\.jar' -e '-javadoc\.jar' | head -n 1); cp "$$MAIN_JAR" out/multiarch.jar

rust:
	cargo build --manifest-path bindings/rust/Cargo.toml

rust-tests: rust
	cargo test --manifest-path bindings/rust/Cargo.toml

rust_tests: rust-tests

java:
	cd bindings/java && gradle build

java-tests:
	cd bindings/java && gradle test

java-publish-local:
	cd bindings/java && gradle publishToMavenLocal

java-publish:
	cd bindings/java && gradle publish

tests: test_runner
	./test_runner

clean:
	rm -rf out build src/libs/*.o src/arch/linux/*.o src/core/*.o src/core/sql/*.o test_runner demo /tmp/test.rsc.db /tmp/bt_test.rsc.db
	cargo clean --manifest-path bindings/rust/Cargo.toml 2>/dev/null; true

.PHONY: all clean rust rust-tests rust_tests tests java java-tests java-publish-local java-publish
