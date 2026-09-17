SHELL := /bin/sh
.DEFAULT_GOAL := all

ARCH ?= $(shell uname -m)
BUILD_DIR ?= build/$(ARCH)
OUT ?= out/$(ARCH)

ifeq ($(origin CC),default)
  CC := gcc
  ifeq ($(ARCH),aarch64)
    CC := aarch64-linux-gnu-gcc
  endif
  ifeq ($(ARCH),arm)
    CC := arm-linux-gnueabihf-gcc
  endif
endif

ifeq ($(origin AR),default)
  AR := ar
  ifeq ($(ARCH),aarch64)
    AR := aarch64-linux-gnu-ar
  endif
  ifeq ($(ARCH),arm)
    AR := arm-linux-gnueabihf-ar
  endif
endif

GRADLE ?= gradle
CARGO ?= cargo
JAVA_HOME ?= $(shell dirname "$$(dirname "$$(readlink -f "$$(command -v javac 2>/dev/null)")")" 2>/dev/null)

WARNINGS := -Wall -Wextra
OPT ?= -O2
CPPFLAGS += -Isrc
CFLAGS_CORE ?= $(WARNINGS) $(OPT) -ffreestanding -nostdlib -nostdinc -fno-builtin -fPIC
CFLAGS_TEST ?= $(WARNINGS) $(OPT) -g
CFLAGS_JNI ?= $(WARNINGS) $(OPT) -fPIC
DEPFLAGS = -MMD -MP
LDFLAGS_SHARED ?= -shared

SRC_LIBS := src/libs/string.c src/libs/mem.c
SRC_ARCH := src/arch/linux/arch.c
SRC_CORE := \
	src/core/pager.c \
	src/core/btree.c \
	src/core/txn.c \
	src/core/dump.c \
	src/core/debug.c \
	src/core/sql/parser.c \
	src/core/sql/exec_util.c \
	src/core/sql/exec_ddl.c \
	src/core/sql/exec_insert.c \
	src/core/sql/exec_select.c \
	src/core/sql/exec_write.c \
	src/core/sql/exec_stmt.c \
	src/core/sql/exec_query.c \
	src/core/sql/db.c
SRC := $(SRC_LIBS) $(SRC_ARCH) $(SRC_CORE)
OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEPS := $(OBJS:.o=.d)

HEADERS := \
	src/libs/types.h \
	src/libs/string.h \
	src/libs/mem.h \
	src/libs/ctype.h \
	src/arch/arch.h \
	src/core/pager.h \
	src/core/btree.h \
	src/core/txn.h \
	src/core/dump.h \
	src/core/debug.h \
	src/core/sql/sql.h

LIB_A := $(OUT)/librsc.a
LIB_SO := $(OUT)/librsc.so
REPL_BIN := $(OUT)/repl
TEST_BIN := test_runner

JNI_SRC := bindings/java/src/main/c/rescaledb_jni.c
JAVA_PLATFORM_x86_64 := linux-x86_64
JAVA_PLATFORM_aarch64 := linux-aarch64
JAVA_PLATFORM_arm64 := linux-aarch64
JAVA_PLATFORM ?= $(or $(JAVA_PLATFORM_$(ARCH)),linux-$(ARCH))
JNI_LIB := bindings/java/src/main/resources/native/$(JAVA_PLATFORM)/librsc_jni.so
JNI_LEGACY_LIB := bindings/java/src/main/resources/native/librsc_jni.so

.PHONY: all lib shared static headers repl test tests jni jar java java-tests java-publish-local java-publish java-publish-github rust rust-tests rust_tests release cross-x86_64 cross-aarch64 clean

all: lib repl

lib: static shared headers

static: $(LIB_A)

shared: $(LIB_SO)

headers: $(HEADERS) | $(OUT)
	mkdir -p $(OUT)/include
	cp --parents $(HEADERS) $(OUT)/include/

repl: $(REPL_BIN)

test tests: $(TEST_BIN)
	./$(TEST_BIN)

jni: $(JNI_LIB)

jar:
	$(GRADLE) -p bindings/java build
	mkdir -p out
	cp "$$(find bindings/java/build/libs -maxdepth 1 -type f -name '*.jar' ! -name '*-sources.jar' ! -name '*-javadoc.jar' | head -n 1)" out/multiarch.jar

java: jar

java-tests:
	$(GRADLE) -p bindings/java test

java-publish-local:
	$(GRADLE) -p bindings/java publishToMavenLocal

java-publish java-publish-github:
	$(GRADLE) -p bindings/java publishMavenPublicationToGitHubPackagesRepository

rust:
	$(CARGO) build --manifest-path bindings/rust/Cargo.toml

rust-tests rust_tests:
	$(CARGO) test --manifest-path bindings/rust/Cargo.toml

release: cross-x86_64 cross-aarch64 jar

cross-x86_64:
	$(MAKE) ARCH=x86_64 BUILD_DIR=build/x86_64 OUT=out/x86_64 out/x86_64/librsc.so
	mkdir -p out
	cp out/x86_64/librsc.so out/x86_64.so

cross-aarch64:
	@command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || { echo "Missing aarch64-linux-gnu-gcc. Install gcc-aarch64-linux-gnu."; exit 1; }
	$(MAKE) ARCH=aarch64 BUILD_DIR=build/aarch64 OUT=out/aarch64 out/aarch64/librsc.so
	mkdir -p out
	cp out/aarch64/librsc.so out/aarch64.so

$(OUT) $(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_CORE) $(DEPFLAGS) -c $< -o $@

$(LIB_A): $(OBJS) | $(OUT)
	$(AR) rcs $@ $^

$(LIB_SO): $(OBJS) | $(OUT)
	$(CC) $(LDFLAGS_SHARED) -o $@ $^

$(REPL_BIN): repl/repl.c $(OBJS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS_TEST) $^ -o $@

$(TEST_BIN): test/test.c $(LIB_A)
	$(CC) $(CPPFLAGS) $(CFLAGS_TEST) $< $(LIB_A) -o $@

$(JNI_LIB): $(SRC) $(JNI_SRC)
	@test -n "$(JAVA_HOME)" || { echo "JAVA_HOME is not set and javac was not found."; exit 1; }
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_JNI) -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux $(LDFLAGS_SHARED) -o $@ $^
	@if [ "$(ARCH)" = "x86_64" ]; then mkdir -p $(dir $(JNI_LEGACY_LIB)); cp -f $@ $(JNI_LEGACY_LIB); fi

clean:
	rm -rf out build $(TEST_BIN) demo
	rm -f /tmp/test.rsc.db /tmp/bt_test.rsc.db
	$(CARGO) clean --manifest-path bindings/rust/Cargo.toml 2>/dev/null || true
	$(GRADLE) -p bindings/java clean 2>/dev/null || true

-include $(DEPS)
