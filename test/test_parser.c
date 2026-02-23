/*
 * test_parser: unit tests for the Heluna parser.
 *
 * Each test creates an arena + lexer + parser from an inline source string,
 * calls parser_parse(), and asserts on AST node properties.
 */

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

/* Convenience macro: set up arena, lexer, parser, and parse a program. */
#define PARSE(src) \
    Arena *arena = arena_create(64 * 1024); \
    Lexer lex; \
    lexer_init(&lex, src, "test", arena); \
    Parser parser; \
    parser_init(&parser, &lex, arena); \
    AstProgram *prog = parser_parse(&parser)

/* Convenience: teardown */
#define TEARDOWN() arena_destroy(arena)

/* ── Helpers ──────────────────────────────────────────────── */

/* Count if-branches */
static int count_if_branches(const AstIfBranch *b) {
    int n = 0;
    for (; b; b = b->next) n++;
    return n;
}

/* Count when-clauses */
static int count_when_clauses(const AstWhenClause *c) {
    int n = 0;
    for (; c; c = c->next) n++;
    return n;
}

/* Get the result expression (unwinding any let chain) */
static AstExpr *get_result(AstExpr *body) {
    while (body && body->kind == EXPR_LET)
        body = body->as.let.body;
    return body;
}

/* ── Tests ────────────────────────────────────────────────── */

static void test_minimal_program(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "minimal: no error");
    ASSERT(prog != NULL, "minimal: prog not null");
    ASSERT(prog->contract != NULL, "minimal: contract not null");
    ASSERT(strcmp(prog->contract->name, "t") == 0, "minimal: contract name");
    ASSERT(prog->function != NULL, "minimal: function not null");
    ASSERT(strcmp(prog->function->name, "t") == 0, "minimal: function name");
    ASSERT(prog->function->body->kind == EXPR_RECORD, "minimal: body is record");
    TEARDOWN();
}

static void test_literals(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output x as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { a: 42, b: 3.14, c: \"hi\", d: true, e: false, f: nothing }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "literals: no error");
    AstExpr *rec = get_result(prog->function->body);
    ASSERT(rec->kind == EXPR_RECORD, "literals: result is record");

    AstLabel *l = rec->as.record.labels;
    ASSERT(l && l->value->kind == EXPR_INTEGER, "literals: integer");
    ASSERT(l->value->as.integer_val == 42, "literals: integer value");
    l = l->next;
    ASSERT(l && l->value->kind == EXPR_FLOAT, "literals: float");
    l = l->next;
    ASSERT(l && l->value->kind == EXPR_STRING, "literals: string");
    l = l->next;
    ASSERT(l && l->value->kind == EXPR_TRUE, "literals: true");
    l = l->next;
    ASSERT(l && l->value->kind == EXPR_FALSE, "literals: false");
    l = l->next;
    ASSERT(l && l->value->kind == EXPR_NOTHING, "literals: nothing");
    TEARDOWN();
}

static void test_identifiers_and_refs(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let a be $x\n"
        "  result { y: a }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "ident/ref: no error");
    AstExpr *body = prog->function->body;
    ASSERT(body->kind == EXPR_LET, "ident/ref: let");
    ASSERT(body->as.let.binding->kind == EXPR_INPUT_REF, "ident/ref: input ref");
    ASSERT(strcmp(body->as.let.binding->as.input_ref.name, "x") == 0, "ident/ref: ref name stripped");

    AstExpr *rec = body->as.let.body;
    ASSERT(rec->kind == EXPR_RECORD, "ident/ref: result is record");
    ASSERT(rec->as.record.labels->value->kind == EXPR_IDENT, "ident/ref: ident");
    ASSERT(strcmp(rec->as.record.labels->value->as.ident.name, "a") == 0, "ident/ref: ident name");
    TEARDOWN();
}

static void test_binary_arithmetic(void) {
    PARSE(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $a + $b }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "arith: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_BINARY, "arith: binary node");
    ASSERT(val->as.binary.op == BIN_ADD, "arith: add op");
    ASSERT(val->as.binary.left->kind == EXPR_INPUT_REF, "arith: left is ref");
    ASSERT(val->as.binary.right->kind == EXPR_INPUT_REF, "arith: right is ref");
    TEARDOWN();
}

static void test_precedence(void) {
    /* $a + $b * $c  should parse as (+ $a (* $b $c)) */
    PARSE(
        "contract t\n"
        "  input a as integer, b as integer, c as integer end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $a + $b * $c }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "prec: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_BINARY, "prec: top is binary");
    ASSERT(val->as.binary.op == BIN_ADD, "prec: top is add");
    ASSERT(val->as.binary.right->kind == EXPR_BINARY, "prec: right is binary");
    ASSERT(val->as.binary.right->as.binary.op == BIN_MUL, "prec: right is mul");
    TEARDOWN();
}

static void test_unary_neg(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: -$x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "neg: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_UNARY_NEG, "neg: unary neg");
    ASSERT(val->as.unary.operand->kind == EXPR_INPUT_REF, "neg: operand is ref");
    TEARDOWN();
}

static void test_comparison(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $x >= 10 }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "cmp: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_BINARY, "cmp: binary");
    ASSERT(val->as.binary.op == BIN_GTE, "cmp: >=");
    TEARDOWN();
}

