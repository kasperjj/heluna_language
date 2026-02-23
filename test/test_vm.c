/*
 * test_vm: unit tests for the Heluna bytecode VM.
 *
 * Each test compiles an inline source string, loads the packet,
 * executes it with a JSON input, and verifies the output.
 */

#include "heluna/vm.h"
#include "heluna/compiler.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/json.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

/* Compile, load, and execute a Heluna source with JSON input.
 * Returns the output HVal, or NULL on error.
 * Sets *out_arena to the arena that must be destroyed by the caller. */
static HVal *compile_and_run(const char *source, const char *json_input,
                             Arena **out_arena) {
    Arena *arena = arena_create(64 * 1024);
    *out_arena = arena;

    /* Parse */
    Lexer lex;
    lexer_init(&lex, source, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    if (parser.had_error || !prog) return NULL;

    /* Check */
    Checker checker;
    checker_init(&checker, prog, arena);
    if (checker_check(&checker) > 0) return NULL;

    /* Compile */
    Compiler compiler;
    compiler_init(&compiler, prog, arena);
    PacketResult packet = compiler_compile(&compiler);
    if (!packet.data || packet.size == 0) return NULL;

    /* Load packet */
    HelunaError load_err = {0};
    VmPacket *vmpkt = vm_load_packet(packet.data, packet.size,
                                     arena, &load_err);
    if (!vmpkt) return NULL;

    /* Parse input JSON */
    HelunaError json_err = {0};
    HVal *input = json_parse(arena, json_input, &json_err);
    if (!input) return NULL;

    /* Execute */
    Vm vm;
    vm_init(&vm, vmpkt, arena);
    return vm_execute(&vm, input);
}

/* Helper: get a field from a record result */
static HVal *get_field(HVal *rec, const char *name) {
    if (!rec || rec->kind != VAL_RECORD) return NULL;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return f->value;
    }
    return NULL;
}

/* ── Packet loading tests ────────────────────────────────── */

