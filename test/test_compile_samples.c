/*
 * test_compile_samples: compile every .heluna sample file and verify
 * the resulting packet structure.
 *
 * For each sample in test/samples/<name>.heluna:
 *   - Function contracts: compile and verify packet (magic, sections, size)
 *   - Tag/source contracts: skip gracefully (compilation not supported)
 *
 * Run from the project root (make test does this automatically).
 */

#include "heluna/compiler.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

/* ── Helpers ─────────────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* ── Stdlib name check ──────────────────────────────────── */

static const char *stdlib_names[] = {
    "upper","lower","trim","trim-start","trim-end","substring","replace",
    "split","join","starts-with","ends-with","contains","length",
    "pad-left","pad-right","regex-match","regex-replace",
    "abs","ceil","floor","round","min","max","clamp",
    "sort","sort-by","reverse","unique","flatten","zip","range","slice",
    "keys","values","merge","pick","omit",
    "parse-date","format-date","date-diff","date-add","now-date",
    "base64-encode","base64-decode","url-encode","url-decode",
    "json-encode","json-parse","sha256","hmac-sha256","uuid",
    NULL
};

static int is_stdlib_name(const char *name) {
    for (const char **s = stdlib_names; *s; s++) {
        if (strcmp(*s, name) == 0) return 1;
    }
    return 0;
}

/* ── Dependency resolution ──────────────────────────────── */

/* Track source strings for later free */
static char *dep_sources[64];
static int   dep_source_count = 0;

static int resolve_deps(Arena *arena, const AstProgram *prog,
                        CompilerDep *deps, int *dep_count, int max_deps) {
    const AstContract *ct = prog->contract;

    /* Collect dep names from uses declarations */
    for (int ui = 0; ui < ct->uses_count; ui++) {
        const char *uname = ct->uses[ui];
        /* Skip if already resolved */
        int found = 0;
        for (int i = 0; i < *dep_count; i++) {
            if (strcmp(deps[i].name, uname) == 0) { found = 1; break; }
        }
        if (found || *dep_count >= max_deps) continue;

        /* Parse dep file */
        char dep_path[256];
        snprintf(dep_path, sizeof dep_path, "test/samples/%s.heluna", uname);
        char *src = read_file(dep_path);
        if (!src) continue;
        dep_sources[dep_source_count++] = src;

        Lexer lex;
        lexer_init(&lex, src, dep_path, arena);
        Parser parser;
        parser_init(&parser, &lex, arena);
        AstProgram *dep_prog = parser_parse(&parser);
        if (parser.had_error) continue;

        Checker checker;
        checker_init(&checker, dep_prog, arena);
        if (checker_check(&checker) > 0) continue;

        deps[*dep_count].name = uname;
        deps[*dep_count].prog = dep_prog;
        (*dep_count)++;

        /* Recursively resolve transitive deps */
        resolve_deps(arena, dep_prog, deps, dep_count, max_deps);
    }

    /* Collect from sanitizer impl_name (non-stdlib, non-already-resolved) */
    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
        if (!s->impl_name || is_stdlib_name(s->impl_name)) continue;

        int found = 0;
        for (int i = 0; i < *dep_count; i++) {
            if (strcmp(deps[i].name, s->impl_name) == 0) { found = 1; break; }
        }
        if (found || *dep_count >= max_deps) continue;

        char dep_path[256];
        snprintf(dep_path, sizeof dep_path, "test/samples/%s.heluna", s->impl_name);
        char *src = read_file(dep_path);
        if (!src) continue;
        dep_sources[dep_source_count++] = src;

        Lexer lex;
        lexer_init(&lex, src, dep_path, arena);
        Parser parser;
        parser_init(&parser, &lex, arena);
        AstProgram *dep_prog = parser_parse(&parser);
        if (parser.had_error) continue;

        Checker checker;
        checker_init(&checker, dep_prog, arena);
        if (checker_check(&checker) > 0) continue;

        deps[*dep_count].name = s->impl_name;
        deps[*dep_count].prog = dep_prog;
        (*dep_count)++;

        resolve_deps(arena, dep_prog, deps, dep_count, max_deps);
    }

    return 0;
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ── Test one sample ─────────────────────────────────────── */

