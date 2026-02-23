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

Implemented tools: `bin/heluna-lex` (tokenizer), `bin/heluna-parse` (parser + AST printer), `bin/heluna-check` (static checker), and `bin/heluna-fmt` (source formatter).

## Architecture

The toolchain is written in C11 (`-Wall -Wextra -Wpedantic`) and builds a static library (`build/libheluna.a`) that CLI tools link against.

**Pipeline:** source → lexer → parser → checker → evaluator (planned)

### Core library (`src/` → `build/libheluna.a`)

- **arena.c** — Bump allocator (64KB blocks, 8-byte aligned). Never returns NULL; aborts on OOM. All allocations freed at once via `arena_destroy()`.
- **lexer.c** — Single-pass tokenizer. Produces a stream of `Token` structs with source location tracking. Supports `lexer_next()` and `lexer_peek()` for lookahead. Handles keywords (54), literals, input references (`$field-name`), and `#` comments.
- **token.c** — Token kind enum (85 variants) and human-readable name lookup.
- **parser.c** — Recursive-descent parser. Consumes token stream from the lexer and builds a typed AST (`AstProgram`). Handles full Heluna grammar: three contract kinds (function, tag, source), function definitions, expressions (including `lookup`), patterns, and types.
- **ast.c** — AST pretty-printer (`ast_print()`). Outputs S-expression representation of a parsed program.
- **checker.c** — Static analysis pass. Validates contract structure (duplicate fields/tags/sanitizers/tests), tag coherence (undeclared tags in annotations and rules), sanitizer coherence, rule field references, test case fields against schemas, scope (no shadowing, undefined identifiers), function calls (stdlib, `uses`, sanitizers), and lookup source references.
- **formatter.c** — Source code formatter (`heluna_format()`). Emits canonically-formatted Heluna source from an AST. Round-trips through parse→format are idempotent. Comments are not preserved (stripped during parsing).
- **errors.c** — Structured error reporting with source location (filename:line:col). Error kinds: SYNTAX, TYPE, CONTRACT, RUNTIME, TAG, IO.

### Headers (`include/heluna/`)

Public API for the library. Each `.c` file has a corresponding header.

### CLI tools (`tools/`)

One `.c` file per tool, linked against `libheluna.a`. `heluna-lex`, `heluna-parse`, `heluna-check`, and `heluna-fmt` are implemented. `heluna-run` and `heluna-test` are stubs.

### Tests (`test/`)

Test binaries are built to `build/test/` and run sequentially by `make test`. Test files:
- **test_lexer.c** — Unit tests (keywords, literals, input refs, operators).
- **test_lex_samples.c** — Golden-file integration tests: lexes every sample and compares against `test/expected/*.tokens`.
- **test_parser.c** — Unit tests for the parser (~40 test functions covering expressions, operators, precedence, patterns, types, contract sections, rules, and error cases).
- **test_parse_samples.c** — Integration tests: parses every `.heluna` sample and asserts no errors. Handles all three contract kinds (function, tag, source).
- **test_checker.c** — Unit tests for the checker (~160 tests covering scope, shadowing, tags, sanitizers, rules, test case field validation, and error cases).
- **test_check_samples.c** — Integration tests: parses and checks every `.heluna` sample with 0 errors.
- **test_fmt_samples.c** — Idempotency tests: for each sample, formats to string A, re-parses and formats to string B, asserts A == B, and checks semantic validity.

Sample `.heluna` files live in `test/samples/`.

## Language Reference

Read `language_introduction.md` for practical examples with syntax. Read `language_design.md` for design rationale. The formal grammar is in `syntax.txt` (EBNF).

Heluna uses English keywords over symbols: `through` not `|>`, `and`/`or`/`not` not `&&`/`||`/`!`, `match`...`when`...`then`...`end` not braces/arrows, explicit `result` not implicit returns.

## Conventions

- C11 standard, compiled with strict warnings
- Arena allocation for all dynamic memory (no manual malloc/free in library code)
- Tokens carry `SrcLoc` (filename, line, col) for error reporting
- Hyphens in identifiers (`full-name`, `bracket-age`) — not underscores — matching the language's English-prose style
- Input references use `$` prefix (`$first-name`)