static void test_boolean_ops(void) {
    PARSE(
        "contract t\n"
        "  input a as boolean, b as boolean end\n"
        "  output r as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $a and ($b or not $a) }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "bool: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_BINARY, "bool: top is binary");
    ASSERT(val->as.binary.op == BIN_AND, "bool: top is and");
    /* right side is a paren containing (or ...) */
    AstExpr *right = val->as.binary.right;
    ASSERT(right->kind == EXPR_PAREN, "bool: right is paren");
    AstExpr *inner = right->as.paren.inner;
    ASSERT(inner->kind == EXPR_BINARY, "bool: inner is binary");
    ASSERT(inner->as.binary.op == BIN_OR, "bool: inner is or");
    /* 'not $a' on the right of 'or' */
    ASSERT(inner->as.binary.right->kind == EXPR_NOT, "bool: not node");
    TEARDOWN();
}

static void test_if_else(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: if $x < 10 then \"small\"\n"
        "    else if $x < 100 then \"medium\"\n"
        "    else \"large\"\n"
        "    end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "if: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_IF, "if: is if");
    int n = count_if_branches(val->as.if_expr.branches);
    ASSERT(n == 3, "if: 3 branches (if, else if, else)");
    /* first branch has condition */
    ASSERT(val->as.if_expr.branches->condition != NULL, "if: first has cond");
    /* last branch has no condition (else) */
    AstIfBranch *last = val->as.if_expr.branches->next->next;
    ASSERT(last->condition == NULL, "if: else has no cond");
    TEARDOWN();
}

static void test_match_when(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $x\n"
        "    when 1 then \"one\"\n"
        "    when 2 then \"two\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "match: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_MATCH, "match: is match");
    ASSERT(val->as.match.subject->kind == EXPR_INPUT_REF, "match: subject is ref");
    int n = count_when_clauses(val->as.match.clauses);
    ASSERT(n == 2, "match: 2 when clauses");
    ASSERT(val->as.match.else_body != NULL, "match: has else");
    /* first clause pattern is literal 1 */
    ASSERT(val->as.match.clauses->pattern->kind == PAT_LITERAL, "match: first pat is literal");
    TEARDOWN();
}

static void test_let_chaining(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  let a be $x\n"
        "  let b be a + 1\n"
        "  result { r: b }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "let: no error");
    AstExpr *body = prog->function->body;
    ASSERT(body->kind == EXPR_LET, "let: first is let");
    ASSERT(strcmp(body->as.let.name, "a") == 0, "let: first name");
    AstExpr *second = body->as.let.body;
    ASSERT(second->kind == EXPR_LET, "let: second is let");
    ASSERT(strcmp(second->as.let.name, "b") == 0, "let: second name");
    AstExpr *result = second->as.let.body;
    ASSERT(result->kind == EXPR_RECORD, "let: result is record");
    TEARDOWN();
}

static void test_filter_map(void) {
    PARSE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output ys as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  let evens be filter $xs where n n % 2 = 0 end\n"
        "  let doubled be map evens as n do n * 2 end\n"
        "  result { ys: doubled }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "filter/map: no error");
    AstExpr *body = prog->function->body;
    ASSERT(body->kind == EXPR_LET, "filter/map: first let");
    ASSERT(body->as.let.binding->kind == EXPR_FILTER, "filter/map: filter");
    ASSERT(strcmp(body->as.let.binding->as.filter.var_name, "n") == 0, "filter/map: filter var");

    AstExpr *second = body->as.let.body;
    ASSERT(second->kind == EXPR_LET, "filter/map: second let");
    ASSERT(second->as.let.binding->kind == EXPR_MAP, "filter/map: map");
    ASSERT(strcmp(second->as.let.binding->as.map.var_name, "n") == 0, "filter/map: map var");
    TEARDOWN();
}

static void test_through(void) {
    PARSE(
        "contract t\n"
        "  input x as string end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $x through trim({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "through: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_THROUGH, "through: is through");
    ASSERT(val->as.through.left->kind == EXPR_INPUT_REF, "through: left is ref");
    ASSERT(val->as.through.right->kind == EXPR_CALL, "through: right is call");
    TEARDOWN();
}

