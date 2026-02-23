/*
 * test_fmt_samples: verify the formatter is idempotent for every .heluna
 * sample file.
 *
 * For each sample in test/samples/<name>.heluna, this test:
 *   1. Parses the file and formats it to string A
 *   2. Re-parses string A and formats it to string B
 *   3. Asserts A == B (idempotency)
 *   4. Runs the checker on the re-parsed AST and asserts 0 errors
 *
 * Run from the project root (make test does this automatically).
 */

#include "heluna/formatter.h"
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

/* ── Format to a malloc'd string ────────────────────────── */

static char *format_to_string(const AstProgram *prog) {
    /* Write to a temp file and read it back */
    FILE *tmp = tmpfile();
    if (!tmp) return NULL;

    heluna_format(prog, tmp);
    long size = ftell(tmp);
    fseek(tmp, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(tmp); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, tmp);
    buf[n] = '\0';
    fclose(tmp);
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

    int ok = 1;

    /* ── Pass 1: parse original → format to string A ──── */
    Arena *arena1 = arena_create(64 * 1024);
    Lexer lex1;
    lexer_init(&lex1, source, heluna_path, arena1);
    Parser parser1;
    parser_init(&parser1, &lex1, arena1);
    AstProgram *prog1 = parser_parse(&parser1);

    if (parser1.had_error || !prog1) {
        fprintf(stderr, "  FAIL: %s — parse error on original\n", name);
        arena_destroy(arena1);
        free(source);
        tests_run++;
        return;
    }

    char *formatted_a = format_to_string(prog1);
    arena_destroy(arena1);
    free(source);

    if (!formatted_a) {
        fprintf(stderr, "  FAIL: %s — format_to_string returned NULL\n", name);
        tests_run++;
        return;
    }

    /* ── Pass 2: re-parse string A → format to string B ── */
    Arena *arena2 = arena_create(64 * 1024);
    Lexer lex2;
    lexer_init(&lex2, formatted_a, "<fmt>", arena2);
    Parser parser2;
    parser_init(&parser2, &lex2, arena2);
    AstProgram *prog2 = parser_parse(&parser2);

    if (parser2.had_error || !prog2) {
        fprintf(stderr, "  FAIL: %s — parse error on formatted output: %s\n",
                name, parser2.error.message);
        ok = 0;
    }

    char *formatted_b = NULL;
    if (ok) {
        formatted_b = format_to_string(prog2);
        if (!formatted_b) {
            fprintf(stderr, "  FAIL: %s — second format_to_string returned NULL\n", name);
            ok = 0;
        }
    }

    /* ── Check idempotency: A == B ──────────────────────── */
    if (ok && strcmp(formatted_a, formatted_b) != 0) {
        fprintf(stderr, "  FAIL: %s — formatter not idempotent\n", name);
        ok = 0;
    }

    /* ── Check semantic validity ────────────────────────── */
    if (ok) {
        Checker checker;
        checker_init(&checker, prog2, arena2);
        int nerrs = checker_check(&checker);
        if (nerrs > 0) {
            fprintf(stderr, "  FAIL: %s — checker found %d error(s) on formatted output\n",
                    name, nerrs);
            for (int i = 0; i < checker.errors.count; i++) {
                fprintf(stderr, "    %s\n", checker.errors.errors[i].message);
            }
            ok = 0;
        }
    }

    tests_run++;
    if (ok) tests_passed++;

    free(formatted_b);
    arena_destroy(arena2);
    free(formatted_a);
}

/* ── Sample list ────────────────────────────────────────── */

static const char *samples[] = {
    "boolean-logic",
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
    printf("test_fmt_samples:\n");

    for (const char **s = samples; *s; s++) {
        test_sample(*s);
    }

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