static void test_load_valid_packet(void) {
    Arena *arena;
    const char *src =
        "contract t\n"
        "  input a as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a }\n"
        "end\n";

    Lexer lex;
    arena = arena_create(64 * 1024);
    lexer_init(&lex, src, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    ASSERT(prog != NULL, "parse ok");

    Checker checker;
    checker_init(&checker, prog, arena);
    checker_check(&checker);

    Compiler compiler;
    compiler_init(&compiler, prog, arena);
    PacketResult packet = compiler_compile(&compiler);
    ASSERT(packet.data != NULL, "packet produced");

    HelunaError err = {0};
    VmPacket *vmpkt = vm_load_packet(packet.data, packet.size, arena, &err);
    ASSERT(vmpkt != NULL, "packet loaded");
    ASSERT(vmpkt->scratchpad_size > 0, "scratchpad > 0");
    ASSERT(vmpkt->input_count == 1, "1 input field");
    ASSERT(vmpkt->output_count == 1, "1 output field");
    ASSERT(vmpkt->instr_count > 0, "has instructions");
    ASSERT(strcmp(vmpkt->name, "t") == 0, "contract name = t");

    arena_destroy(arena);
}

static void test_load_bad_magic(void) {
    Arena *arena = arena_create(4096);
    uint8_t bad[88];
    memset(bad, 0, sizeof(bad));
    bad[0] = 0xFF; /* wrong magic */

    HelunaError err = {0};
    VmPacket *pkt = vm_load_packet(bad, sizeof(bad), arena, &err);
    ASSERT(pkt == NULL, "bad magic rejected");
    ASSERT(err.kind != HELUNA_OK, "error set");

    arena_destroy(arena);
}

static void test_load_too_small(void) {
    Arena *arena = arena_create(4096);
    uint8_t tiny[10];
    memset(tiny, 0, sizeof(tiny));

    HelunaError err = {0};
    VmPacket *pkt = vm_load_packet(tiny, sizeof(tiny), arena, &err);
    ASSERT(pkt == NULL, "too-small rejected");

    arena_destroy(arena);
}

static void test_load_constants(void) {
    Arena *arena;
    const char *src =
        "contract t\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: \"hello\" }\n"
        "end\n";

    Lexer lex;
    arena = arena_create(64 * 1024);
    lexer_init(&lex, src, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    Checker checker;
    checker_init(&checker, prog, arena);
    checker_check(&checker);
    Compiler compiler;
    compiler_init(&compiler, prog, arena);
    PacketResult packet = compiler_compile(&compiler);

    HelunaError err = {0};
    VmPacket *vmpkt = vm_load_packet(packet.data, packet.size, arena, &err);
    ASSERT(vmpkt != NULL, "packet loaded");
    ASSERT(vmpkt->constant_count > 0, "has constants");

    /* Find the "hello" constant */
    int found = 0;
    for (int i = 0; i < vmpkt->constant_count; i++) {
        if (vmpkt->constants[i].value &&
            vmpkt->constants[i].value->kind == VAL_STRING &&
            strcmp(vmpkt->constants[i].value->as.string_val, "hello") == 0) {
            found = 1;
        }
    }
    ASSERT(found, "hello constant found");

    arena_destroy(arena);
}

static void test_load_stdlib_deps(void) {
    Arena *arena;
    const char *src =
        "contract t\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x through trim({}) through lower({}) }\n"
        "end\n";

    Lexer lex;
    arena = arena_create(64 * 1024);
    lexer_init(&lex, src, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    Checker checker;
    checker_init(&checker, prog, arena);
    checker_check(&checker);
    Compiler compiler;
    compiler_init(&compiler, prog, arena);
    PacketResult packet = compiler_compile(&compiler);

    HelunaError err = {0};
    VmPacket *vmpkt = vm_load_packet(packet.data, packet.size, arena, &err);
    ASSERT(vmpkt != NULL, "packet loaded");
    ASSERT(vmpkt->stdlib_dep_count >= 2, "at least 2 stdlib deps");

    arena_destroy(arena);
}

/* ── Integer arithmetic ──────────────────────────────────── */

static void test_integer_add(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a + $b }\n"
        "end\n",
        "{\"a\":3,\"b\":4}", &arena);
    ASSERT(result != NULL, "add: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 7, "3+4=7");
    arena_destroy(arena);
}

static void test_integer_sub(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a - $b }\n"
        "end\n",
        "{\"a\":10,\"b\":3}", &arena);
    ASSERT(result != NULL, "sub: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 7, "10-3=7");
    arena_destroy(arena);
}

static void test_integer_mul(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a * $b }\n"
        "end\n",
        "{\"a\":5,\"b\":6}", &arena);
    ASSERT(result != NULL, "mul: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 30, "5*6=30");
    arena_destroy(arena);
}

static void test_integer_div(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a / $b }\n"
        "end\n",
        "{\"a\":15,\"b\":4}", &arena);
    ASSERT(result != NULL, "div: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 3, "15/4=3");
    arena_destroy(arena);
}

static void test_integer_mod(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a mod $b }\n"
        "end\n",
        "{\"a\":17,\"b\":5}", &arena);
    ASSERT(result != NULL, "mod: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 2, "17 mod 5=2");
    arena_destroy(arena);
}

/* ── String concatenation ────────────────────────────────── */

static void test_string_concat(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input first as string, last as string end\n"
        "  output name as string end\n"
        "end\n"
        "define t with input\n"
        "  result { name: $first + \" \" + $last }\n"
        "end\n",
        "{\"first\":\"John\",\"last\":\"Doe\"}", &arena);
    ASSERT(result != NULL, "concat: result");
    HVal *name = get_field(result, "name");
    ASSERT(name && name->kind == VAL_STRING &&
           strcmp(name->as.string_val, "John Doe") == 0, "concat ok");
    arena_destroy(arena);
}

/* ── Comparison operators ────────────────────────────────── */