static void test_patterns(void) {
    /* Tests wildcard, binding, range, list with rest, and record patterns */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $x\n"
        "    when _ then \"wildcard\"\n"
        "    when between 1 and 10 then \"range\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "pat: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(m->kind == EXPR_MATCH, "pat: is match");
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_WILDCARD, "pat: wildcard");
    AstWhenClause *c2 = c1->next;
    ASSERT(c2->pattern->kind == PAT_RANGE, "pat: range");
    TEARDOWN();
}

static void test_list_pattern(void) {
    PARSE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $xs\n"
        "    when [] then \"empty\"\n"
        "    when [x] then \"one\"\n"
        "    when [a, ..rest] then \"many\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "listpat: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_LIST, "listpat: empty list");
    ASSERT(c1->pattern->as.list.elements == NULL, "listpat: empty has no elems");

    AstWhenClause *c2 = c1->next;
    ASSERT(c2->pattern->kind == PAT_LIST, "listpat: single");

    AstWhenClause *c3 = c2->next;
    ASSERT(c3->pattern->kind == PAT_LIST, "listpat: rest");
    ASSERT(c3->pattern->as.list.rest_name != NULL, "listpat: has rest");
    ASSERT(strcmp(c3->pattern->as.list.rest_name, "rest") == 0, "listpat: rest name");
    TEARDOWN();
}

static void test_record_pattern(void) {
    PARSE(
        "contract t\n"
        "  input s as record kind as string, val as integer end end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $s\n"
        "    when { kind: \"circle\", val: v } then \"got circle\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "recpat: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_RECORD, "recpat: record pattern");
    AstFieldPattern *f1 = c1->pattern->as.record.fields;
    ASSERT(strcmp(f1->name, "kind") == 0, "recpat: first field name");
    ASSERT(f1->pattern->kind == PAT_LITERAL, "recpat: first is literal");
    AstFieldPattern *f2 = f1->next;
    ASSERT(strcmp(f2->name, "val") == 0, "recpat: second field name");
    ASSERT(f2->pattern->kind == PAT_BINDING, "recpat: second is binding");
    TEARDOWN();
}

static void test_types(void) {
    PARSE(
        "contract t\n"
        "  input\n"
        "    a as string,\n"
        "    b as integer,\n"
        "    c as float,\n"
        "    d as boolean,\n"
        "    e as maybe integer,\n"
        "    f as list of string,\n"
        "    g as record x as integer end\n"
        "  end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: 0 }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "types: no error");
    AstFieldDecl *f = prog->contract->input;
    ASSERT(f->type->kind == TYPE_STRING, "types: string");
    f = f->next;
    ASSERT(f->type->kind == TYPE_INTEGER, "types: integer");
    f = f->next;
    ASSERT(f->type->kind == TYPE_FLOAT, "types: float");
    f = f->next;
    ASSERT(f->type->kind == TYPE_BOOLEAN, "types: boolean");
    f = f->next;
    ASSERT(f->type->kind == TYPE_MAYBE, "types: maybe");
    ASSERT(f->type->as.maybe.inner->kind == TYPE_INTEGER, "types: maybe inner");
    f = f->next;
    ASSERT(f->type->kind == TYPE_LIST, "types: list");
    ASSERT(f->type->as.list.inner->kind == TYPE_STRING, "types: list inner");
    f = f->next;
    ASSERT(f->type->kind == TYPE_RECORD, "types: record");
    ASSERT(f->type->as.record.fields != NULL, "types: record has fields");
    TEARDOWN();
}

static void test_contract_sections(void) {
    PARSE(
        "contract t\n"
        "  uses other-module\n"
        "  tags\n"
        "    secret \"confidential\",\n"
        "    internal \"internal only\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash strips secret internal\n"
        "  end\n"
        "  input\n"
        "    x as string tagged secret\n"
        "  end\n"
        "  output\n"
        "    y as string\n"
        "  end\n"
        "  rules\n"
        "    forbid tagged secret in output\n"
        "  end\n"
        "  tests\n"
        "    test \"basic\"\n"
        "      given { x: \"hi\" }\n"
        "      expect { y: \"hi\" }\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: hash({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "sections: no error");
    AstContract *c = prog->contract;
    ASSERT(c->uses_count == 1, "sections: uses count");
    ASSERT(strcmp(c->uses[0], "other-module") == 0, "sections: uses name");
    ASSERT(c->tags != NULL, "sections: has tags");
    ASSERT(strcmp(c->tags->name, "secret") == 0, "sections: tag name");
    ASSERT(c->tags->next != NULL, "sections: second tag");
    ASSERT(c->sanitizers != NULL, "sections: has sanitizers");
    ASSERT(strcmp(c->sanitizers->name, "hash") == 0, "sections: sanitizer name");
    ASSERT(c->sanitizers->stripped_count == 2, "sections: strips 2 tags");
    ASSERT(c->rules != NULL, "sections: has rules");
    ASSERT(c->rules->kind == RULE_FORBID_TAGGED, "sections: forbid tagged rule");
    ASSERT(c->tests != NULL, "sections: has tests");
    /* Tagged field */
    ASSERT(c->input->tag_count == 1, "sections: input field has tag");
    ASSERT(strcmp(c->input->tags[0], "secret") == 0, "sections: tagged secret");
    TEARDOWN();
}

static void test_accessor_chain(void) {
    PARSE(
        "contract t\n"
        "  input p as record name as string end end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $p.name }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "access: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_ACCESS, "access: is access");
    ASSERT(strcmp(val->as.access.field, "name") == 0, "access: field name");
    ASSERT(val->as.access.object->kind == EXPR_INPUT_REF, "access: object is ref");
    TEARDOWN();
}

static void test_function_call(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: to-string({ value: $x }).value }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "call: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    /* to-string({...}).value → ACCESS(CALL, "value") */
    ASSERT(val->kind == EXPR_ACCESS, "call: access wrapper");
    ASSERT(val->as.access.object->kind == EXPR_CALL, "call: is call");
    ASSERT(strcmp(val->as.access.object->as.call.name, "to-string") == 0, "call: name");
    ASSERT(val->as.access.object->as.call.arg->kind == EXPR_RECORD, "call: arg is record");
    TEARDOWN();
}

static void test_comments_skipped(void) {
    PARSE(
        "# leading comment\n"
        "contract t\n"
        "  # comment in contract\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "# comment between\n"
        "define t with input\n"
        "  # comment in function\n"
        "  result { y: $x }\n"
        "end\n"
        "# trailing comment\n"
    );
    ASSERT(!parser.had_error, "comments: no error");
    ASSERT(prog != NULL, "comments: prog not null");
    ASSERT(strcmp(prog->contract->name, "t") == 0, "comments: name correct");
    TEARDOWN();
}

static void test_error_missing_end(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        /* missing contract 'end' */
    );
    (void)prog;
    ASSERT(parser.had_error, "error: missing end detected");
    TEARDOWN();
}

