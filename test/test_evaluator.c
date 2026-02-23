/*
 * test_evaluator: unit tests for the Heluna evaluator.
 *
 * Each test creates an inline source string, parses it, then evaluates
 * and asserts on the result.
 */

#include "heluna/evaluator.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        tests_passed++; \
    } \
} while (0)

/* Parse, check, and evaluate a program with a given input record.
 * The input record is built from the test case's "given" expression. */
#define SETUP(src) \
    Arena *arena = arena_create(64 * 1024); \
    Lexer lex; \
    lexer_init(&lex, src, "test", arena); \
    Parser parser; \
    parser_init(&parser, &lex, arena); \
    AstProgram *prog = parser_parse(&parser); \
    ASSERT(!parser.had_error, "parse ok"); \
    ASSERT(prog != NULL, "prog not null"); \
    Checker checker; \
    if (prog) { \
        checker_init(&checker, prog, arena); \
        checker_check(&checker); \
    }

#define TEARDOWN() arena_destroy(arena)

/* Evaluate function with a given record (built from test case) */
static HVal *run_test_case(Arena *arena, AstProgram *prog,
                           const AstTestCase *tc) {
    /* Eval the given expression to get input */
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
    if (ev_g.had_error) return NULL;

    /* Eval the function with the input */
    Evaluator ev;
    evaluator_init(&ev, prog, arena);
    return evaluator_eval(&ev, input);
}

static HVal *eval_expect(Arena *arena, AstProgram *prog,
                         const AstTestCase *tc) {
    AstFunctionDef tmp_fn = {
        .name = prog->function->name,
        .body = tc->expect,
        .loc = tc->loc
    };
    AstProgram tmp_prog = {
        .contract = prog->contract,
        .function = &tmp_fn,
        .loc = prog->loc
    };
    Evaluator ev;
    evaluator_init(&ev, &tmp_prog, arena);
    return evaluator_eval(&ev, NULL);
}

/* ── Tests ────────────────────────────────────────────────── */

static void test_integer_literal(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 1 } expect { y: 42 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: 42 }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result != NULL, "integer: result not null");
    ASSERT(result->kind == VAL_RECORD, "integer: result is record");
    HVal *y = NULL;
    for (HField *f = result->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, "y") == 0) y = f->value;
    }
    ASSERT(y != NULL && y->kind == VAL_INTEGER, "integer: y is integer");
    ASSERT(y != NULL && y->as.integer_val == 42, "integer: y = 42");
    TEARDOWN();
}

static void test_string_literal(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  tests\n"
        "    test \"t\" given { x: 1 } expect { y: \"hello\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: \"hello\" }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result != NULL, "string: result not null");
    HVal *y = NULL;
    for (HField *f = result->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, "y") == 0) y = f->value;
    }
    ASSERT(y != NULL && y->kind == VAL_STRING, "string: y is string");
    ASSERT(y != NULL && strcmp(y->as.string_val, "hello") == 0,
           "string: y = \"hello\"");
    TEARDOWN();
}

static void test_boolean_true(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as boolean end\n"
        "  tests\n"
        "    test \"t\" given { x: 1 } expect { y: true } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: true }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result != NULL, "bool true: not null");
    HVal *y = NULL;
    for (HField *f = result->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, "y") == 0) y = f->value;
    }
    ASSERT(y != NULL && y->kind == VAL_BOOLEAN && y->as.boolean_val == 1,
           "bool true: y = true");
    TEARDOWN();
}

static void test_nothing_literal(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as maybe integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 1 } expect { y: nothing } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: nothing }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result != NULL, "nothing: not null");
    HVal *y = NULL;
    for (HField *f = result->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, "y") == 0) y = f->value;
    }
    ASSERT(y != NULL && y->kind == VAL_NOTHING, "nothing: y = nothing");
    TEARDOWN();
}

