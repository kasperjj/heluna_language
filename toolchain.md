# Heluna Toolchain

Command-line tools for the [Heluna](docs/introduction-to-heluna.md) programming language — a pure functional language for safe, composable JSON transformations.

## Tools

| Tool | Purpose |
|------|---------|
| `heluna-lex` | Tokenize a source file and print each token |
| `heluna-parse` | Parse a source file and dump the AST |
| `heluna-check` | Validate a contract and function without running |
| `heluna-fmt` | Format a source file to canonical style |
| `heluna-compile` | Compile a function contract to a VM packet (`.hlna`) |
| `heluna-run` | Execute a function with JSON input |
| `heluna-test` | Run embedded test cases from a contract |

## Building

```
make
```

Produces binaries in `bin/`. Requires a C11 compiler (gcc, clang, etc.).

For debug builds with symbols and no optimization:

```
make DEBUG=1
```

## Running Tests

```
make test
```

## Trying It Out

```
bin/heluna-lex test/samples/full-name.heluna
bin/heluna-compile test/samples/full-name.heluna -o full-name.hlna
```

## Project Structure

```
include/heluna/   Public headers for the shared library
include/vendor/   Vendored third-party code
src/              Library implementation (builds into libheluna.a)
tools/            One .c file per command-line tool
test/             Tests and sample .heluna files
docs/             Documentation
licenses/         Third-party license notices
```

## License

MIT. See [LICENSE](LICENSE) for details.