static void test_error_invalid_token(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: @ }\n"
        "end\n"
    );
    (void)prog;
    ASSERT(parser.had_error, "error: invalid token detected");
    TEARDOWN();
}

static void test_keywords_as_names(void) {
    /* Record label using keyword name 'list:' */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output list as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { list: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "kwname: no error");
    AstExpr *rec = get_result(prog->function->body);
    ASSERT(rec->kind == EXPR_RECORD, "kwname: result is record");
    ASSERT(strcmp(rec->as.record.labels->name, "list") == 0, "kwname: label is 'list'");
    /* output field named 'list' */
    ASSERT(strcmp(prog->contract->output->name, "list") == 0, "kwname: output field named 'list'");
    TEARDOWN();
}

static void test_require_rule(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  rules\n"
        "    require output.y\n"
        "      output.y > 0\n"
        "      else reject \"must be positive\"\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "require: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r != NULL, "require: has rule");
    ASSERT(r->kind == RULE_REQUIRE, "require: is require");
    ASSERT(r->as.require.reject_msg != NULL, "require: has reject msg");
    ASSERT(r->as.require.field_ref->is_output == 1, "require: is output ref");
    TEARDOWN();
}

static void test_match_rule(void) {
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  rules\n"
        "    match output.y\n"
        "      when \"ok\" then true\n"
        "      when \"fail\" then false\n"
        "      else reject \"invalid value\"\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: \"ok\" }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "matchrule: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r != NULL, "matchrule: has rule");
    ASSERT(r->kind == RULE_MATCH, "matchrule: is match rule");
    ASSERT(r->as.match.reject_msg != NULL, "matchrule: has reject msg");
    TEARDOWN();
}

/* ── High-priority gap tests ─────────────────────────────── */

static void test_forbid_field_rule(void) {
    /* RULE_FORBID_FIELD (non-tagged forbid) — completely untested path */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  rules\n"
        "    forbid input.x in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "forbid-field: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r != NULL, "forbid-field: has rule");
    ASSERT(r->kind == RULE_FORBID_FIELD, "forbid-field: is forbid field");
    ASSERT(r->as.forbid_field.field_ref->is_output == 0, "forbid-field: is input ref");
    ASSERT(r->as.forbid_field.field_ref->accessor_count == 1, "forbid-field: 1 accessor");
    ASSERT(strcmp(r->as.forbid_field.field_ref->accessors[0], "x") == 0, "forbid-field: accessor name");
    TEARDOWN();
}

static void test_guard_expression(void) {
    /* when clause with 'and' guard */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $x\n"
        "    when n and n > 100 then \"big\"\n"
        "    when n and n > 0 then \"pos\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "guard: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(m->kind == EXPR_MATCH, "guard: is match");
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_BINDING, "guard: first is binding");
    ASSERT(c1->guard != NULL, "guard: first has guard");
    ASSERT(c1->guard->kind == EXPR_BINARY, "guard: guard is comparison");
    ASSERT(c1->guard->as.binary.op == BIN_GT, "guard: guard is >");
    AstWhenClause *c2 = c1->next;
    ASSERT(c2->guard != NULL, "guard: second has guard");
    TEARDOWN();
}