static void test_comparison_ops(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output lt as boolean, gt as boolean, eq as boolean,\n"
        "         lte as boolean, gte as boolean, neq as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    lt: $a < $b,\n"
        "    gt: $a > $b,\n"
        "    eq: $a = $b,\n"
        "    lte: $a <= $b,\n"
        "    gte: $a >= $b,\n"
        "    neq: $a != $b\n"
        "  }\n"
        "end\n",
        "{\"a\":3,\"b\":5}", &arena);
    ASSERT(result != NULL, "cmp: result");
    HVal *lt = get_field(result, "lt");
    ASSERT(lt && lt->kind == VAL_BOOLEAN && lt->as.boolean_val == 1, "3<5");
    HVal *gt = get_field(result, "gt");
    ASSERT(gt && gt->kind == VAL_BOOLEAN && gt->as.boolean_val == 0, "3>5=false");
    HVal *eq = get_field(result, "eq");
    ASSERT(eq && eq->kind == VAL_BOOLEAN && eq->as.boolean_val == 0, "3=5=false");
    HVal *neq = get_field(result, "neq");
    ASSERT(neq && neq->kind == VAL_BOOLEAN && neq->as.boolean_val == 1, "3!=5");
    arena_destroy(arena);
}

/* ── Boolean operators ───────────────────────────────────── */

static void test_boolean_ops(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as boolean, b as boolean end\n"
        "  output and-r as boolean, or-r as boolean, not-r as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    and-r: $a and $b,\n"
        "    or-r: $a or $b,\n"
        "    not-r: not $a\n"
        "  }\n"
        "end\n",
        "{\"a\":true,\"b\":false}", &arena);
    ASSERT(result != NULL, "bool: result");
    HVal *and_r = get_field(result, "and-r");
    ASSERT(and_r && and_r->kind == VAL_BOOLEAN && and_r->as.boolean_val == 0,
           "true and false = false");
    HVal *or_r = get_field(result, "or-r");
    ASSERT(or_r && or_r->kind == VAL_BOOLEAN && or_r->as.boolean_val == 1,
           "true or false = true");
    HVal *not_r = get_field(result, "not-r");
    ASSERT(not_r && not_r->kind == VAL_BOOLEAN && not_r->as.boolean_val == 0,
           "not true = false");
    arena_destroy(arena);
}

/* ── If/else ─────────────────────────────────────────────── */

static void test_if_else(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input age as integer end\n"
        "  output label as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    label: if $age >= 18 then \"adult\" else \"minor\" end\n"
        "  }\n"
        "end\n",
        "{\"age\":25}", &arena);
    ASSERT(result != NULL, "if: result");
    HVal *label = get_field(result, "label");
    ASSERT(label && label->kind == VAL_STRING &&
           strcmp(label->as.string_val, "adult") == 0, "25=adult");
    arena_destroy(arena);

    result = compile_and_run(
        "contract t\n"
        "  input age as integer end\n"
        "  output label as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    label: if $age >= 18 then \"adult\" else \"minor\" end\n"
        "  }\n"
        "end\n",
        "{\"age\":10}", &arena);
    ASSERT(result != NULL, "if: result2");
    label = get_field(result, "label");
    ASSERT(label && label->kind == VAL_STRING &&
           strcmp(label->as.string_val, "minor") == 0, "10=minor");
    arena_destroy(arena);
}

/* ── Let binding ─────────────────────────────────────────── */

static void test_let_binding(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let x be $a + $b\n"
        "  result { y: x * 2 }\n"
        "end\n",
        "{\"a\":3,\"b\":4}", &arena);
    ASSERT(result != NULL, "let: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 14,
           "(3+4)*2=14");
    arena_destroy(arena);
}

/* ── Record construction ─────────────────────────────────── */

static void test_record_construction(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer, b as string end\n"
        "  output r as record x as integer, y as string end end\n"
        "end\n"
        "define t with input\n"
        "  result { r: { x: $a, y: $b } }\n"
        "end\n",
        "{\"a\":42,\"b\":\"hello\"}", &arena);
    ASSERT(result != NULL, "rec: result");
    HVal *r = get_field(result, "r");
    ASSERT(r && r->kind == VAL_RECORD, "rec: is record");
    HVal *x = get_field(r, "x");
    ASSERT(x && x->kind == VAL_INTEGER && x->as.integer_val == 42, "rec.x=42");
    HVal *y = get_field(r, "y");
    ASSERT(y && y->kind == VAL_STRING && strcmp(y->as.string_val, "hello") == 0,
           "rec.y=hello");
    arena_destroy(arena);
}

