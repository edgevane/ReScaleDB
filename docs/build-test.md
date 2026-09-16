# Build & Test

Prerequisites: GCC, GNU make, Linux (x86_64 or aarch64).
Optional: Rust toolchain + cargo (Rust bindings), Java 17 + Gradle
(Java bindings), `aarch64-linux-gnu-gcc` (ARM cross builds).

## Make targets

| Target               | What it does                                              |
|----------------------|-----------------------------------------------------------|
| `make`               | native build → `out/` + `test_runner` (default `ARCH`)    |
| `make ARCH=aarch64`  | cross build (per-arch objects under `build/<arch>/`)      |
| `make rust`          | `cargo build` the Rust crate                              |
| `make rust-tests`    | `cargo test` the Rust crate                               |
| `make java`          | `gradle build` the Java bindings (multiarch natives)      |
| `make java-tests`    | `gradle test`                                             |
| `make java-publish-local` | publish jar to local Maven repo                      |
| `make java-publish`  | publish to Maven Central (`MAVEN_USERNAME`/`MAVEN_PASSWORD`) |
| `make tests`         | build + run `./test_runner` (C tests)                     |
| `make clean`         | remove `out/`, `build/`, binaries                         |

CI (`.github/workflows/ci.yml`) installs both compilers plus Rust/Java
and runs the C, Rust and Java builds and tests on every push/PR.

## Outputs

`out/` intentionally holds only three artifacts:

- `x86_64.so` — pure C engine (`db_exec`/`db_query`/…), zero `Java_*` symbols
- `aarch64.so` — same, cross-compiled (empty placeholder if no cross toolchain)
- `multiarch.jar` — Java bindings with bundled `linux-x86_64` +
  `linux-aarch64` JNI natives (selected at runtime via `os.arch`)

Intermediate objects live under `build/<arch>/` so native and cross
builds never mix relocations. C headers for embedding ship via the
`headers` target into `out/<arch>/include/`.

## Tests

```sh
make tests        # C: builds test_runner, runs it (must print "0 failures")
make rust-tests   # Rust: cargo test in bindings/rust
make java-tests   # Java: gradle test in bindings/java
```

`test/test.c` covers tables, `WHERE`/`ORDER BY`/`LIMIT`, `UPDATE`,
constraints (`PRIMARY KEY`/`UNIQUE`/`NOT NULL`/`DEFAULT`/`NULL`),
`DISTINCT`, aggregates and the B+Tree itself.