static void test_require_without_reject(void) {
    /* require rule without else reject — optional path */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  rules\n"
        "    require output.y\n"
        "      output.y > 0\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "req-no-reject: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r != NULL, "req-no-reject: has rule");
    ASSERT(r->kind == RULE_REQUIRE, "req-no-reject: is require");
    ASSERT(r->as.require.reject_msg == NULL, "req-no-reject: no reject msg");
    ASSERT(r->as.require.condition != NULL, "req-no-reject: has condition");
    TEARDOWN();
}

static void test_match_rule_without_reject(void) {
    /* match rule without else reject */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "  rules\n"
        "    match output.y\n"
        "      when \"ok\" then true\n"
        "      when \"fail\" then false\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: \"ok\" }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "matchrule-no-reject: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r->kind == RULE_MATCH, "matchrule-no-reject: is match");
    ASSERT(r->as.match.reject_msg == NULL, "matchrule-no-reject: no reject msg");
    TEARDOWN();
}

static void test_and_or_precedence(void) {
    /* $a or $b and $c  should parse as (or $a (and $b $c)) */
    PARSE(
        "contract t\n"
        "  input a as boolean, b as boolean, c as boolean end\n"
        "  output r as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $a or $b and $c }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "and-or-prec: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_BINARY, "and-or-prec: top is binary");
    ASSERT(val->as.binary.op == BIN_OR, "and-or-prec: top is or");
    ASSERT(val->as.binary.left->kind == EXPR_INPUT_REF, "and-or-prec: left is $a");
    ASSERT(val->as.binary.right->kind == EXPR_BINARY, "and-or-prec: right is binary");
    ASSERT(val->as.binary.right->as.binary.op == BIN_AND, "and-or-prec: right is and");
    TEARDOWN();
}

static void test_left_associativity(void) {
    /* $a - $b - $c  should parse as (- (- $a $b) $c) */
    PARSE(
        "contract t\n"
        "  input a as integer, b as integer, c as integer end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $a - $b - $c }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "left-assoc: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_BINARY, "left-assoc: top is binary");
    ASSERT(val->as.binary.op == BIN_SUB, "left-assoc: top is sub");
    /* left child should also be sub: ($a - $b) */
    ASSERT(val->as.binary.left->kind == EXPR_BINARY, "left-assoc: left is binary");
    ASSERT(val->as.binary.left->as.binary.op == BIN_SUB, "left-assoc: left is sub");
    /* right child is just $c */
    ASSERT(val->as.binary.right->kind == EXPR_INPUT_REF, "left-assoc: right is $c");
    TEARDOWN();
}

static void test_all_comparison_ops(void) {
    /* Test all 6 comparison operators produce correct BinOp */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output a as boolean, b as boolean, c as boolean,"
        "    d as boolean, e as boolean, f as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    a: $x = 1,\n"
        "    b: $x != 2,\n"
        "    c: $x < 3,\n"
        "    d: $x > 4,\n"
        "    e: $x <= 5,\n"
        "    f: $x >= 6\n"
        "  }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "all-cmp: no error");
    AstExpr *rec = get_result(prog->function->body);
    AstLabel *l = rec->as.record.labels;
    ASSERT(l->value->as.binary.op == BIN_EQ, "all-cmp: =");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_NEQ, "all-cmp: !=");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_LT, "all-cmp: <");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_GT, "all-cmp: >");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_LTE, "all-cmp: <=");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_GTE, "all-cmp: >=");
    TEARDOWN();
}

static void test_negative_pattern(void) {
    /* Negative integer and negative float in match patterns */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $x\n"
        "    when -5 then \"neg five\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "negpat: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_LITERAL, "negpat: is literal");
    ASSERT(c1->pattern->as.literal.value->kind == EXPR_INTEGER, "negpat: is integer");
    ASSERT(c1->pattern->as.literal.value->as.integer_val == -5, "negpat: value is -5");
    TEARDOWN();
}

