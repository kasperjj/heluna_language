/*
 * test_checker: unit tests for the Heluna static checker.
 *
 * Each test creates an inline source string, parses it, then runs the
 * checker and asserts on error count and messages.
 */

#include "heluna/checker.h"
#include "heluna/parser.h"
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

/* Parse source and run checker.  Sets `nerrs` to the error count.
 * If parse fails, nerrs is set to -1 so tests can still TEARDOWN safely. */
#define CHECK(src) \
    Arena *arena = arena_create(64 * 1024); \
    Lexer lex; \
    lexer_init(&lex, src, "test", arena); \
    Parser parser; \
    parser_init(&parser, &lex, arena); \
    AstProgram *prog = parser_parse(&parser); \
    ASSERT(!parser.had_error, "parse ok"); \
    ASSERT(prog != NULL, "prog not null"); \
    Checker checker; \
    int nerrs = -1; \
    if (prog) { \
        checker_init(&checker, prog, arena); \
        nerrs = checker_check(&checker); \
    }

#define TEARDOWN() arena_destroy(arena)

/* Check that an error message contains a substring */
static int has_error_containing(const Checker *c, const char *substr) {
    for (int i = 0; i < c->errors.count; i++) {
        if (strstr(c->errors.errors[i].message, substr)) return 1;
    }
    return 0;
}

/* ── Tests ────────────────────────────────────────────────── */

static void test_valid_minimal(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "valid minimal: 0 errors");
    TEARDOWN();
}

static void test_contract_name_mismatch(void) {
    CHECK(
        "contract foo\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define bar with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "name mismatch: has errors");
    ASSERT(has_error_containing(&checker, "does not match"),
           "name mismatch: message");
    TEARDOWN();
}

static void test_duplicate_input_field(void) {
    CHECK(
        "contract t\n"
        "  input\n"
        "    x as integer,\n"
        "    x as string\n"
        "  end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "dup input: has errors");
    ASSERT(has_error_containing(&checker, "duplicate input field"),
           "dup input: message");
    TEARDOWN();
}

static void test_duplicate_output_field(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output\n"
        "    y as integer,\n"
        "    y as string\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "dup output: has errors");
    ASSERT(has_error_containing(&checker, "duplicate output field"),
           "dup output: message");
    TEARDOWN();
}

static void test_invalid_input_ref(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $nonexistent }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "invalid input ref: has errors");
    ASSERT(has_error_containing(&checker, "$nonexistent"),
           "invalid input ref: message");
    TEARDOWN();
}

static void test_valid_input_ref(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "valid input ref: 0 errors");
    TEARDOWN();
}

static void test_let_shadowing(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let a be 1\n"
        "  let a be 2\n"
        "  result { y: a }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "let shadow: has errors");
    ASSERT(has_error_containing(&checker, "shadows"),
           "let shadow: message");
    TEARDOWN();
}

static void test_let_shadows_input(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let x be 1\n"
        "  result { y: x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "let shadows input: has errors");
    ASSERT(has_error_containing(&checker, "shadows"),
           "let shadows input: message");
    TEARDOWN();
}

static void test_undefined_ident(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: undefined-name }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "undefined ident: has errors");
    ASSERT(has_error_containing(&checker, "undefined identifier"),
           "undefined ident: message");
    TEARDOWN();
}

static void test_let_scope(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let a be $x + 1\n"
        "  result { y: a }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "let scope: 0 errors");
    TEARDOWN();
}

static void test_filter_var_scope(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output items as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { items: filter $items where s s > 0 end }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "filter var scope: 0 errors");
    TEARDOWN();
}

static void test_map_var_scope(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output items as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { items: map $items as item do item + 1 end }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "map var scope: 0 errors");
    TEARDOWN();
}

static void test_match_binding_scope(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    y: match $x\n"
        "      when n and n > 0 then \"positive\"\n"
        "      else \"other\"\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "match binding scope: 0 errors");
    TEARDOWN();
}

