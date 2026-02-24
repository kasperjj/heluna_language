# Performance Benchmarks

Benchmark results for the Heluna toolchain, measured with `make bench`.

The harness runs each pipeline stage independently for 5000 iterations and reports wall-clock time and throughput (ops/sec). "ops" means full stage invocations (lex an entire file, parse an entire file, etc.).

## How to reproduce

```bash
make clean && make bench          # default 5000 iterations
make bench BENCH_ARGS="-n 10000"  # custom iteration count
build/bench/bench_pipeline -q     # machine-readable output
```

## v0.1.0

**Machine:** Apple Silicon (arm64), macOS, `-O2` build

### simple.heluna (768 bytes)

| Stage   | Time (s) | Ops/sec  |
|---------|----------|----------|
| lex     | 0.070    | 71,926   |
| parse   | 0.046    | 109,470  |
| check   | 0.043    | 116,168  |
| compile | 0.047    | 107,003  |
| eval    | 0.044    | 113,493  |
| vm      | 0.006    | 774,648  |

### complex.heluna (3894 bytes)

| Stage   | Time (s) | Ops/sec  |
|---------|----------|----------|
| lex     | 0.169    | 29,513   |
| parse   | 0.193    | 25,941   |
| check   | 0.204    | 24,457   |
| compile | 0.220    | 22,708   |
| eval    | 0.207    | 24,191   |
| vm      | 0.018    | 277,458  |

## Notes

- **lex**: Tokenizes the full source file into a token stream.
- **parse**: Lexes + builds the AST via recursive descent.
- **check**: Lex + parse + static analysis (scope, tags, sanitizers, rules).
- **compile**: Full frontend (lex/parse/check) + bytecode emission.
- **eval**: Lex + parse + tree-walking evaluation with JSON input.
- **vm**: Loads a pre-compiled packet + executes bytecode with JSON input. Compilation is not included in the measurement, only load + execute.

The VM stage is significantly faster than the other stages because it skips the entire frontend (lex/parse/check/compile) and operates on pre-compiled bytecode.