static void test_through_filter(void) {
    /* through with filter on RHS */
    PARSE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output ys as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { ys: $xs through filter $xs where n n > 0 end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "through-filter: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_THROUGH, "through-filter: is through");
    ASSERT(val->as.through.right->kind == EXPR_FILTER, "through-filter: right is filter");
    TEARDOWN();
}

static void test_through_map(void) {
    /* through with map on RHS */
    PARSE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output ys as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { ys: $xs through map $xs as n do n * 2 end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "through-map: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_THROUGH, "through-map: is through");
    ASSERT(val->as.through.right->kind == EXPR_MAP, "through-map: right is map");
    TEARDOWN();
}

static void test_if_without_else_error(void) {
    /* if/then/end without else must produce an error */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: if $x > 0 then \"positive\" end }\n"
        "end\n"
    );
    (void)prog;
    ASSERT(parser.had_error, "if-no-else: error detected");
    TEARDOWN();
}

/* ── Medium-priority gap tests ───────────────────────────── */

static void test_deep_accessor_chain(void) {
    /* $x.a.b.c → nested EXPR_ACCESS */
    PARSE(
        "contract t\n"
        "  input x as record a as record b as record c as integer end end end end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $x.a.b.c }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "deep-access: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    /* Outermost: .c */
    ASSERT(val->kind == EXPR_ACCESS, "deep-access: outer is access");
    ASSERT(strcmp(val->as.access.field, "c") == 0, "deep-access: outer field is c");
    /* Middle: .b */
    AstExpr *mid = val->as.access.object;
    ASSERT(mid->kind == EXPR_ACCESS, "deep-access: mid is access");
    ASSERT(strcmp(mid->as.access.field, "b") == 0, "deep-access: mid field is b");
    /* Inner: .a */
    AstExpr *inner = mid->as.access.object;
    ASSERT(inner->kind == EXPR_ACCESS, "deep-access: inner is access");
    ASSERT(strcmp(inner->as.access.field, "a") == 0, "deep-access: inner field is a");
    /* Base: $x */
    ASSERT(inner->as.access.object->kind == EXPR_INPUT_REF, "deep-access: base is ref");
    TEARDOWN();
}

static void test_anonymous_rest_pattern(void) {
    /* [a, ..] — rest without a name, sets rest_name to "" */
    PARSE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $xs\n"
        "    when [first, ..] then \"has items\"\n"
        "    else \"empty\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "anon-rest: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_LIST, "anon-rest: is list pattern");
    ASSERT(c1->pattern->as.list.rest_name != NULL, "anon-rest: has rest");
    ASSERT(strcmp(c1->pattern->as.list.rest_name, "") == 0, "anon-rest: rest name is empty");
    TEARDOWN();
}

static void test_empty_record_pattern(void) {
    /* when {} then ... */
    PARSE(
        "contract t\n"
        "  input x as record kind as string end end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: match $x\n"
        "    when {} then \"empty record\"\n"
        "    else \"other\"\n"
        "  end }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "empty-recpat: no error");
    AstExpr *m = get_result(prog->function->body)->as.record.labels->value;
    AstWhenClause *c1 = m->as.match.clauses;
    ASSERT(c1->pattern->kind == PAT_RECORD, "empty-recpat: is record pattern");
    ASSERT(c1->pattern->as.record.fields == NULL, "empty-recpat: no fields");
    TEARDOWN();
}

static void test_multiple_uses(void) {
    /* uses a, b */
    PARSE(
        "contract t\n"
        "  uses module-a, module-b\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "multi-uses: no error");
    ASSERT(prog->contract->uses_count == 2, "multi-uses: 2 uses");
    ASSERT(strcmp(prog->contract->uses[0], "module-a") == 0, "multi-uses: first");
    ASSERT(strcmp(prog->contract->uses[1], "module-b") == 0, "multi-uses: second");
    TEARDOWN();
}

static void test_tag_without_description(void) {
    /* tag defined without a description string */
    PARSE(
        "contract t\n"
        "  tags\n"
        "    secret,\n"
        "    internal \"for internal use\"\n"
        "  end\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "tag-no-desc: no error");
    AstTagDef *t1 = prog->contract->tags;
    ASSERT(t1 != NULL, "tag-no-desc: has first tag");
    ASSERT(strcmp(t1->name, "secret") == 0, "tag-no-desc: first name");
    ASSERT(t1->description == NULL, "tag-no-desc: first has no description");
    AstTagDef *t2 = t1->next;
    ASSERT(t2 != NULL, "tag-no-desc: has second tag");
    ASSERT(t2->description != NULL, "tag-no-desc: second has description");
    TEARDOWN();
}

static void test_multiple_tagged_annotations(void) {
    /* field with multiple tags: tagged pii restricted */
    PARSE(
        "contract t\n"
        "  input\n"
        "    name as string tagged pii restricted\n"
        "  end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $name }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "multi-tag: no error");
    AstFieldDecl *f = prog->contract->input;
    ASSERT(f->tag_count == 2, "multi-tag: 2 tags");
    ASSERT(strcmp(f->tags[0], "pii") == 0, "multi-tag: first tag");
    ASSERT(strcmp(f->tags[1], "restricted") == 0, "multi-tag: second tag");
    TEARDOWN();
}

static void test_chained_through(void) {
    /* x through f({}) through g({}) — two through ops */
    PARSE(
        "contract t\n"
        "  input x as string end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: $x through trim({}) through lower({}) }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "chain-through: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    /* Outer: through lower */
    ASSERT(val->kind == EXPR_THROUGH, "chain-through: outer is through");
    ASSERT(val->as.through.right->kind == EXPR_CALL, "chain-through: outer right is call");
    ASSERT(strcmp(val->as.through.right->as.call.name, "lower") == 0, "chain-through: outer is lower");
    /* Inner: through trim */
    AstExpr *inner = val->as.through.left;
    ASSERT(inner->kind == EXPR_THROUGH, "chain-through: inner is through");
    ASSERT(inner->as.through.right->kind == EXPR_CALL, "chain-through: inner right is call");
    ASSERT(strcmp(inner->as.through.right->as.call.name, "trim") == 0, "chain-through: inner is trim");
    /* Base: $x */
    ASSERT(inner->as.through.left->kind == EXPR_INPUT_REF, "chain-through: base is ref");
    TEARDOWN();
}

static void test_empty_record_literal(void) {
    /* {} as a value in expression context */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as record kind as string end end\n"
        "end\n"
        "define t with input\n"
        "  result { r: {} }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "empty-rec: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_RECORD, "empty-rec: is record");
    ASSERT(val->as.record.labels == NULL, "empty-rec: no labels");
    TEARDOWN();
}

static void test_empty_list_literal(void) {
    /* [] as a value in expression context */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output r as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: [] }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "empty-list: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_LIST, "empty-list: is list");
    ASSERT(val->as.list.elements == NULL, "empty-list: no elements");
    TEARDOWN();
}

static void test_call_with_empty_arg(void) {
    /* foo({}) — function call with empty record arg */
    PARSE(
        "contract t\n"
        "  input x as string end\n"
        "  output r as string end\n"
        "end\n"
        "define t with input\n"
        "  result { r: trim({}) }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "call-empty: no error");
    AstExpr *val = get_result(prog->function->body)->as.record.labels->value;
    ASSERT(val->kind == EXPR_CALL, "call-empty: is call");
    ASSERT(strcmp(val->as.call.name, "trim") == 0, "call-empty: name");
    ASSERT(val->as.call.arg->kind == EXPR_RECORD, "call-empty: arg is record");
    ASSERT(val->as.call.arg->as.record.labels == NULL, "call-empty: arg is empty");
    TEARDOWN();
}

static void test_require_with_input_ref(void) {
    /* require rule on input field ref */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  rules\n"
        "    require input.x\n"
        "      input.x > 0\n"
        "      else reject \"must be positive\"\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "req-input: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r->kind == RULE_REQUIRE, "req-input: is require");
    ASSERT(r->as.require.field_ref->is_output == 0, "req-input: is input ref");
    TEARDOWN();
}

static void test_all_binary_ops(void) {
    /* Verify -, *, /, % produce correct BinOp */
    PARSE(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output s as integer, m as integer, d as integer, r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { s: $a - $b, m: $a * $b, d: $a / $b, r: $a % $b }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "all-binop: no error");
    AstLabel *l = get_result(prog->function->body)->as.record.labels;
    ASSERT(l->value->as.binary.op == BIN_SUB, "all-binop: -");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_MUL, "all-binop: *");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_DIV, "all-binop: /");
    l = l->next;
    ASSERT(l->value->as.binary.op == BIN_MOD, "all-binop: %");
    TEARDOWN();
}

static void test_nested_maybe_list_types(void) {
    /* maybe list of string, list of list of integer */
    PARSE(
        "contract t\n"
        "  input\n"
        "    a as maybe list of string,\n"
        "    b as list of list of integer\n"
        "  end\n"
        "  output r as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: 0 }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "nested-types: no error");
    AstFieldDecl *fa = prog->contract->input;
    ASSERT(fa->type->kind == TYPE_MAYBE, "nested-types: maybe");
    ASSERT(fa->type->as.maybe.inner->kind == TYPE_LIST, "nested-types: maybe>list");
    ASSERT(fa->type->as.maybe.inner->as.list.inner->kind == TYPE_STRING, "nested-types: maybe>list>string");

    AstFieldDecl *fb = fa->next;
    ASSERT(fb->type->kind == TYPE_LIST, "nested-types: list");
    ASSERT(fb->type->as.list.inner->kind == TYPE_LIST, "nested-types: list>list");
    ASSERT(fb->type->as.list.inner->as.list.inner->kind == TYPE_INTEGER, "nested-types: list>list>integer");
    TEARDOWN();
}

static void test_multiple_test_cases(void) {
    /* Verify multiple test cases are parsed and linked */
    PARSE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "  tests\n"
        "    test \"first\"\n"
        "      given { x: 1 }\n"
        "      expect { y: 1 }\n"
        "    end\n"
        "    test \"second\"\n"
        "      given { x: 2 }\n"
        "      expect { y: 2 }\n"
        "    end\n"
        "    test \"third\"\n"
        "      given { x: 3 }\n"
        "      expect { y: 3 }\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "multi-test: no error");
    AstTestCase *tc = prog->contract->tests;
    ASSERT(tc != NULL, "multi-test: has first");
    ASSERT(tc->next != NULL, "multi-test: has second");
    ASSERT(tc->next->next != NULL, "multi-test: has third");
    ASSERT(tc->next->next->next == NULL, "multi-test: no fourth");
    TEARDOWN();
}

static void test_field_ref_deep_accessor(void) {
    /* require output.user.email — multi-level field ref */
    PARSE(
        "contract t\n"
        "  input x as record user as record email as string end end end\n"
        "  output user as record email as string end end\n"
        "  rules\n"
        "    require output.user.email\n"
        "      output.user.email != \"\"\n"
        "    end\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { user: { email: $x.user.email } }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "deep-fref: no error");
    AstRule *r = prog->contract->rules;
    ASSERT(r->kind == RULE_REQUIRE, "deep-fref: is require");
    ASSERT(r->as.require.field_ref->accessor_count == 2, "deep-fref: 2 accessors");
    ASSERT(strcmp(r->as.require.field_ref->accessors[0], "user") == 0, "deep-fref: first is user");
    ASSERT(strcmp(r->as.require.field_ref->accessors[1], "email") == 0, "deep-fref: second is email");
    TEARDOWN();
}

/* ── Sanitizer using clause tests ─────────────────────────── */

static void test_sanitizer_using_clause(void) {
    PARSE(
        "contract t\n"
        "  tags\n"
        "    pii \"personal info\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash using sha256 strips pii\n"
        "  end\n"
        "  input x as string tagged pii end\n"
        "  output y as string end\n"
        "  rules\n"
        "    forbid tagged pii in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: hash({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(!parser.had_error, "using clause: no parse error");
    ASSERT(prog != NULL, "using clause: prog not null");
    ASSERT(prog->contract->sanitizers != NULL, "using clause: has sanitizers");
    ASSERT(strcmp(prog->contract->sanitizers->name, "hash") == 0,
           "using clause: sanitizer name is hash");
    ASSERT(prog->contract->sanitizers->impl_name != NULL,
           "using clause: impl_name not null");
    ASSERT(strcmp(prog->contract->sanitizers->impl_name, "sha256") == 0,
           "using clause: impl_name is sha256");
    ASSERT(prog->contract->sanitizers->stripped_count == 1,
           "using clause: strips 1 tag");
    TEARDOWN();
}

static void test_sanitizer_without_using(void) {
    PARSE(
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
    ASSERT(!parser.had_error, "no-using clause: no parse error");
    ASSERT(prog != NULL, "no-using clause: prog not null");
    ASSERT(prog->contract->sanitizers != NULL, "no-using clause: has sanitizers");
    ASSERT(prog->contract->sanitizers->impl_name == NULL,
           "no-using clause: impl_name is null");
    TEARDOWN();
}

/* ── Main ─────────────────────────────────────────────────── */

int main(void) {
    printf("test_parser:\n");

    /* Original tests */
    test_minimal_program();
    test_literals();
    test_identifiers_and_refs();
    test_binary_arithmetic();
    test_precedence();
    test_unary_neg();
    test_comparison();
    test_boolean_ops();
    test_if_else();
    test_match_when();
    test_let_chaining();
    test_filter_map();
    test_through();
    test_patterns();
    test_list_pattern();
    test_record_pattern();
    test_types();
    test_contract_sections();
    test_accessor_chain();
    test_function_call();
    test_comments_skipped();
    test_error_missing_end();
    test_error_invalid_token();
    test_keywords_as_names();
    test_require_rule();
    test_match_rule();

    /* High-priority gap tests */
    test_forbid_field_rule();
    test_guard_expression();
    test_require_without_reject();
    test_match_rule_without_reject();
    test_and_or_precedence();
    test_left_associativity();
    test_all_comparison_ops();
    test_negative_pattern();
    test_through_filter();
    test_through_map();
    test_if_without_else_error();

    /* Using keyword in sanitizers */
    test_sanitizer_using_clause();
    test_sanitizer_without_using();

    /* Medium-priority gap tests */
    test_deep_accessor_chain();
    test_anonymous_rest_pattern();
    test_empty_record_pattern();
    test_multiple_uses();
    test_tag_without_description();
    test_multiple_tagged_annotations();
    test_chained_through();
    test_empty_record_literal();
    test_empty_list_literal();
    test_call_with_empty_arg();
    test_require_with_input_ref();
    test_all_binary_ops();
    test_nested_maybe_list_types();
    test_multiple_test_cases();
    test_field_ref_deep_accessor();

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
