# REPL

The REPL is a tiny interactive shell around the engine. It reads one
statement per line, runs it, and prints the result.

## Run

```sh
make                    # builds out/x86_64/repl (see build-test.md)
./out/x86_64/repl
```

On start you get an empty in-memory database and the `rsc>` prompt.

## Session example

```
rsc> CREATE TABLE users(id INT PRIMARY KEY, name TEXT NOT NULL);
OK
rsc> INSERT INTO users VALUES (1, 'alice');
OK
rsc> INSERT INTO users VALUES (1, 'bob');
ERR duplicate primary key '1'
rsc> SELECT * FROM users;
+----+-------+
| id | name  |
+----+-------+
| 1  | alice |
+----+-------+
rsc> .save state.dump
OK
rsc> .exit
bye
```

`SELECT` results render as ASCII tables (`NULL` cells print as `NULL`,
empty results as `(empty)`). Errors print as one-line `ERR ...` messages.

## Dot commands

Dot commands are REPL-only shortcuts for the SQL persistence statements
(documented in [SQL](sql.md)). They accept an optional file name;
the default is `state.rsc.dump`.

| Command      | Equivalent SQL             | Meaning                              |
|--------------|----------------------------|--------------------------------------|
| `.save [f]`  | `DUMP ALL TO 'f';`         | write in-memory state to one file    |
| `.load [f]`  | `LOAD ALL FROM 'f';`       | replace in-memory state from a file  |
| `.help`      | —                          | list dot commands                    |
| `.exit`      | —                          | quit (also `.quit`)                  |

Nothing is written to disk unless you run `.save` / `DUMP ALL`.
Exiting without saving discards everything — that is by design.
