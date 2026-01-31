# CLAUDE.md

## Project Overview

**rc** (version 1.7.4) is a Unix reimplementation of the Plan 9 shell, originally designed by Tom Duff at Bell Labs and reimplemented for Unix by Byron Rakitzis. It is a C-like shell with clean syntax, first-class list semantics, and fewer pitfalls than Bourne-compatible shells (particularly around filenames with spaces).

**License:** zlib (BSD-style)
**Language:** C (ANSI/C99), with Yacc/Bison for the parser grammar

## Build & Install

```sh
make                    # build rc (default: no line editing)
make EDIT=readline      # build with GNU readline support
make EDIT=editline      # build with BSD libedit
make EDIT=bestline      # build with bestline
make config.h           # generate config.h from config.def.h (auto on first build)
make install            # install to /usr/local (requires root)
make PREFIX=/your/dir install  # install to custom prefix
```

Line editing options: `null` (default/none), `edit`, `editline`, `readline`, `vrl`, `bestline`.

Platform-specific settings are in `config.h` (copied from `config.def.h` on first build). Edit `config.h` to adjust features before building.

## Testing

```sh
make check              # run all tests (trip + history)
make trip               # run shell regression tests (trip.rc)
make testhist           # run history command tests (test-history/)
make run-test-bestline  # run bestline editor unit tests (requires bestline lib)
```

- `trip.rc` is the main test suite. It runs as `./rc -p <trip.rc`.
- `test-history/` contains 12 history command test cases.
- CI runs on Ubuntu and macOS via GitHub Actions (`.github/workflows/ci.yml`).

## Architecture

```
Input → Lexer (lex.c) → Parser (parse.y) → Tree (tree.c)
  → Glom/Expand (glom.c, glob.c) → Execute (walk.c, exec.c)
  → Builtins (builtins.c) | Functions (fn.c) | External (execve.c)
```

**Key pipeline stages:**
1. **Input** (`input.c`, `edit-*.c`) — reads from files, strings, or line-editing libraries
2. **Lexer** (`lex.c`) — tokenizes input
3. **Parser** (`parse.y` → `parse.c`) — YACC grammar builds parse tree
4. **Expansion** (`glom.c`, `glob.c`) — variable/command substitution, globbing
5. **Execution** (`walk.c`, `exec.c`) — tree traversal, forking, pipeline setup

## Key Source Files

| File | Purpose |
|---|---|
| `main.c` | Entry point, CLI arg parsing, shell initialization |
| `lex.c` | Lexical analyzer / tokenizer |
| `parse.y` | YACC grammar (generates `parse.c` / `parse.h`) |
| `tree.c` | Parse tree node creation and manipulation |
| `glom.c` | Variable substitution, command substitution, redirection queuing |
| `glob.c` | Filename glob expansion |
| `walk.c` | Parse tree traversal and command dispatch |
| `exec.c` / `execve.c` | Process execution (fork/exec) |
| `builtins.c` | Built-in commands (cd, echo, eval, exit, etc.) |
| `fn.c` | Shell function storage and execution |
| `hash.c` | Hash table for variable/function lookup |
| `var.c` | Variable assignment and management |
| `signal.c` | Signal handling and masking |
| `except.c` | Exception handling (break, return, continue via longjmp) |
| `wait.c` | Process waiting and background job management |
| `redir.c` | File redirection management |
| `heredoc.c` | Here-document and here-string processing |
| `nalloc.c` | Arena-based memory allocator for command-lifetime objects |
| `input.c` | Input source management |
| `edit-*.c` | Line-editing backend variants (6 files, selected at build time) |
| `history.c` | Standalone history utility |
| `print.c` | Formatting and output routines |
| `match.c` | Pattern matching engine |
| `footobar.c` | Utility functions for parsing and output |

## Key Headers

| Header | Purpose |
|---|---|
| `rc.h` | Master header — all core types, macros, extern declarations |
| `proto.h` | Function prototypes |
| `config.h` | Platform-specific feature flags (generated from `config.def.h`) |
| `input.h` | Input handling types |
| `edit.h` | Line editor interface |
| `jbwrap.h` | setjmp/longjmp wrapper |
| `wait.h`, `stat.h`, `rlimit.h`, `getgroups.h` | System compatibility wrappers |

## Generated Files (do not edit directly)

- `config.h` — from `config.def.h`
- `parse.c` / `parse.h` — from `parse.y` via YACC
- `sigmsgs.c` / `sigmsgs.h` — signal tables from `mksignal`
- `statval.h` — status value constants from `mkstatval`
- `version.h` — git version string

## Code Conventions

- **Style:** K&R C with tabs for indentation
- **Naming:** functions are `lowercase_with_underscores`, types are `CamelCase` (e.g., `List`, `Node`, `Htab`), macros are `UPPERCASE`
- **Booleans:** enum with `TRUE`/`FALSE`
- **Memory:** arena allocator (`nalloc`/`nnew`/`nfree`) for per-command allocations; `ealloc`/`erealloc`/`efree` for long-lived objects
- **Error handling:** `rc_error()` with longjmp-based exception mechanism
- **Key macros:** `WALK(x, y)` for tail-recursive tree traversal, `mk(...)` for parse tree construction
- **Compiler flags:** `-Wall` is standard; builds must be clean of warnings

## Core Data Types (defined in `rc.h`)

```c
typedef struct List { char *w; char *m; List *n; }         // Linked list (word + metachar + next)
typedef struct Node { nodetype type; union { ... } u[4]; } // Parse tree node
typedef enum nodetype { nVar, nWord, nPipe, ... }          // Node types
typedef struct Variable { List *def; Variable *n; }        // Variable binding
typedef struct rc_Function { Node *def; char *extdef; }    // Function definition
```

## Platform Support

Tested on Linux, macOS, and BSD. CI matrix covers `ubuntu-latest` and `macos-latest`. Platform differences are handled via `config.h` feature flags (e.g., `HAVE_DEV_FD`, `HAVE_PROC_SELF_FD`, `HAVE_FIFO`, `HAVE_SYSV_SIGCLD`).