static void test_binary_int_arithmetic(void) {
    SETUP(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output sum as integer, diff as integer, prod as integer end\n"
        "  tests\n"
        "    test \"t\" given { a: 17, b: 5 }\n"
        "      expect { sum: 22, diff: 12, prod: 85 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { sum: $a + $b, diff: $a - $b, prod: $a * $b }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "int arith: not null");
    ASSERT(hval_equal(result, expect), "int arith: result matches expect");
    TEARDOWN();
}

static void test_float_arithmetic(void) {
    SETUP(
        "contract t\n"
        "  input x as float, y as float end\n"
        "  output sum as float, prod as float end\n"
        "  tests\n"
        "    test \"t\" given { x: 3.0, y: 2.0 }\n"
        "      expect { sum: 5.0, prod: 6.0 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { sum: $x + $y, prod: $x * $y }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "float arith: not null");
    ASSERT(hval_equal(result, expect), "float arith: matches");
    TEARDOWN();
}

static void test_mixed_arithmetic(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as float end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 } expect { y: 7.5 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x + 2.5 }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "mixed arith: not null");
    ASSERT(hval_equal(result, expect), "mixed arith: matches");
    TEARDOWN();
}

static void test_string_concatenation(void) {
    SETUP(
        "contract t\n"
        "  input a as string, b as string end\n"
        "  output c as string end\n"
        "  tests\n"
        "    test \"t\" given { a: \"hello\", b: \"world\" }\n"
        "      expect { c: \"hello world\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { c: $a + \" \" + $b }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "string concat: not null");
    ASSERT(hval_equal(result, expect), "string concat: matches");
    TEARDOWN();
}

static void test_comparison_operators(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output lt as boolean, eq as boolean, gt as boolean end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 }\n"
        "      expect { lt: true, eq: true, gt: false } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { lt: $x < 10, eq: $x = 5, gt: $x > 10 }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "comparison: not null");
    ASSERT(hval_equal(result, expect), "comparison: matches");
    TEARDOWN();
}

