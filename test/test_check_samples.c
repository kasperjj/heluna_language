/*
 * test_check_samples: verify the checker accepts every .heluna sample file.
 *
 * For each sample in test/samples/<name>.heluna, this test:
 *   1. Reads, lexes, and parses the file
 *   2. Runs the checker
 *   3. Asserts 0 checker errors
 *
 * Run from the project root (make test does this automatically).
 */

#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

/* ── File I/O helper ────────────────────────────────────── */

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

/* ── Test one sample ────────────────────────────────────── */

static void test_sample(const char *name) {
    char heluna_path[256];
    snprintf(heluna_path, sizeof heluna_path, "test/samples/%s.heluna", name);

    char *source = read_file(heluna_path);
    if (!source) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — cannot open %s\n", name, heluna_path);
        return;
    }

    Arena *arena = arena_create(64 * 1024);
    Lexer lex;
    lexer_init(&lex, source, heluna_path, arena);
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

    Checker checker;
    checker_init(&checker, prog, arena);
    int nerrs = checker_check(&checker);

    tests_run++;
    if (nerrs == 0) {
        tests_passed++;
    } else {
        fprintf(stderr, "  FAIL: %s — %d checker error%s:\n",
                name, nerrs, nerrs == 1 ? "" : "s");
        for (int i = 0; i < checker.errors.count; i++) {
            fprintf(stderr, "    ");
            heluna_error_print(&checker.errors.errors[i]);
        }
    }

    arena_destroy(arena);
    free(source);
}

/* ── Sample list ────────────────────────────────────────── */

static const char *samples[] = {
    "boolean-logic",
    "comments",
    "complex-types",
    "create-order",
    "describe-value",
    "empty-and-nothing",
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
    printf("test_check_samples:\n");

    for (const char **s = samples; *s; s++) {
        test_sample(*s);
    }

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
