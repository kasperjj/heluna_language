/*
 * bench_pipeline.c — Benchmark harness for the Heluna toolchain.
 *
 * Measures 6 pipeline stages (lex, parse, check, compile, eval, vm)
 * independently for each fixture file.
 *
 * Usage:
 *   build/bench/bench_pipeline [-n COUNT] [-q]
 */

#include "heluna/arena.h"
#include "heluna/lexer.h"
#include "heluna/parser.h"
#include "heluna/checker.h"
#include "heluna/compiler.h"
#include "heluna/evaluator.h"
#include "heluna/vm.h"
#include "heluna/json.h"
#include "heluna/errors.h"
#include "heluna/version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Portable high-resolution timer ──────────────────────── */

#ifdef __APPLE__
#include <mach/mach_time.h>
static uint64_t now_ns(void) {
    static mach_timebase_info_data_t info;
    if (info.denom == 0) mach_timebase_info(&info);
    uint64_t t = mach_absolute_time();
    return t * info.numer / info.denom;
}
#else
#include <time.h>
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif

/* ── File reading ────────────────────────────────────────── */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bench: cannot open '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); fprintf(stderr, "bench: OOM\n"); exit(1); }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    if (out_len) *out_len = nread;
    return buf;
}

/* ── Fixture descriptor ──────────────────────────────────── */

typedef struct {
    const char *name;
    const char *heluna_path;
    const char *json_path;
    char       *source;
    size_t      source_len;
    char       *json_str;
} Fixture;

/* ── Stage benchmarks ────────────────────────────────────── */

static double bench_lex(const Fixture *fix, int iterations) {
    uint64_t start = now_ns();
    for (int i = 0; i < iterations; i++) {
        Arena *a = arena_create(64 * 1024);
        Lexer lex;
        lexer_init(&lex, fix->source, fix->name, a);
        Token tok;
        do { tok = lexer_next(&lex); } while (tok.kind != TOK_EOF);
        arena_destroy(a);
    }
    return (double)(now_ns() - start) / 1e9;
}

static double bench_parse(const Fixture *fix, int iterations) {
    uint64_t start = now_ns();
    for (int i = 0; i < iterations; i++) {
        Arena *a = arena_create(64 * 1024);
        Lexer lex;
        lexer_init(&lex, fix->source, fix->name, a);
        Parser parser;
        parser_init(&parser, &lex, a);
        parser_parse(&parser);
        arena_destroy(a);
    }
    return (double)(now_ns() - start) / 1e9;
}

static double bench_check(const Fixture *fix, int iterations) {
    uint64_t start = now_ns();
    for (int i = 0; i < iterations; i++) {
        Arena *a = arena_create(64 * 1024);
        Lexer lex;
        lexer_init(&lex, fix->source, fix->name, a);
        Parser parser;
        parser_init(&parser, &lex, a);
        AstProgram *prog = parser_parse(&parser);
        Checker checker;
        checker_init(&checker, prog, a);
        checker_check(&checker);
        arena_destroy(a);
    }
    return (double)(now_ns() - start) / 1e9;
}

static double bench_compile(const Fixture *fix, int iterations) {
    uint64_t start = now_ns();
    for (int i = 0; i < iterations; i++) {
        Arena *a = arena_create(64 * 1024);
        Lexer lex;
        lexer_init(&lex, fix->source, fix->name, a);
        Parser parser;
        parser_init(&parser, &lex, a);
        AstProgram *prog = parser_parse(&parser);
        Checker checker;
        checker_init(&checker, prog, a);
        checker_check(&checker);
        Compiler compiler;
        compiler_init(&compiler, prog, a);
        compiler_compile(&compiler);
        arena_destroy(a);
    }
    return (double)(now_ns() - start) / 1e9;
}

static double bench_eval(const Fixture *fix, int iterations) {
    uint64_t start = now_ns();
    for (int i = 0; i < iterations; i++) {
        Arena *a = arena_create(64 * 1024);
        Lexer lex;
        lexer_init(&lex, fix->source, fix->name, a);
        Parser parser;
        parser_init(&parser, &lex, a);
        AstProgram *prog = parser_parse(&parser);
        HelunaError jerr = {0};
        HVal *input = json_parse(a, fix->json_str, &jerr);
        Evaluator ev;
        evaluator_init(&ev, prog, a);
        evaluator_eval(&ev, input);
        arena_destroy(a);
    }
    return (double)(now_ns() - start) / 1e9;
}