static void test_boolean_operators(void) {
    SETUP(
        "contract t\n"
        "  input a as boolean, b as boolean end\n"
        "  output and-val as boolean, or-val as boolean, not-val as boolean end\n"
        "  tests\n"
        "    test \"t\" given { a: true, b: false }\n"
        "      expect { and-val: false, or-val: true, not-val: false } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { and-val: $a and $b, or-val: $a or $b, not-val: not $a }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "boolean ops: not null");
    ASSERT(hval_equal(result, expect), "boolean ops: matches");
    TEARDOWN();
}

static void test_if_else(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 } expect { y: \"small\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: if $x < 10 then \"small\" else \"large\" end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "if/else: not null");
    ASSERT(hval_equal(result, expect), "if/else: matches");
    TEARDOWN();
}

static void test_let_binding(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 3 } expect { y: 9 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  let sq be $x * $x\n"
        "  result { y: sq }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "let: not null");
    ASSERT(hval_equal(result, expect), "let: matches");
    TEARDOWN();
}

static void test_match_literal(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  tests\n"
        "    test \"t\" given { x: 1 } expect { y: \"one\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when 1 then \"one\" when 2 then \"two\" else \"other\" end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "match literal: not null");
    ASSERT(hval_equal(result, expect), "match literal: matches");
    TEARDOWN();
}

static void test_match_binding_with_guard(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  tests\n"
        "    test \"t\" given { x: 50 } expect { y: \"medium\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x\n"
        "    when n and n > 100 then \"large\"\n"
        "    when n and n > 10 then \"medium\"\n"
        "    else \"small\"\n"
        "  end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "match guard: not null");
    ASSERT(hval_equal(result, expect), "match guard: matches");
    TEARDOWN();
}

static void test_match_range(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  tests\n"
        "    test \"t\" given { x: 42 } expect { y: \"medium\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x\n"
        "    when between 1 and 10 then \"small\"\n"
        "    when between 11 and 100 then \"medium\"\n"
        "    else \"large\"\n"
        "  end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "match range: not null");
    ASSERT(hval_equal(result, expect), "match range: matches");
    TEARDOWN();
}

static void test_match_wildcard(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  tests\n"
        "    test \"t\" given { x: 999 } expect { y: \"other\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when 1 then \"one\" when _ then \"other\" end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "match wildcard: not null");
    ASSERT(hval_equal(result, expect), "match wildcard: matches");
    TEARDOWN();
}

static void test_list_and_filter(void) {
    SETUP(
        "contract t\n"
        "  input nums as list of integer end\n"
        "  output evens as list of integer end\n"
        "  tests\n"
        "    test \"t\" given { nums: [1, 2, 3, 4] }\n"
        "      expect { evens: [2, 4] } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { evens: filter $nums where n n % 2 = 0 end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "filter: not null");
    ASSERT(hval_equal(result, expect), "filter: matches");
    TEARDOWN();
}

static void test_map(void) {
    SETUP(
        "contract t\n"
        "  input nums as list of integer end\n"
        "  output doubled as list of integer end\n"
        "  tests\n"
        "    test \"t\" given { nums: [1, 2, 3] }\n"
        "      expect { doubled: [2, 4, 6] } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { doubled: map $nums as n do n * 2 end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "map: not null");
    ASSERT(hval_equal(result, expect), "map: matches");
    TEARDOWN();
}

static void test_field_access(void) {
    SETUP(
        "contract t\n"
        "  input r as record name as string, age as integer end end\n"
        "  output name as string end\n"
        "  tests\n"
        "    test \"t\" given { r: { name: \"Alice\", age: 30 } }\n"
        "      expect { name: \"Alice\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { name: $r.name }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "access: not null");
    ASSERT(hval_equal(result, expect), "access: matches");
    TEARDOWN();
}

static void test_through_call(void) {
    SETUP(
        "contract t\n"
        "  input s as string end\n"
        "  output s as string end\n"
        "  tests\n"
        "    test \"t\" given { s: \"  HELLO  \" }\n"
        "      expect { s: \"hello\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { s: $s through trim({}) through lower({}) }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "through call: not null");
    ASSERT(hval_equal(result, expect), "through call: matches");
    TEARDOWN();
}

static void test_division_by_zero(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 0 } expect { y: 0 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: 1 / $x }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result == NULL, "div by zero: returns NULL");
    TEARDOWN();
}

static void test_unary_neg(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 } expect { y: -5 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: -$x }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "unary neg: not null");
    ASSERT(hval_equal(result, expect), "unary neg: matches");
    TEARDOWN();
}

/* ── New tests: operator & expression gaps ────────────────── */

static void test_neq_operator(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output a as boolean, b as boolean end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 }\n"
        "      expect { a: true, b: false } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { a: $x != 3, b: $x != 5 }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "neq: not null");
    ASSERT(hval_equal(result, expect), "neq: matches");
    TEARDOWN();
}

static void test_lte_gte_operators(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output lte as boolean, gte as boolean, lte-eq as boolean, gte-eq as boolean end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 }\n"
        "      expect { lte: true, gte: false, lte-eq: true, gte-eq: true } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { lte: $x <= 10, gte: $x >= 10, lte-eq: $x <= 5, gte-eq: $x >= 5 }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "lte/gte: not null");
    ASSERT(hval_equal(result, expect), "lte/gte: matches");
    TEARDOWN();
}