/* ── List construction ───────────────────────────────────── */

static void test_list_construction(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: [1, 2, 3] }\n"
        "end\n",
        "{\"x\":0}", &arena);
    ASSERT(result != NULL, "list: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_LIST, "list: is list");
    /* Check elements */
    HVal *e = y->as.list_head;
    ASSERT(e && e->kind == VAL_INTEGER && e->as.integer_val == 1, "list[0]=1");
    e = e->next;
    ASSERT(e && e->kind == VAL_INTEGER && e->as.integer_val == 2, "list[1]=2");
    e = e->next;
    ASSERT(e && e->kind == VAL_INTEGER && e->as.integer_val == 3, "list[2]=3");
    arena_destroy(arena);
}

/* ── Type testing ────────────────────────────────────────── */

static void test_type_testing(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input v as maybe integer end\n"
        "  output is-int as boolean, is-nothing as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    is-int: $v is integer,\n"
        "    is-nothing: $v is nothing\n"
        "  }\n"
        "end\n",
        "{\"v\":42}", &arena);
    ASSERT(result != NULL, "type: result");
    HVal *is_int = get_field(result, "is-int");
    ASSERT(is_int && is_int->as.boolean_val == 1, "42 is integer");
    HVal *is_nothing = get_field(result, "is-nothing");
    ASSERT(is_nothing && is_nothing->as.boolean_val == 0, "42 not nothing");
    arena_destroy(arena);
}

/* ── Nothing/coalesce ────────────────────────────────────── */

static void test_nothing_coalesce(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input v as maybe integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $v or else 42 }\n"
        "end\n",
        "{\"v\":null}", &arena);
    ASSERT(result != NULL, "coalesce: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_INTEGER && y->as.integer_val == 42,
           "nothing or else 42 = 42");
    arena_destroy(arena);
}

/* ── Float promotion ─────────────────────────────────────── */

static void test_float_promotion(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input a as integer end\n"
        "  output y as float end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a + 1.5 }\n"
        "end\n",
        "{\"a\":3}", &arena);
    ASSERT(result != NULL, "float: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_FLOAT && fabs(y->as.float_val - 4.5) < 0.001,
           "3+1.5=4.5");
    arena_destroy(arena);
}

/* ── Match expression ────────────────────────────────────── */

static void test_match_literal(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input v as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    y: match $v\n"
        "      when 1 then \"one\"\n"
        "      when 2 then \"two\"\n"
        "      else \"other\"\n"
        "    end\n"
        "  }\n"
        "end\n",
        "{\"v\":2}", &arena);
    ASSERT(result != NULL, "match: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_STRING &&
           strcmp(y->as.string_val, "two") == 0, "match 2=two");
    arena_destroy(arena);
}

/* ── Iteration: filter ───────────────────────────────────── */

static void test_filter(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input vals as list of integer end\n"
        "  output evens as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    evens: filter $vals where n n > 2 end\n"
        "  }\n"
        "end\n",
        "{\"vals\":[1,2,3,4]}", &arena);
    ASSERT(result != NULL, "filter: result");
    HVal *evens = get_field(result, "evens");
    ASSERT(evens && evens->kind == VAL_LIST, "filter: is list");
    HVal *e = evens->as.list_head;
    ASSERT(e && e->as.integer_val == 3, "filter[0]=3");
    e = e->next;
    ASSERT(e && e->as.integer_val == 4, "filter[1]=4");
    ASSERT(e->next == NULL, "filter: 2 elements");
    arena_destroy(arena);
}

/* ── Iteration: map ──────────────────────────────────────── */

static void test_map(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input vals as list of integer end\n"
        "  output doubled as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    doubled: map $vals as n do n * 2 end\n"
        "  }\n"
        "end\n",
        "{\"vals\":[1,2,3]}", &arena);
    ASSERT(result != NULL, "map: result");
    HVal *doubled = get_field(result, "doubled");
    ASSERT(doubled && doubled->kind == VAL_LIST, "map: is list");
    HVal *e = doubled->as.list_head;
    ASSERT(e && e->as.integer_val == 2, "map[0]=2");
    e = e->next;
    ASSERT(e && e->as.integer_val == 4, "map[1]=4");
    e = e->next;
    ASSERT(e && e->as.integer_val == 6, "map[2]=6");
    arena_destroy(arena);
}

