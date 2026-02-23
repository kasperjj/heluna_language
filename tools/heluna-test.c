/*
 * heluna-test: run embedded test cases from a Heluna contract.
 *
 * Usage: heluna-test <file.heluna>
 *
 * Parses the contract, runs each test case (given → eval → compare expect),
 * and prints pass/fail results.
 */

#include "heluna/evaluator.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/arena.h"
#include "heluna/errors.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-test: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-test: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-test <file.heluna>\n");
        return 1;
    }

    char *source = read_file(argv[1]);
    if (!source) return 1;

    Arena *arena = arena_create(64 * 1024);

    /* Lex */
    Lexer lex;
    lexer_init(&lex, source, argv[1], arena);

    /* Parse */
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);

    if (parser.had_error) {
        heluna_error_print(&parser.error);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Check */
    Checker checker;
    checker_init(&checker, prog, arena);
    int nerrs = checker_check(&checker);
    if (nerrs > 0) {
        for (int i = 0; i < checker.errors.count; i++) {
            heluna_error_print(&checker.errors.errors[i]);
        }
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Must be a function contract with tests */
    if (prog->contract->kind != CONTRACT_FUNCTION) {
        printf("no tests (not a function contract)\n");
        arena_destroy(arena);
        free(source);
        return 0;
    }

    if (!prog->contract->tests) {
        printf("no tests\n");
        arena_destroy(arena);
        free(source);
        return 0;
    }

    /* Run each test case */
    int total = 0;
    int passed = 0;

    for (const AstTestCase *tc = prog->contract->tests; tc; tc = tc->next) {
        total++;

        /* Evaluate the "given" record to get the input */
        Evaluator ev_given;
        evaluator_init(&ev_given, prog, arena);
        /* given is evaluated without input (it's a literal record) */
        ev_given.input_record = NULL;
        HVal *given_val = NULL;
        if (tc->given) {
            /* We need a mini eval for the given expr.
             * Given records may contain nothing, literals, lists, etc. */
            Evaluator gev;
            evaluator_init(&gev, prog, arena);
            gev.input_record = NULL;
            given_val = evaluator_eval(&gev, NULL);
            /* Actually, we need to eval the given expression directly.
             * Let's use eval_expr via evaluator_eval trick: set input to
             * empty and eval the given expr manually. We can't call
             * eval_expr directly since it's static. Instead, create a
             * temporary AstFunctionDef with the given expr as body. */
            (void)given_val;
        }

        /* Build a temporary program with given->body to eval the given expr */
        AstFunctionDef tmp_fn;
        tmp_fn.name = prog->function ? prog->function->name : "test";
        tmp_fn.body = tc->given;
        tmp_fn.loc = tc->loc;

        AstProgram tmp_prog;
        tmp_prog.contract = prog->contract;
        tmp_prog.function = &tmp_fn;
        tmp_prog.loc = prog->loc;

        Evaluator ev_g;
        evaluator_init(&ev_g, &tmp_prog, arena);
        given_val = evaluator_eval(&ev_g, NULL);

        if (ev_g.had_error) {
            printf("  FAIL: \"%s\" — error evaluating given: %s\n",
                   tc->name, ev_g.error.message);
            continue;
        }

        /* Evaluate the function with the given input */
        Evaluator ev;
        evaluator_init(&ev, prog, arena);
        HVal *result = evaluator_eval(&ev, given_val);

        if (ev.had_error) {
            printf("  FAIL: \"%s\" — runtime error: %s\n",
                   tc->name, ev.error.message);
            continue;
        }

        /* Evaluate the "expect" record */
        tmp_fn.body = tc->expect;
        Evaluator ev_e;
        evaluator_init(&ev_e, &tmp_prog, arena);
        HVal *expect_val = evaluator_eval(&ev_e, NULL);

        if (ev_e.had_error) {
            printf("  FAIL: \"%s\" — error evaluating expect: %s\n",
                   tc->name, ev_e.error.message);
            continue;
        }

        /* Compare */
        if (hval_equal(result, expect_val)) {
            printf("  PASS: \"%s\"\n", tc->name);
            passed++;
        } else {
            printf("  FAIL: \"%s\" — result does not match expected\n",
                   tc->name);
        }
    }

    printf("%d/%d passed\n", passed, total);

    arena_destroy(arena);
    free(source);
    return passed == total ? 0 : 1;
}
