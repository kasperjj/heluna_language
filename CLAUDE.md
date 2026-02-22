# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Heluna is a pure functional programming language for safe, composable JSON transformations. It is a computation kernel — not a general-purpose language. Functions take JSON in, produce JSON out, with no side effects. The language is designed to be written collaboratively by humans and LLMs.

Key design principles: contract-first development, everything is JSON, no null (uses `maybe`/`nothing`), no variable shadowing, data security via tags/sanitizers as language features.

## Build Commands

```bash
make              # Build all tools → bin/
make DEBUG=1      # Build with debug symbols (-g -O0 -DHELUNA_DEBUG)
make test         # Build and run all tests
make clean        # Remove build/ and bin/
```

The only implemented tool so far: `bin/heluna-lex test/samples/full-name.heluna`

## Architecture

The toolchain is written in C11 (`-Wall -Wextra -Wpedantic`) and builds a static library (`build/libheluna.a`) that CLI tools link against.

**Pipeline (planned):** source → lexer → parser → type checker → evaluator

### Core library (`src/` → `build/libheluna.a`)

- **arena.c** — Bump allocator (64KB blocks, 8-byte aligned). Never returns NULL; aborts on OOM. All allocations freed at once via `arena_destroy()`.
- **lexer.c** — Single-pass tokenizer. Produces a stream of `Token` structs with source location tracking. Supports `lexer_next()` and `lexer_peek()` for lookahead. Handles keywords (~48), literals, input references (`$field-name`), and `#` comments.
- **token.c** — Token kind enum (78 variants) and human-readable name lookup.
- **errors.c** — Structured error reporting with source location (filename:line:col). Error kinds: SYNTAX, TYPE, CONTRACT, RUNTIME, TAG, IO.

### Headers (`include/heluna/`)

Public API for the library. Each `.c` file has a corresponding header.

### CLI tools (`tools/`)

One `.c` file per tool, linked against `libheluna.a`. Only `heluna-lex` is implemented. The others (`heluna-parse`, `heluna-check`, `heluna-run`, `heluna-test`) are stubs.

### Tests (`test/`)

Test binaries are built to `build/test/` and run sequentially by `make test`. Currently only `test_lexer.c` exists (4 test functions covering keywords, literals, input refs, and operators). Sample `.heluna` files live in `test/samples/`.

## Language Reference

Read `language_introduction.md` for practical examples with syntax. Read `language_design.md` for design rationale. The formal grammar is in `syntax.txt` (EBNF).

Heluna uses English keywords over symbols: `through` not `|>`, `and`/`or`/`not` not `&&`/`||`/`!`, `match`...`when`...`then`...`end` not braces/arrows, explicit `result` not implicit returns.

## Conventions

- C11 standard, compiled with strict warnings
- Arena allocation for all dynamic memory (no manual malloc/free in library code)
- Tokens carry `SrcLoc` (filename, line, col) for error reporting
- Hyphens in identifiers (`full-name`, `bracket-age`) — not underscores — matching the language's English-prose style
- Input references use `$` prefix (`$first-name`)
