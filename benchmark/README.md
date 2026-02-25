# Heluna Cross-VM Benchmark Suite

Portable benchmark suite for comparing Heluna VM implementations across languages (Java, Rust, Go, etc.). All benchmark artifacts — compiled packets, data generator, and specification — live here so any VM can use them.

## What It Measures

Four benchmark programs exercise different aspects of VM performance:

| Benchmark | Focus |
|-----------|-------|
| `bench-arithmetic` | filter, map, fold, arithmetic, type conversion, comparisons |
| `bench-strings` | trim, lower, upper, replace, join, concat |
| `bench-lists` | sort, sort-by, unique, reverse, slice, range, filter |
| `bench-mixed` | Realistic pipeline combining all of the above + sha256 |

Each benchmark uses the same input contract: a `records` list of employee-like records with string, integer, float, and boolean fields.

## Generating Test Data

The data generator uses a fixed seed (42) for deterministic output. Every run produces identical files.

```bash
cd benchmark/data
python3 generate.py
```

This creates four files:
- `tiny.json` — 1 record (latency testing)
- `small.json` — 100 records
- `medium.json` — 10,000 records (throughput testing)
- `large.json` — 100,000 records (stress testing)

Generated files are git-ignored. You must run the generator before benchmarking.

## Running with the Java VM

```bash
# From the heluna_jvm directory
mvn compile

# Run all benchmarks
java -cp target/classes io.heluna.vm.BenchmarkRunner \
  --spec ../heluna_language/benchmark/benchmark-spec.json \
  --benchmark-dir ../heluna_language/benchmark/

# Run a subset
java -cp target/classes io.heluna.vm.BenchmarkRunner \
  --spec ../heluna_language/benchmark/benchmark-spec.json \
  --benchmark-dir ../heluna_language/benchmark/ \
  --filter arithmetic
```

Progress is printed to stderr. JSON results are printed to stdout, so you can redirect:

```bash
java -cp target/classes io.heluna.vm.BenchmarkRunner \
  --spec ../heluna_language/benchmark/benchmark-spec.json \
  --benchmark-dir ../heluna_language/benchmark/ \
  > results-java.json
```

## Implementing a Runner for a New VM

The benchmark protocol:

1. Read `benchmark-spec.json` to get the benchmark matrix
2. For each benchmark entry:
   - Load the `.hlna` packet once
   - Load and parse the JSON data once (exclude parse time from measurement)
   - Run `warmup` iterations (discard results)
   - Run `iterations` measured iterations, timing each one
   - Compute SHA-256 of the JSON-serialized output from the first iteration
3. Output results as JSON to stdout

## Output Format

```json
{
  "vm": "java",
  "java_version": "11.0.x",
  "timestamp": "2026-02-24T...",
  "results": [
    {
      "name": "arithmetic-medium",
      "iterations": 1000,
      "total_ms": 5432.1,
      "mean_ms": 5.43,
      "median_ms": 5.12,
      "p99_ms": 12.34,
      "min_ms": 4.01,
      "max_ms": 15.67,
      "output_sha256": "abc123..."
    }
  ]
}
```

## Comparing Results Across VMs

1. Run the same `benchmark-spec.json` with each VM's runner
2. Verify `output_sha256` matches across VMs for each benchmark — this confirms correctness
3. Compare `mean_ms`, `median_ms`, `p99_ms` for performance

The `output_sha256` acts as a correctness check: if two VMs produce different hashes for the same benchmark + data, one of them has a bug.

## Benchmark Specification

The `benchmark-spec.json` declares:
- `packet`: path to compiled `.hlna` file (relative to benchmark dir)
- `data`: path to input JSON file (relative to benchmark dir)
- `warmup`: number of warmup iterations
- `iterations`: number of measured iterations
- `mode`: `"latency"` (tiny data, many iterations) or `"throughput"` (large data, fewer iterations)

## File Layout

```
benchmark/
  README.md                     # This file
  benchmark-spec.json           # Benchmark matrix
  heluna/                       # Source programs
    bench-arithmetic.heluna
    bench-strings.heluna
    bench-lists.heluna
    bench-mixed.heluna
  packets/                      # Compiled packets (checked in)
    bench-arithmetic.hlna
    bench-strings.hlna
    bench-lists.hlna
    bench-mixed.hlna
  data/                         # Generated data (git-ignored)
    generate.py                 # Deterministic data generator
    .gitignore
```
