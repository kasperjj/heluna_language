/*
 * test_eval_samples: run embedded test cases from every function-contract
 * sample and verify they pass.
 *
 * Samples that require uses/sanitizers/lookup (not yet implemented) are
 * expected to fail and are skipped.
 */

#include "heluna/evaluator.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
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
    if (nerrs > 0) {
        fprintf(stderr, "  FAIL: %s — %d checker errors\n", name, nerrs);
        tests_run++;
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Skip non-function contracts */
    if (prog->contract->kind != CONTRACT_FUNCTION) {
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Skip samples with no tests */
    if (!prog->contract->tests) {
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Run each test case */
    int sample_total = 0;
    int sample_passed = 0;

    for (const AstTestCase *tc = prog->contract->tests; tc; tc = tc->next) {
        sample_total++;
        tests_run++;

        /* Eval given */
        AstFunctionDef tmp_fn = {
            .name = prog->function->name,
            .body = tc->given,
            .loc = tc->loc
        };
        AstProgram tmp_prog = {
            .contract = prog->contract,
            .function = &tmp_fn,
            .loc = prog->loc
        };

        Evaluator ev_g;
        evaluator_init(&ev_g, &tmp_prog, arena);
        HVal *input = evaluator_eval(&ev_g, NULL);
        if (ev_g.had_error) {
            fprintf(stderr, "  FAIL: %s / \"%s\" — given eval: %s\n",
                    name, tc->name, ev_g.error.message);
            continue;
        }

        /* Eval function */
        Evaluator ev;
        evaluator_init(&ev, prog, arena);
        HVal *result = evaluator_eval(&ev, input);
        if (ev.had_error) {
            fprintf(stderr, "  FAIL: %s / \"%s\" — runtime: %s\n",
                    name, tc->name, ev.error.message);
            continue;
        }

        /* Eval expect */
        tmp_fn.body = tc->expect;
        Evaluator ev_e;
        evaluator_init(&ev_e, &tmp_prog, arena);
        HVal *expect = evaluator_eval(&ev_e, NULL);
        if (ev_e.had_error) {
            fprintf(stderr, "  FAIL: %s / \"%s\" — expect eval: %s\n",
                    name, tc->name, ev_e.error.message);
            continue;
        }

        if (hval_equal(result, expect)) {
            sample_passed++;
            tests_passed++;
        } else {
            fprintf(stderr, "  FAIL: %s / \"%s\" — result mismatch\n",
                    name, tc->name);
        }
    }

    if (sample_passed == sample_total && sample_total > 0) {
        /* All passed — no output (quiet success) */
    }

    arena_destroy(arena);
    free(source);
}

/* ── Sample list (function contracts with tests, no uses/sanitizers) ── */

static const char *samples[] = {
    "boolean-logic",
    "bracket-age",
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
    "process-scores",
    "rectangle-area",
    "string-escapes",
    "string-operations",
    "ticket-price",
    NULL,
};

int main(void) {
    printf("test_eval_samples:\n");

    for (const char **s = samples; *s; s++) {
        test_sample(*s);
    }

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