static void test_unknown_function_call(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: unknown-fn({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "unknown fn: has errors");
    ASSERT(has_error_containing(&checker, "unknown function"),
           "unknown fn: message");
    TEARDOWN();
}

static void test_stdlib_call(void) {
    CHECK(
        "contract t\n"
        "  input x as string end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: length({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "stdlib call: 0 errors");
    TEARDOWN();
}

static void test_uses_call(void) {
    CHECK(
        "contract t\n"
        "  uses other-fn\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: other-fn({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "uses call: 0 errors");
    TEARDOWN();
}

static void test_sanitizer_call(void) {
    CHECK(
        "contract t\n"
        "  tags\n"
        "    pii \"personal info\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash strips pii\n"
        "  end\n"
        "  input x as string tagged pii end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: hash({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "sanitizer call: 0 errors");
    TEARDOWN();
}

static void test_self_recursion(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: t({ x: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "self recursion: has errors");
    ASSERT(has_error_containing(&checker, "calls itself"),
           "self recursion: message");
    TEARDOWN();
}

static void test_undeclared_tag(void) {
    CHECK(
        "contract t\n"
        "  input x as string tagged pii end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "undeclared tag: has errors");
    ASSERT(has_error_containing(&checker, "undeclared tag"),
           "undeclared tag: message");
    TEARDOWN();
}

static void test_undeclared_tag_in_forbid(void) {
    CHECK(
        "contract t\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "  rules\n"
        "    forbid tagged pii in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "undeclared tag in forbid: has errors");
    ASSERT(has_error_containing(&checker, "undeclared tag"),
           "undeclared tag in forbid: message");
    TEARDOWN();
}

static void test_undeclared_tag_in_sanitizer(void) {
    CHECK(
        "contract t\n"
        "  tags\n"
        "    pii \"personal info\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash strips unknown-tag\n"
        "  end\n"
        "  input x as string tagged pii end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: hash({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "undeclared tag in sanitizer: has errors");
    ASSERT(has_error_containing(&checker, "strips undeclared tag"),
           "undeclared tag in sanitizer: message");
    TEARDOWN();
}

static void test_invalid_field_ref_in_rule(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  rules\n"
        "    require output.nonexistent\n"
        "      output.nonexistent > 0\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "invalid field ref: has errors");
    ASSERT(has_error_containing(&checker, "unknown output field"),
           "invalid field ref: message");
    TEARDOWN();
}

static void test_duplicate_tag_def(void) {
    CHECK(
        "contract t\n"
        "  tags\n"
        "    pii \"personal info\",\n"
        "    pii \"another pii\"\n"
        "  end\n"
        "  input x as string tagged pii end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "dup tag: has errors");
    ASSERT(has_error_containing(&checker, "duplicate tag"),
           "dup tag: message");
    TEARDOWN();
}

static void test_duplicate_test_name(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"same\"\n"
        "      given { x: 1 }\n"
        "      expect { y: 1 }\n"
        "    end\n"
        "    test \"same\"\n"
        "      given { x: 2 }\n"
        "      expect { y: 2 }\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "dup test: has errors");
    ASSERT(has_error_containing(&checker, "duplicate test"),
           "dup test: message");
    TEARDOWN();
}

static void test_multiple_errors(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let a be $nonexistent\n"
        "  let a be unknown-fn({ value: $x })\n"
        "  result { y: a }\n"
        "end\n"
    );
    ASSERT(nerrs >= 3, "multiple errors: at least 3");
    ASSERT(has_error_containing(&checker, "$nonexistent"),
           "multiple errors: invalid ref");
    ASSERT(has_error_containing(&checker, "unknown function"),
           "multiple errors: unknown fn");
    ASSERT(has_error_containing(&checker, "shadows"),
           "multiple errors: shadow");
    TEARDOWN();
}

/* ── Scope isolation tests ────────────────────────────────── */

static void test_filter_var_not_in_scope_outside(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let filtered be filter $items where s s > 0 end\n"
        "  result { y: s }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "filter var leaked: has errors");
    ASSERT(has_error_containing(&checker, "undefined identifier"),
           "filter var leaked: message");
    TEARDOWN();
}

static void test_map_var_not_in_scope_outside(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let mapped be map $items as item do item + 1 end\n"
        "  result { y: item }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "map var leaked: has errors");
    ASSERT(has_error_containing(&checker, "undefined identifier"),
           "map var leaked: message");
    TEARDOWN();
}

static void test_let_self_reference(void) {
    CHECK(
        "contract t\n"
        "  input y as integer end\n"
        "  output z as integer end\n"
        "end\n"
        "define t with input\n"
        "  let x be x + 1\n"
        "  result { z: x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "let self-ref: has errors");
    ASSERT(has_error_containing(&checker, "undefined identifier"),
           "let self-ref: message");
    TEARDOWN();
}

/* ── Shadowing in nested constructs ──────────────────────── */

static void test_filter_var_shadows_let(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output items as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  let n be 1\n"
        "  result { items: filter $items where n n > 0 end }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "filter shadows let: has errors");
    ASSERT(has_error_containing(&checker, "filter variable"),
           "filter shadows let: mentions filter");
    ASSERT(has_error_containing(&checker, "shadows"),
           "filter shadows let: mentions shadows");
    TEARDOWN();
}

static void test_map_var_shadows_let(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output items as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  let n be 1\n"
        "  result { items: map $items as n do n + 1 end }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "map shadows let: has errors");
    ASSERT(has_error_containing(&checker, "map variable"),
           "map shadows let: mentions map");
    ASSERT(has_error_containing(&checker, "shadows"),
           "map shadows let: mentions shadows");
    TEARDOWN();
}

static void test_match_binding_shadows_input(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    y: match $x\n"
        "      when x then \"got it\"\n"
        "      else \"other\"\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "match shadows input: has errors");
    ASSERT(has_error_containing(&checker, "shadows"),
           "match shadows input: message");
    TEARDOWN();
}

/* ── Pattern binding scope tests ─────────────────────────── */

static void test_match_list_pattern_bindings(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    y: match $items\n"
        "      when [a, b] then to-string({ value: a + b }).value\n"
        "      else \"other\"\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "list pattern bindings: 0 errors");
    TEARDOWN();
}

static void test_match_record_pattern_bindings(void) {
    CHECK(
        "contract t\n"
        "  input shape as record kind as string, radius as float end end\n"
        "  output y as float end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    y: match $shape\n"
        "      when { kind: \"circle\", radius: r } then r * 3.14\n"
        "      else 0.0\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "record pattern bindings: 0 errors");
    TEARDOWN();
}

static void test_match_rest_pattern_binding(void) {
    CHECK(
        "contract t\n"
        "  input items as list of integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    y: match $items\n"
        "      when [first, ..rest] then to-string({ value: first }).value\n"
        "      else \"empty\"\n"
        "    end\n"
        "  }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "rest pattern binding: 0 errors");
    TEARDOWN();
}

/* ── Other missing coverage ──────────────────────────────── */

static void test_self_recursion_in_let(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let a be t({ x: $x })\n"
        "  result { y: a }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "recursion in let: has errors");
    ASSERT(has_error_containing(&checker, "calls itself"),
           "recursion in let: message");
    TEARDOWN();
}

static void test_invalid_input_field_ref_in_rule(void) {
    CHECK(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  rules\n"
        "    forbid input.nonexistent in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "invalid input field ref: has errors");
    ASSERT(has_error_containing(&checker, "unknown input field"),
           "invalid input field ref: message");
    TEARDOWN();
}

static void test_duplicate_sanitizer_def(void) {
    CHECK(
        "contract t\n"
        "  tags\n"
        "    pii \"personal info\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash strips pii,\n"
        "    hash strips pii\n"
        "  end\n"
        "  input x as string tagged pii end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: hash({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(nerrs > 0, "dup sanitizer: has errors");
    ASSERT(has_error_containing(&checker, "duplicate sanitizer"),
           "dup sanitizer: message");
    TEARDOWN();
}

static void test_through_pipeline(void) {
    CHECK(
        "contract t\n"
        "  input email as string end\n"
        "  output email as string end\n"
        "end\n"
        "define t with input\n"
        "  result { email: $email through trim({}) through lower({}) }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "through pipeline: 0 errors");
    TEARDOWN();
}

static void test_complex_valid_program(void) {
    CHECK(
        "contract t\n"
        "  uses helper-fn\n"
        "  tags\n"
        "    secret \"confidential\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash strips secret\n"
        "  end\n"
        "  input\n"
        "    name as string,\n"
        "    scores as list of integer,\n"
        "    key as string tagged secret\n"
        "  end\n"
        "  output\n"
        "    label as string,\n"
        "    passing as list of integer,\n"
        "    total as integer,\n"
        "    safe-key as string\n"
        "  end\n"
        "  rules\n"
        "    forbid tagged secret in output\n"
        "  end\n"
        "  tests\n"
        "    test \"basic\"\n"
        "      given { name: \"Ada\", scores: [80, 50, 90], key: \"sk-1\" }\n"
        "      expect { label: \"Ada\", passing: [80, 90], total: 170, safe-key: \"abc\" }\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  let passing be filter $scores where s s >= 60 end\n"
        "  let total be fold({ list: passing, initial: 0, fn: \"add\" })\n"
        "  let desc be match total\n"
        "    when 0 then \"none\"\n"
        "    when n and n > 100 then \"high: \" + to-string({ value: n }).value\n"
        "    else \"some\"\n"
        "  end\n"
        "  let doubled be map passing as item do item * 2 end\n"
        "  let safe be hash({ value: $key })\n"
        "  let ext be helper-fn({ value: $name })\n"
        "  result {\n"
        "    label: $name,\n"
        "    passing: passing,\n"
        "    total: total,\n"
        "    safe-key: safe\n"
        "  }\n"
        "end\n"
    );
    ASSERT(nerrs == 0, "complex valid: 0 errors");
    TEARDOWN();
}

/* ── Main ─────────────────────────────────────────────────── */

int main(void) {
    printf("test_checker:\n");

    test_valid_minimal();
    test_contract_name_mismatch();
    test_duplicate_input_field();
    test_duplicate_output_field();
    test_invalid_input_ref();
    test_valid_input_ref();
    test_let_shadowing();
    test_let_shadows_input();
    test_undefined_ident();
    test_let_scope();
    test_filter_var_scope();
    test_map_var_scope();
    test_match_binding_scope();
    test_unknown_function_call();
    test_stdlib_call();
    test_uses_call();
    test_sanitizer_call();
    test_self_recursion();
    test_undeclared_tag();
    test_undeclared_tag_in_forbid();
    test_undeclared_tag_in_sanitizer();
    test_invalid_field_ref_in_rule();
    test_duplicate_tag_def();
    test_duplicate_test_name();
    test_multiple_errors();

    /* Scope isolation */
    test_filter_var_not_in_scope_outside();
    test_map_var_not_in_scope_outside();
    test_let_self_reference();

    /* Shadowing in nested constructs */
    test_filter_var_shadows_let();
    test_map_var_shadows_let();
    test_match_binding_shadows_input();

    /* Pattern binding scopes */
    test_match_list_pattern_bindings();
    test_match_record_pattern_bindings();
    test_match_rest_pattern_binding();

    /* Other coverage */
    test_self_recursion_in_let();
    test_invalid_input_field_ref_in_rule();
    test_duplicate_sanitizer_def();
    test_through_pipeline();
    test_complex_valid_program();

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