static double bench_vm(const Fixture *fix, int iterations) {
    /* Pre-compile the packet once (not part of measurement) */
    Arena *setup = arena_create(64 * 1024);
    Lexer lex;
    lexer_init(&lex, fix->source, fix->name, setup);
    Parser parser;
    parser_init(&parser, &lex, setup);
    AstProgram *prog = parser_parse(&parser);
    Checker checker;
    checker_init(&checker, prog, setup);
    checker_check(&checker);
    Compiler compiler;
    compiler_init(&compiler, prog, setup);
    PacketResult pkt = compiler_compile(&compiler);
    /* Copy packet data so we can free the setup arena */
    size_t pkt_size = pkt.size;
    uint8_t *pkt_data = malloc(pkt_size);
    memcpy(pkt_data, pkt.data, pkt_size);
    arena_destroy(setup);

    uint64_t start = now_ns();
    for (int i = 0; i < iterations; i++) {
        Arena *a = arena_create(64 * 1024);
        HelunaError load_err = {0};
        VmPacket *packet = vm_load_packet(pkt_data, pkt_size, a, &load_err);
        HelunaError jerr = {0};
        HVal *input = json_parse(a, fix->json_str, &jerr);
        Vm vm;
        vm_init(&vm, packet, a);
        vm_execute(&vm, input);
        arena_destroy(a);
    }
    double elapsed = (double)(now_ns() - start) / 1e9;
    free(pkt_data);
    return elapsed;
}

/* ── Main ────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    int iterations = 5000;
    int quiet = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("heluna-bench %s\n", HELUNA_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
            if (iterations <= 0) iterations = 5000;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        }
    }

    Fixture fixtures[] = {
        { "simple.heluna",  "bench/fixtures/simple.heluna",
                            "bench/fixtures/simple.json",
                            NULL, 0, NULL },
        { "complex.heluna", "bench/fixtures/complex.heluna",
                            "bench/fixtures/complex.json",
                            NULL, 0, NULL },
    };
    int nfix = (int)(sizeof(fixtures) / sizeof(fixtures[0]));

    /* Load fixture files */
    for (int i = 0; i < nfix; i++) {
        fixtures[i].source = read_file(fixtures[i].heluna_path,
                                       &fixtures[i].source_len);
        fixtures[i].json_str = read_file(fixtures[i].json_path, NULL);
    }

    if (!quiet) {
        printf("heluna bench v%s — %d iterations\n\n", HELUNA_VERSION, iterations);
    }

    static const char *stage_names[] = {
        "lex", "parse", "check", "compile", "eval", "vm"
    };

    for (int i = 0; i < nfix; i++) {
        Fixture *fix = &fixtures[i];

        double times[6];
        times[0] = bench_lex(fix, iterations);
        times[1] = bench_parse(fix, iterations);
        times[2] = bench_check(fix, iterations);
        times[3] = bench_compile(fix, iterations);
        times[4] = bench_eval(fix, iterations);
        times[5] = bench_vm(fix, iterations);

        if (quiet) {
            /* Machine-readable: fixture stage elapsed ops/sec */
            for (int s = 0; s < 6; s++) {
                int ops = (times[s] > 0.0)
                    ? (int)((double)iterations / times[s])
                    : 0;
                printf("%s %s %.3f %d\n",
                       fix->name, stage_names[s], times[s], ops);
            }
        } else {
            printf("%s (%zu bytes):\n", fix->name, fix->source_len);
            for (int s = 0; s < 6; s++) {
                int ops = (times[s] > 0.0)
                    ? (int)((double)iterations / times[s])
                    : 0;
                printf("  %-12s%.3fs  %8d ops/sec\n",
                       stage_names[s], times[s], ops);
            }
            printf("\n");
        }
    }

    /* Cleanup */
    for (int i = 0; i < nfix; i++) {
        free(fixtures[i].source);
        free(fixtures[i].json_str);
    }

    return 0;
}