static void test_modulo(void) {
    SETUP(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output r as integer end\n"
        "  tests\n"
        "    test \"t\" given { a: 17, b: 5 } expect { r: 2 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $a % $b }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "modulo: not null");
    ASSERT(hval_equal(result, expect), "modulo: matches");
    TEARDOWN();
}

static void test_paren_grouping(void) {
    SETUP(
        "contract t\n"
        "  input a as integer, b as integer, c as integer end\n"
        "  output no-paren as integer, with-paren as integer end\n"
        "  tests\n"
        "    test \"t\" given { a: 2, b: 3, c: 4 }\n"
        "      expect { no-paren: 14, with-paren: 20 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { no-paren: $a + $b * $c, with-paren: ($a + $b) * $c }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "paren: not null");
    ASSERT(hval_equal(result, expect), "paren: matches");
    TEARDOWN();
}

/* ── New tests: through + filter/map ─────────────────────── */

static void test_through_filter(void) {
    SETUP(
        "contract t\n"
        "  input nums as list of integer end\n"
        "  output big as list of integer end\n"
        "  tests\n"
        "    test \"t\" given { nums: [1, 5, 2, 8, 3] }\n"
        "      expect { big: [5, 8] } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { big: $nums through filter $nums where n n > 4 end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "through filter: not null");
    ASSERT(hval_equal(result, expect), "through filter: matches");
    TEARDOWN();
}

static void test_through_map(void) {
    SETUP(
        "contract t\n"
        "  input nums as list of integer end\n"
        "  output tripled as list of integer end\n"
        "  tests\n"
        "    test \"t\" given { nums: [1, 2, 3] }\n"
        "      expect { tripled: [3, 6, 9] } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { tripled: $nums through map $nums as n do n * 3 end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "through map: not null");
    ASSERT(hval_equal(result, expect), "through map: matches");
    TEARDOWN();
}

/* ── New tests: stdlib gaps ──────────────────────────────── */

static void test_stdlib_upper(void) {
    SETUP(
        "contract t\n"
        "  input s as string end\n"
        "  output s as string end\n"
        "  tests\n"
        "    test \"t\" given { s: \"hello\" }\n"
        "      expect { s: \"HELLO\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { s: upper({ value: $s }) }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "upper: not null");
    ASSERT(hval_equal(result, expect), "upper: matches");
    TEARDOWN();
}

static void test_stdlib_to_integer(void) {
    SETUP(
        "contract t\n"
        "  input x as float end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 3.7 } expect { y: 3 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: to-integer({ value: $x }) }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "to-integer: not null");
    ASSERT(hval_equal(result, expect), "to-integer: matches");
    TEARDOWN();
}

static void test_fold_empty_list(void) {
    SETUP(
        "contract t\n"
        "  input nums as list of integer end\n"
        "  output total as integer end\n"
        "  tests\n"
        "    test \"t\" given { nums: [] } expect { total: 0 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { total: fold({ list: $nums, initial: 0, fn: \"add\" }) }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "fold empty: not null");
    ASSERT(hval_equal(result, expect), "fold empty: matches");
    TEARDOWN();
}

/* ── New tests: match edge cases ─────────────────────────── */

static void test_match_no_else_no_match(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as maybe integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 99 } expect { y: nothing } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when 1 then 10 when 2 then 20 end }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "match no-else: not null");
    ASSERT(hval_equal(result, expect), "match no-else: returns nothing");
    TEARDOWN();
}

static void test_match_list_pattern(void) {
    SETUP(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output desc as string end\n"
        "  tests\n"
        "    test \"t\" given { items: [10, 20, 30] }\n"
        "      expect { desc: \"first is 10\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    desc: match $items\n"
        "      when [] then \"empty\"\n"
        "      when [x] then \"single\"\n"
        "      when [first, ..rest] then \"first is \" + to-string({ value: first }).value\n"
        "      else \"other\"\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "list pattern: not null");
    ASSERT(hval_equal(result, expect), "list pattern: matches");
    TEARDOWN();
}

static void test_match_record_pattern(void) {
    SETUP(
        "contract t\n"
        "  input shape as record kind as string, size as integer end end\n"
        "  output desc as string end\n"
        "  tests\n"
        "    test \"t\" given { shape: { kind: \"box\", size: 5 } }\n"
        "      expect { desc: \"box\" } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    desc: match $shape\n"
        "      when { kind: k } then k\n"
        "      else \"unknown\"\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    HVal *expect = eval_expect(arena, prog, prog->contract->tests);
    ASSERT(result != NULL && expect != NULL, "record pattern: not null");
    ASSERT(hval_equal(result, expect), "record pattern: matches");
    TEARDOWN();
}

/* ── New tests: error paths ──────────────────────────────── */

static void test_error_access_non_record(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 } expect { y: 0 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x.name }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result == NULL, "access non-record: returns NULL");
    TEARDOWN();
}

static void test_error_not_on_non_boolean(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as boolean end\n"
        "  tests\n"
        "    test \"t\" given { x: 5 } expect { y: true } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: not $x }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result == NULL, "not non-bool: returns NULL");
    TEARDOWN();
}

static void test_error_modulo_by_zero(void) {
    SETUP(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"t\" given { x: 0 } expect { y: 0 } end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: 10 % $x }\n"
        "end\n"
    );
    HVal *result = run_test_case(arena, prog, prog->contract->tests);
    ASSERT(result == NULL, "mod by zero: returns NULL");
    TEARDOWN();
}

static void test_hval_equal_lists(void) {
    Arena *arena = arena_create(64 * 1024);

    /* Build two identical lists [1, 2, 3] */
    HVal *a3 = arena_calloc(arena, sizeof(HVal));
    a3->kind = VAL_INTEGER; a3->as.integer_val = 3; a3->next = NULL;
    HVal *a2 = arena_calloc(arena, sizeof(HVal));
    a2->kind = VAL_INTEGER; a2->as.integer_val = 2; a2->next = a3;
    HVal *a1 = arena_calloc(arena, sizeof(HVal));
    a1->kind = VAL_INTEGER; a1->as.integer_val = 1; a1->next = a2;
    HVal *la = arena_calloc(arena, sizeof(HVal));
    la->kind = VAL_LIST; la->as.list_head = a1;

    HVal *b3 = arena_calloc(arena, sizeof(HVal));
    b3->kind = VAL_INTEGER; b3->as.integer_val = 3; b3->next = NULL;
    HVal *b2 = arena_calloc(arena, sizeof(HVal));
    b2->kind = VAL_INTEGER; b2->as.integer_val = 2; b2->next = b3;
    HVal *b1 = arena_calloc(arena, sizeof(HVal));
    b1->kind = VAL_INTEGER; b1->as.integer_val = 1; b1->next = b2;
    HVal *lb = arena_calloc(arena, sizeof(HVal));
    lb->kind = VAL_LIST; lb->as.list_head = b1;

    ASSERT(hval_equal(la, lb), "equal lists: [1,2,3] = [1,2,3]");

    /* Different list [1, 2, 4] */
    b3->as.integer_val = 4;
    ASSERT(!hval_equal(la, lb), "unequal lists: [1,2,3] != [1,2,4]");

    arena_destroy(arena);
}

/* ── Main ─────────────────────────────────────────────────── */

int main(void) {
    printf("test_evaluator:\n");

    test_integer_literal();
    test_string_literal();
    test_boolean_true();
    test_nothing_literal();
    test_binary_int_arithmetic();
    test_float_arithmetic();
    test_mixed_arithmetic();
    test_string_concatenation();
    test_comparison_operators();
    test_boolean_operators();
    test_if_else();
    test_let_binding();
    test_match_literal();
    test_match_binding_with_guard();
    test_match_range();
    test_match_wildcard();
    test_list_and_filter();
    test_map();
    test_field_access();
    test_through_call();
    test_division_by_zero();
    test_unary_neg();

    /* operator & expression gaps */
    test_neq_operator();
    test_lte_gte_operators();
    test_modulo();
    test_paren_grouping();

    /* through + filter/map */
    test_through_filter();
    test_through_map();

    /* stdlib gaps */
    test_stdlib_upper();
    test_stdlib_to_integer();
    test_fold_empty_list();

    /* match edge cases */
    test_match_no_else_no_match();
    test_match_list_pattern();
    test_match_record_pattern();

    /* error paths */
    test_error_access_non_record();
    test_error_not_on_non_boolean();
    test_error_modulo_by_zero();

    test_hval_equal_lists();

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