/* ── Iteration: fold ─────────────────────────────────────── */

static void test_fold_add(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input vals as list of integer end\n"
        "  output total as integer end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    total: fold({ list: $vals, initial: 0, fn: \"add\" })\n"
        "  }\n"
        "end\n",
        "{\"vals\":[1,2,3]}", &arena);
    ASSERT(result != NULL, "fold-add: result");
    HVal *total = get_field(result, "total");
    ASSERT(total && total->kind == VAL_INTEGER && total->as.integer_val == 6,
           "fold add [1,2,3] = 6");
    arena_destroy(arena);
}

static void test_fold_multiply(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input vals as list of integer end\n"
        "  output product as integer end\n"
        "end\n"
        "define t with input\n"
        "  result {\n"
        "    product: fold({ list: $vals, initial: 1, fn: \"multiply\" })\n"
        "  }\n"
        "end\n",
        "{\"vals\":[1,2,3,4]}", &arena);
    ASSERT(result != NULL, "fold-mul: result");
    HVal *product = get_field(result, "product");
    ASSERT(product && product->kind == VAL_INTEGER &&
           product->as.integer_val == 24, "fold multiply [1,2,3,4] = 24");
    arena_destroy(arena);
}

/* ── Through pipeline ────────────────────────────────────── */

static void test_through_pipeline(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input s as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $s through trim({}) through lower({}) }\n"
        "end\n",
        "{\"s\":\"  HELLO  \"}", &arena);
    ASSERT(result != NULL, "through: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_STRING &&
           strcmp(y->as.string_val, "hello") == 0,
           "through trim+lower ok");
    arena_destroy(arena);
}

/* ── Stdlib function tests ───────────────────────────────── */

static void test_stdlib_upper(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input s as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $s through upper({}) }\n"
        "end\n",
        "{\"s\":\"hello\"}", &arena);
    ASSERT(result != NULL, "upper: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_STRING &&
           strcmp(y->as.string_val, "HELLO") == 0, "upper ok");
    arena_destroy(arena);
}

static void test_stdlib_replace(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input s as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $s through replace({ find: \"world\", replacement: \"there\" }) }\n"
        "end\n",
        "{\"s\":\"hello world\"}", &arena);
    ASSERT(result != NULL, "replace: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_STRING &&
           strcmp(y->as.string_val, "hello there") == 0, "replace ok");
    arena_destroy(arena);
}

static void test_stdlib_sort(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input vals as list of integer end\n"
        "  output sorted as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { sorted: sort({ list: $vals }) }\n"
        "end\n",
        "{\"vals\":[3,1,2]}", &arena);
    ASSERT(result != NULL, "sort: result");
    HVal *sorted = get_field(result, "sorted");
    ASSERT(sorted && sorted->kind == VAL_LIST, "sort: is list");
    HVal *e = sorted->as.list_head;
    ASSERT(e && e->as.integer_val == 1, "sort[0]=1");
    e = e->next;
    ASSERT(e && e->as.integer_val == 2, "sort[1]=2");
    e = e->next;
    ASSERT(e && e->as.integer_val == 3, "sort[2]=3");
    arena_destroy(arena);
}

static void test_stdlib_reverse(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input vals as list of integer end\n"
        "  output rev as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { rev: reverse({ list: $vals }) }\n"
        "end\n",
        "{\"vals\":[1,2,3]}", &arena);
    ASSERT(result != NULL, "reverse: result");
    HVal *rev = get_field(result, "rev");
    ASSERT(rev && rev->kind == VAL_LIST, "reverse: is list");
    HVal *e = rev->as.list_head;
    ASSERT(e && e->as.integer_val == 3, "rev[0]=3");
    e = e->next;
    ASSERT(e && e->as.integer_val == 2, "rev[1]=2");
    e = e->next;
    ASSERT(e && e->as.integer_val == 1, "rev[2]=1");
    arena_destroy(arena);
}