static void test_sample(const char *name) {
    char path[256];
    snprintf(path, sizeof path, "test/samples/%s.heluna", name);

    char *source = read_file(path);
    if (!source) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — cannot open %s\n", name, path);
        return;
    }

    Arena *arena = arena_create(64 * 1024);
    Lexer lex;
    lexer_init(&lex, source, path, arena);
    Parser parser;
    parser_init(&parser, &lex, arena);

    AstProgram *prog = parser_parse(&parser);
    if (parser.had_error) {
        fprintf(stderr, "  FAIL: %s — parse error: %s\n",
                name, parser.error.message);
        tests_run++;
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Check */
    Checker checker;
    checker_init(&checker, prog, arena);
    int nerrs = checker_check(&checker);
    if (nerrs > 0) {
        fprintf(stderr, "  FAIL: %s — %d checker error%s\n",
                name, nerrs, nerrs == 1 ? "" : "s");
        tests_run++;
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Skip non-function contracts */
    if (prog->contract->kind != CONTRACT_FUNCTION) {
        tests_run++;
        tests_passed++;
        /* Skipped gracefully */
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Resolve dependencies */
    CompilerDep deps[32];
    int dep_count = 0;
    dep_source_count = 0;
    resolve_deps(arena, prog, deps, &dep_count, 32);

    /* Compile */
    Compiler compiler;
    if (dep_count > 0) {
        compiler_init_with_deps(&compiler, prog, arena, deps, dep_count);
    } else {
        compiler_init(&compiler, prog, arena);
    }
    PacketResult packet = compiler_compile(&compiler);

    tests_run++;

    if (compiler.errors.count > 0) {
        fprintf(stderr, "  FAIL: %s — %d compile error%s:\n",
                name, compiler.errors.count,
                compiler.errors.count == 1 ? "" : "s");
        for (int i = 0; i < compiler.errors.count; i++) {
            fprintf(stderr, "    ");
            heluna_error_print(&compiler.errors.errors[i]);
        }
        for (int i = 0; i < dep_source_count; i++) free(dep_sources[i]);
        arena_destroy(arena);
        free(source);
        return;
    }

    if (!packet.data || packet.size == 0) {
        fprintf(stderr, "  FAIL: %s — empty packet\n", name);
        for (int i = 0; i < dep_source_count; i++) free(dep_sources[i]);
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Verify packet structure */
    int ok = 1;

    /* Magic */
    if (packet.size < 88) {
        fprintf(stderr, "  FAIL: %s — packet too small (%zu bytes)\n",
                name, packet.size);
        ok = 0;
    } else {
        uint32_t magic = read_u32(packet.data);
        if (magic != PACKET_MAGIC) {
            fprintf(stderr, "  FAIL: %s — wrong magic 0x%08X\n",
                    name, magic);
            ok = 0;
        }

        uint32_t total = read_u32(packet.data + 8);
        if (total != (uint32_t)packet.size) {
            fprintf(stderr, "  FAIL: %s — total_size %u != packet size %zu\n",
                    name, total, packet.size);
            ok = 0;
        }

        uint16_t sec_count = read_u16(packet.data + 12);
        if (sec_count < 4) {
            fprintf(stderr, "  FAIL: %s — only %d sections (need 4)\n",
                    name, sec_count);
            ok = 0;
        }

        /* Verify all 4 required sections */
        int has[4] = {0, 0, 0, 0};
        for (int i = 0; i < sec_count && i < 16; i++) {
            int doff = 88 + i * 10;
            if (doff + 10 > (int)packet.size) break;
            uint16_t stype = read_u16(packet.data + doff);
            if (stype == SEC_CONTRACT)    has[0] = 1;
            if (stype == SEC_CONSTANTS)   has[1] = 1;
            if (stype == SEC_STDLIB_DEPS) has[2] = 1;
            if (stype == SEC_BYTECODE)    has[3] = 1;
        }
        if (!has[0]) { fprintf(stderr, "  FAIL: %s — missing CONTRACT\n", name); ok = 0; }
        if (!has[1]) { fprintf(stderr, "  FAIL: %s — missing CONSTANTS\n", name); ok = 0; }
        if (!has[2]) { fprintf(stderr, "  FAIL: %s — missing STDLIB_DEPS\n", name); ok = 0; }
        if (!has[3]) { fprintf(stderr, "  FAIL: %s — missing BYTECODE\n", name); ok = 0; }
    }

    if (ok) {
        tests_passed++;
    }

    for (int i = 0; i < dep_source_count; i++) free(dep_sources[i]);
    arena_destroy(arena);
    free(source);
}

/* ── Sample list ─────────────────────────────────────────── */

static const char *samples[] = {
    "boolean-logic",
    "bracket-age",
    "comments",
    "company-security",
    "complex-types",
    "customers-source",
    "create-order",
    "describe-value",
    "empty-and-nothing",
    "enrich-order",
    "float-arithmetic",
    "forbid-field-rule",
    "format-names",
    "full-name",
    "integer-arithmetic",
    "list-pipeline",
    "match-list-pattern",
    "match-literals",
    "match-record-pattern",
    "match-rule",
    "minimal-contract",
    "multiline-records",
    "nested-conditional",
    "nested-lists",
    "normalize-email",
    "operator-precedence",
    "patient-summary",
    "process-scores",
    "rectangle-area",
    "string-escapes",
    "string-operations",
    "tag-propagation",
    "ticket-price",
    "validate-user",
    NULL,
};

int main(void) {
    printf("test_compile_samples:\n");

    for (const char **s = samples; *s; s++) {
        test_sample(*s);
    }

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