static void test_stdlib_base64_encode(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input s as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: base64-encode({ value: $s }) }\n"
        "end\n",
        "{\"s\":\"hello\"}", &arena);
    ASSERT(result != NULL, "b64: result");
    HVal *y = get_field(result, "y");
    ASSERT(y && y->kind == VAL_STRING &&
           strcmp(y->as.string_val, "aGVsbG8=") == 0, "b64 encode ok");
    arena_destroy(arena);
}

static void test_stdlib_sha256(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input s as string end\n"
        "  output h as string end\n"
        "end\n"
        "define t with input\n"
        "  result { h: sha256({ value: $s }) }\n"
        "end\n",
        "{\"s\":\"hello\"}", &arena);
    ASSERT(result != NULL, "sha256: result");
    HVal *h = get_field(result, "h");
    ASSERT(h && h->kind == VAL_STRING &&
           strcmp(h->as.string_val,
                  "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")
                  == 0, "sha256 hello ok");
    arena_destroy(arena);
}

static void test_stdlib_range(void) {
    Arena *arena;
    HVal *result = compile_and_run(
        "contract t\n"
        "  input s as integer, e as integer end\n"
        "  output r as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { r: range({ start: $s, end: $e }) }\n"
        "end\n",
        "{\"s\":1,\"e\":5}", &arena);
    ASSERT(result != NULL, "range: result");
    HVal *r = get_field(result, "r");
    ASSERT(r && r->kind == VAL_LIST, "range: is list");
    int count = 0;
    long long expect = 1;
    for (HVal *v = r->as.list_head; v; v = v->next) {
        ASSERT(v->kind == VAL_INTEGER && v->as.integer_val == expect,
               "range element");
        expect++;
        count++;
    }
    ASSERT(count == 5, "range 1..5 = 5 elements");
    arena_destroy(arena);
}

/* ── Tag propagation ─────────────────────────────────────── */

static void test_tag_copy(void) {
    Arena *arena;
    /* Tags should propagate through COPY */
    const char *src =
        "contract t\n"
        "  tags secret \"s\" end\n"
        "  input a as string tagged secret end\n"
        "  output y as string end\n"
        "  rules forbid tagged secret in output end\n"
        "end\n"
        "define t with input\n"
        "  let x be $a\n"
        "  result { y: x }\n"
        "end\n";

    /* This should compile fine — tag checking is done at compile-time
     * by the checker, not at VM runtime. We just verify the program
     * runs without error. */
    HVal *result = compile_and_run(src, "{\"a\":\"secret-data\"}", &arena);
    ASSERT(result != NULL, "tag-copy: result");
    arena_destroy(arena);
}

/* ── Main ────────────────────────────────────────────────── */

int main(void) {
    printf("test_vm:\n");

    /* Packet loading */
    test_load_valid_packet();
    test_load_bad_magic();
    test_load_too_small();
    test_load_constants();
    test_load_stdlib_deps();

    /* Arithmetic */
    test_integer_add();
    test_integer_sub();
    test_integer_mul();
    test_integer_div();
    test_integer_mod();
    test_float_promotion();

    /* String */
    test_string_concat();

    /* Comparison & boolean */
    test_comparison_ops();
    test_boolean_ops();

    /* Control flow */
    test_if_else();
    test_match_literal();

    /* Binding */
    test_let_binding();

    /* Data structures */
    test_record_construction();
    test_list_construction();

    /* Type testing */
    test_type_testing();

    /* Nothing */
    test_nothing_coalesce();

    /* Iteration */
    test_filter();
    test_map();
    test_fold_add();
    test_fold_multiply();

    /* Through pipeline */
    test_through_pipeline();

    /* Stdlib */
    test_stdlib_upper();
    test_stdlib_replace();
    test_stdlib_sort();
    test_stdlib_reverse();
    test_stdlib_base64_encode();
    test_stdlib_sha256();
    test_stdlib_range();

    /* Tags */
    test_tag_copy();

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
