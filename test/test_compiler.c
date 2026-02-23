/*
 * test_compiler: unit tests for the Heluna bytecode compiler.
 *
 * Each test creates an inline source string, parses and checks it,
 * then compiles it and asserts on the compiled output.
 */

#include "heluna/compiler.h"
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

/* Parse, check, and compile source. Sets up compiler and packet. */
#define COMPILE(src) \
    Arena *arena = arena_create(64 * 1024); \
    Lexer lex; \
    lexer_init(&lex, src, "test", arena); \
    Parser parser; \
    parser_init(&parser, &lex, arena); \
    AstProgram *prog = parser_parse(&parser); \
    ASSERT(!parser.had_error, "parse ok"); \
    ASSERT(prog != NULL, "prog not null"); \
    Compiler compiler; \
    PacketResult packet = { NULL, 0 }; \
    if (prog) { \
        Checker checker; \
        checker_init(&checker, prog, arena); \
        int nerrs = checker_check(&checker); \
        ASSERT(nerrs == 0, "check ok"); \
        if (nerrs == 0) { \
            compiler_init(&compiler, prog, arena); \
            packet = compiler_compile(&compiler); \
        } \
    }

#define TEARDOWN() arena_destroy(arena)

/* Read a little-endian uint32 from a byte buffer */
static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Read a little-endian uint16 from a byte buffer */
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Find an instruction by opcode in the compiler's instruction buffer */
static int find_instr(const Compiler *c, uint8_t opcode) {
    for (int i = 0; i < c->instr_count; i++) {
        if (c->instructions[i].opcode == opcode) return i;
    }
    return -1;
}

/* Count instructions with a given opcode */
static int count_instr(const Compiler *c, uint8_t opcode) {
    int count = 0;
    for (int i = 0; i < c->instr_count; i++) {
        if (c->instructions[i].opcode == opcode) count++;
    }
    return count;
}

/* ── Scratchpad allocation tests ─────────────────────────── */

static void test_scratchpad_input_slots(void) {
    COMPILE(
        "contract t\n"
        "  input a as integer, b as string end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    /* 2 inputs + 1 output = slots 0,1,2; working starts at 3 */
    ASSERT(compiler.input_count == 2, "2 input slots");
    ASSERT(compiler.output_count == 1, "1 output slot");
    ASSERT(compiler.scratch_next >= 3, "scratch_next >= 3");
    TEARDOWN();
}

static void test_scratchpad_outputs_after_inputs(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output a as integer, b as string end\n"
        "end\n"
        "define t with input\n"
        "  result { a: $x, b: \"hi\" }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(compiler.input_count == 1, "1 input");
    ASSERT(compiler.output_count == 2, "2 outputs");
    /* input slot 0, output slots 1,2 */
    ASSERT(compiler.scratch_next >= 3, "scratch at least 3");
    TEARDOWN();
}

/* ── Constant pool tests ─────────────────────────────────── */

static void test_const_string_dedup(void) {
    COMPILE(
        "contract t\n"
        "  input x as string end\n"
        "  output a as string, b as string end\n"
        "end\n"
        "define t with input\n"
        "  result { a: \"hello\", b: \"hello\" }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    /* "hello" should be deduplicated, plus "a" and "b" keys */
    /* Count LOAD_CONST instructions that load the same const_idx */
    int hello_count = 0;
    uint16_t hello_idx = 0;
    for (int i = 0; i < compiler.instr_count; i++) {
        if (compiler.instructions[i].opcode == OP_LOAD_CONST) {
            /* Check if this is a string constant containing "hello" */
            uint16_t ci = compiler.instructions[i].operand1;
            if (ci < (uint16_t)compiler.const_count &&
                compiler.const_types[ci] == FTYPE_STRING &&
                compiler.const_lengths[ci] == 5) {
                if (memcmp(compiler.const_data + compiler.const_offsets[ci],
                          "hello", 5) == 0) {
                    if (hello_count == 0) hello_idx = ci;
                    else ASSERT(ci == hello_idx, "deduped same index");
                    hello_count++;
                }
            }
        }
    }
    ASSERT(hello_count >= 2, "hello loaded at least twice");
    TEARDOWN();
}

static void test_const_integer(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: 42 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    /* Should have an integer constant 42 */
    int found = 0;
    for (int i = 0; i < compiler.const_count; i++) {
        if (compiler.const_types[i] == FTYPE_INTEGER) {
            long long val;
            memcpy(&val, compiler.const_data + compiler.const_offsets[i], 8);
            if (val == 42) found = 1;
        }
    }
    ASSERT(found, "integer constant 42 in pool");
    TEARDOWN();
}

static void test_const_float(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as float end\n"
        "end\n"
        "define t with input\n"
        "  result { y: 3.14 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    int found = 0;
    for (int i = 0; i < compiler.const_count; i++) {
        if (compiler.const_types[i] == FTYPE_FLOAT) {
            double val;
            memcpy(&val, compiler.const_data + compiler.const_offsets[i], 8);
            if (val > 3.13 && val < 3.15) found = 1;
        }
    }
    ASSERT(found, "float constant 3.14 in pool");
    TEARDOWN();
}

static void test_const_boolean(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { y: true }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    int found = 0;
    for (int i = 0; i < compiler.const_count; i++) {
        if (compiler.const_types[i] == FTYPE_BOOLEAN &&
            compiler.const_data[compiler.const_offsets[i]] == 1) {
            found = 1;
        }
    }
    ASSERT(found, "boolean true in pool");
    TEARDOWN();
}

/* ── Literal compilation tests ───────────────────────────── */

static void test_load_nothing(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as maybe integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: nothing }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_LOAD_NOTHING) >= 0, "LOAD_NOTHING emitted");
    TEARDOWN();
}

static void test_load_const_string(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: \"hello world\" }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_LOAD_CONST) >= 0, "LOAD_CONST emitted");
    TEARDOWN();
}

/* ── Binary operation tests ──────────────────────────────── */

static void test_add_integers(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x + 1 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_ADD) >= 0, "ADD emitted for integer+integer");
    TEARDOWN();
}

static void test_str_concat(void) {
    COMPILE(
        "contract t\n"
        "  input a as string, b as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a + $b }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_STR_CONCAT) >= 0,
           "STR_CONCAT emitted for string+string");
    TEARDOWN();
}

static void test_str_concat_mixed(void) {
    COMPILE(
        "contract t\n"
        "  input a as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a + \" world\" }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_STR_CONCAT) >= 0,
           "STR_CONCAT emitted for string + string literal");
    TEARDOWN();
}

static void test_arithmetic_ops(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output a as integer, b as integer, c as integer, d as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { a: $x - 1, b: $x * 2, c: $x / 3, d: $x % 4 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_SUB) >= 0, "SUB emitted");
    ASSERT(find_instr(&compiler, OP_MUL) >= 0, "MUL emitted");
    ASSERT(find_instr(&compiler, OP_DIV) >= 0, "DIV emitted");
    ASSERT(find_instr(&compiler, OP_MOD) >= 0, "MOD emitted");
    TEARDOWN();
}

static void test_comparison_ops(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output a as boolean, b as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { a: $x < 10, b: $x >= 5 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_LT) >= 0, "LT emitted");
    ASSERT(find_instr(&compiler, OP_GTE) >= 0, "GTE emitted");
    TEARDOWN();
}

static void test_boolean_ops(void) {
    COMPILE(
        "contract t\n"
        "  input a as boolean, b as boolean end\n"
        "  output y as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $a and $b }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_AND) >= 0, "AND emitted");
    TEARDOWN();
}

static void test_not_op(void) {
    COMPILE(
        "contract t\n"
        "  input a as boolean end\n"
        "  output y as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { y: not $a }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_NOT) >= 0, "NOT emitted");
    TEARDOWN();
}

static void test_negate(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: -$x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_NEGATE) >= 0, "NEGATE emitted");
    TEARDOWN();
}

/* ── Control flow tests ──────────────────────────────────── */

static void test_if_else(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: if $x > 0 then \"positive\" else \"non-positive\" end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_JUMP_IF_NOT) >= 0, "JUMP_IF_NOT for if");
    ASSERT(find_instr(&compiler, OP_JUMP) >= 0, "JUMP for else");
    TEARDOWN();
}

static void test_match_literal(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when 1 then \"one\" when 2 then \"two\" "
        "else \"other\" end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_EQ) >= 0, "EQ for literal pattern");
    ASSERT(count_instr(&compiler, OP_JUMP_IF_NOT) >= 2,
           "JUMP_IF_NOT for each when clause");
    TEARDOWN();
}

static void test_match_wildcard(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when _ then \"any\" end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    /* Wildcard should not emit EQ */
    ASSERT(find_instr(&compiler, OP_EQ) < 0, "no EQ for wildcard");
    TEARDOWN();
}

static void test_match_binding(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when val then val end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    /* Binding should reference the subject slot */
    ASSERT(compiler.errors.count == 0, "no compile errors");
    TEARDOWN();
}

static void test_match_range(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: match $x when between 1 and 10 then \"small\" else \"big\" end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_GTE) >= 0, "GTE for range pattern");
    ASSERT(find_instr(&compiler, OP_LTE) >= 0, "LTE for range pattern");
    TEARDOWN();
}

/* ── Let binding tests ───────────────────────────────────── */

static void test_let_binding(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  let doubled be $x * 2\n"
        "  result { y: doubled }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_MUL) >= 0, "MUL for doubling");
    ASSERT(find_instr(&compiler, OP_COPY) >= 0, "COPY for let reference");
    TEARDOWN();
}

/* ── Record/list construction tests ──────────────────────── */

static void test_record_new(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_RECORD_NEW) >= 0, "RECORD_NEW emitted");
    ASSERT(find_instr(&compiler, OP_RECORD_SET) >= 0, "RECORD_SET emitted");
    TEARDOWN();
}

static void test_list_new(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: [1, 2, 3] }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_LIST_NEW) >= 0, "LIST_NEW emitted");
    ASSERT(count_instr(&compiler, OP_LIST_APPEND) == 3,
           "3 LIST_APPEND instructions");
    TEARDOWN();
}

/* ── Function call tests ─────────────────────────────────── */

static void test_to_string_opcode(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: to-string({ value: $x }).value }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_TO_STRING) >= 0,
           "TO_STRING opcode for to-string()");
    ASSERT(find_instr(&compiler, OP_STDLIB_CALL) < 0,
           "no STDLIB_CALL for to-string");
    TEARDOWN();
}

static void test_stdlib_call(void) {
    COMPILE(
        "contract t\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: upper({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_STDLIB_CALL) >= 0, "STDLIB_CALL emitted");
    /* Check stdlib deps tracking */
    int found_upper = 0;
    for (int i = 0; i < compiler.stdlib_dep_count; i++) {
        if (compiler.stdlib_deps[i] == 0x0001) found_upper = 1;
    }
    ASSERT(found_upper, "upper (0x0001) tracked in stdlib deps");
    TEARDOWN();
}

/* ── Iteration tests ─────────────────────────────────────── */

static void test_filter(void) {
    COMPILE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output y as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: filter $xs where n n > 0 end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_ITER_SETUP) >= 0, "ITER_SETUP for filter");
    ASSERT(find_instr(&compiler, OP_ITER_COLLECT) >= 0,
           "ITER_COLLECT for filter");

    /* Check ITER_SETUP has FILTER mode */
    int idx = find_instr(&compiler, OP_ITER_SETUP);
    uint8_t mode = compiler.instructions[idx].flags & 0x03;
    ASSERT(mode == ITER_FILTER, "ITER_SETUP mode is FILTER");
    TEARDOWN();
}

static void test_map(void) {
    COMPILE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output y as list of integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: map $xs as n do n * 2 end }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_ITER_SETUP) >= 0, "ITER_SETUP for map");
    int idx = find_instr(&compiler, OP_ITER_SETUP);
    uint8_t mode = compiler.instructions[idx].flags & 0x03;
    ASSERT(mode == ITER_MAP, "ITER_SETUP mode is MAP");
    TEARDOWN();
}

static void test_fold(void) {
    COMPILE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: fold({ list: $xs, initial: 0, fn: \"add\" }) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_ITER_SETUP) >= 0, "ITER_SETUP for fold");

    int idx = find_instr(&compiler, OP_ITER_SETUP);
    uint8_t mode = compiler.instructions[idx].flags & 0x03;
    ASSERT(mode == ITER_FOLD, "ITER_SETUP mode is FOLD");

    /* Fold body should contain ADD */
    ASSERT(find_instr(&compiler, OP_ADD) >= 0, "ADD in fold body");
    TEARDOWN();
}

/* ── Packet structure tests ──────────────────────────────── */

static void test_packet_magic(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(packet.size >= 88, "packet at least 88 bytes");

    uint32_t magic = read_u32(packet.data);
    ASSERT(magic == PACKET_MAGIC, "magic is 0x484C4E41");
    TEARDOWN();
}

static void test_packet_format_version(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    uint16_t ver = read_u16(packet.data + 4);
    ASSERT(ver == 1, "format_version is 1");
    TEARDOWN();
}

static void test_packet_total_size(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    uint32_t total = read_u32(packet.data + 8);
    ASSERT(total == (uint32_t)packet.size, "total_size matches packet size");
    TEARDOWN();
}

static void test_packet_section_count(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    uint16_t count = read_u16(packet.data + 12);
    ASSERT(count == 4, "4 sections (CONTRACT, CONSTANTS, STDLIB_DEPS, BYTECODE)");
    TEARDOWN();
}

static void test_packet_all_sections_present(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");

    uint16_t count = read_u16(packet.data + 12);
    int has_contract = 0, has_constants = 0;
    int has_stdlib = 0, has_bytecode = 0;

    for (int i = 0; i < count; i++) {
        int off = 88 + i * 10;
        uint16_t sec_type = read_u16(packet.data + off);
        if (sec_type == SEC_CONTRACT)    has_contract = 1;
        if (sec_type == SEC_CONSTANTS)   has_constants = 1;
        if (sec_type == SEC_STDLIB_DEPS) has_stdlib = 1;
        if (sec_type == SEC_BYTECODE)    has_bytecode = 1;
    }

    ASSERT(has_contract,  "CONTRACT section present");
    ASSERT(has_constants, "CONSTANTS section present");
    ASSERT(has_stdlib,    "STDLIB_DEPS section present");
    ASSERT(has_bytecode,  "BYTECODE section present");
    TEARDOWN();
}

static void test_bytecode_section_alignment(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x + 1 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");

    /* Find bytecode section */
    uint16_t count = read_u16(packet.data + 12);
    for (int i = 0; i < count; i++) {
        int off = 88 + i * 10;
        uint16_t sec_type = read_u16(packet.data + off);
        if (sec_type == SEC_BYTECODE) {
            uint32_t sec_len = read_u32(packet.data + off + 6);
            ASSERT(sec_len % 8 == 0,
                   "bytecode section length is multiple of 8");
        }
    }
    TEARDOWN();
}

/* ── Through pipeline test ───────────────────────────────── */

static void test_through_call(void) {
    COMPILE(
        "contract t\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x through trim({}) through lower({}) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(count_instr(&compiler, OP_STDLIB_CALL) == 2,
           "2 STDLIB_CALL for trim+lower");
    TEARDOWN();
}

/* ── Access expression test ──────────────────────────────── */

static void test_record_access(void) {
    COMPILE(
        "contract t\n"
        "  input x as record\n"
        "    a as integer\n"
        "  end end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: $x.a }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_RECORD_GET) >= 0, "RECORD_GET emitted");
    TEARDOWN();
}

/* ── Tag contract rejection test ─────────────────────────── */

static void test_tag_contract_rejected(void) {
    COMPILE(
        "contract my-tags\n"
        "  tags\n"
        "    secret \"confidential\"\n"
        "  end\n"
        "end\n"
    );
    /* Tag contracts should fail compilation */
    ASSERT(packet.data == NULL, "tag contract produces no packet");
    ASSERT(compiler.errors.count > 0, "error reported for tag contract");
    TEARDOWN();
}

/* ── Empty record arg call test ──────────────────────────── */

static void test_call_empty_record(void) {
    COMPILE(
        "contract t\n"
        "  input x as string end\n"
        "  output y as string end\n"
        "end\n"
        "define t with input\n"
        "  result { y: trim({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_STDLIB_CALL) >= 0, "STDLIB_CALL emitted");
    TEARDOWN();
}

/* ── Fold multiply test ──────────────────────────────────── */

static void test_fold_multiply(void) {
    COMPILE(
        "contract t\n"
        "  input xs as list of integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: fold({ list: $xs, initial: 1, fn: \"multiply\" }) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_ITER_SETUP) >= 0, "ITER_SETUP for fold");
    ASSERT(find_instr(&compiler, OP_MUL) >= 0, "MUL in fold multiply body");
    TEARDOWN();
}

/* ── EQ/NEQ tests ────────────────────────────────────────── */

static void test_eq_neq(void) {
    COMPILE(
        "contract t\n"
        "  input x as integer end\n"
        "  output a as boolean, b as boolean end\n"
        "end\n"
        "define t with input\n"
        "  result { a: $x = 5, b: $x != 3 }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    ASSERT(find_instr(&compiler, OP_EQ) >= 0, "EQ emitted");
    ASSERT(find_instr(&compiler, OP_NEQ) >= 0, "NEQ emitted");
    TEARDOWN();
}

/* ── Sanitizer tag mode test ─────────────────────────────── */

static void test_sanitizer_clear_tag_mode(void) {
    COMPILE(
        "contract t\n"
        "  tags\n"
        "    secret \"confidential\"\n"
        "  end\n"
        "  sanitizers\n"
        "    sha256 strips secret\n"
        "  end\n"
        "  input x as string tagged secret end\n"
        "  output y as string end\n"
        "  rules\n"
        "    forbid tagged secret in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: sha256({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "packet produced");
    int idx = find_instr(&compiler, OP_STDLIB_CALL);
    ASSERT(idx >= 0, "STDLIB_CALL emitted");
    if (idx >= 0) {
        uint8_t tm = (compiler.instructions[idx].flags >> 3) & 0x03;
        ASSERT(tm == TMODE_CLEAR, "tag mode is CLEAR for sanitizer");
    }
    TEARDOWN();
}

/* ── Inlining tests ──────────────────────────────────────── */

/* Helper: parse + check a dependency program */
static AstProgram *parse_dep(Arena *arena, const char *src) {
    Lexer lex;
    lexer_init(&lex, src, "dep", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    if (parser.had_error || !prog) return NULL;
    Checker checker;
    checker_init(&checker, prog, arena);
    if (checker_check(&checker) > 0) return NULL;
    return prog;
}

static void test_inline_uses_call(void) {
    Arena *arena = arena_create(64 * 1024);

    /* Dependency: normalize-email trims and lowercases */
    const char *dep_src =
        "contract normalize-email\n"
        "  input email as string end\n"
        "  output email as string end\n"
        "end\n"
        "define normalize-email with input\n"
        "  result { email: $email through trim({}) through lower({}) }\n"
        "end\n";
    AstProgram *dep_prog = parse_dep(arena, dep_src);
    ASSERT(dep_prog != NULL, "inline: dep parsed");

    /* Main contract uses normalize-email */
    const char *main_src =
        "contract t\n"
        "  uses normalize-email\n"
        "  input email as string end\n"
        "  output email as string end\n"
        "end\n"
        "define t with input\n"
        "  result { email: normalize-email({ email: $email }).email }\n"
        "end\n";

    Lexer lex;
    lexer_init(&lex, main_src, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    ASSERT(!parser.had_error, "inline: main parsed");
    ASSERT(prog != NULL, "inline: prog not null");

    if (prog) {
        Checker checker;
        checker_init(&checker, prog, arena);
        int nerrs = checker_check(&checker);
        ASSERT(nerrs == 0, "inline: check ok");

        if (nerrs == 0) {
            CompilerDep deps[1];
            deps[0].name = "normalize-email";
            deps[0].prog = dep_prog;

            Compiler compiler;
            compiler_init_with_deps(&compiler, prog, arena, deps, 1);
            PacketResult packet = compiler_compile(&compiler);

            ASSERT(packet.data != NULL, "inline: packet produced");
            ASSERT(compiler.errors.count == 0, "inline: no errors");
            /* Should have RECORD_GET for extracting dep input fields */
            ASSERT(find_instr(&compiler, OP_RECORD_GET) >= 0,
                   "inline: RECORD_GET for field extraction");
            /* Should NOT have STDLIB_CALL with func_id=0 placeholder */
            int sc = find_instr(&compiler, OP_STDLIB_CALL);
            if (sc >= 0) {
                /* All STDLIB_CALLs should have non-zero func_id */
                int has_zero = 0;
                for (int i = 0; i < compiler.instr_count; i++) {
                    if (compiler.instructions[i].opcode == OP_STDLIB_CALL &&
                        compiler.instructions[i].operand1 == 0) {
                        has_zero = 1;
                    }
                }
                ASSERT(!has_zero, "inline: no func_id=0 placeholder");
            }
        }
    }

    arena_destroy(arena);
}

static void test_sanitizer_using_stdlib_compile(void) {
    COMPILE(
        "contract t\n"
        "  tags\n"
        "    secret \"confidential\"\n"
        "  end\n"
        "  sanitizers\n"
        "    hash using sha256 strips secret\n"
        "  end\n"
        "  input x as string tagged secret end\n"
        "  output y as string end\n"
        "  rules\n"
        "    forbid tagged secret in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: hash({ value: $x }) }\n"
        "end\n"
    );
    ASSERT(packet.data != NULL, "using-stdlib: packet produced");
    /* hash using sha256 → should resolve to stdlib sha256 (0x0070) */
    int idx = find_instr(&compiler, OP_STDLIB_CALL);
    ASSERT(idx >= 0, "using-stdlib: STDLIB_CALL emitted");
    if (idx >= 0) {
        ASSERT(compiler.instructions[idx].operand1 == 0x0070,
               "using-stdlib: func_id is sha256 (0x0070)");
        uint8_t tm = (compiler.instructions[idx].flags >> 3) & 0x03;
        ASSERT(tm == TMODE_CLEAR, "using-stdlib: tag mode is CLEAR");
    }
    TEARDOWN();
}

static void test_unresolved_call_error(void) {
    Arena *arena = arena_create(64 * 1024);

    const char *src =
        "contract t\n"
        "  uses missing-fn\n"
        "  input x as integer end\n"
        "  output y as integer end\n"
        "end\n"
        "define t with input\n"
        "  result { y: missing-fn({ value: $x }) }\n"
        "end\n";

    Lexer lex;
    lexer_init(&lex, src, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    ASSERT(!parser.had_error, "unresolved: parse ok");

    if (prog) {
        Checker checker;
        checker_init(&checker, prog, arena);
        checker_check(&checker);

        /* Compile without providing deps → should error */
        Compiler compiler;
        compiler_init(&compiler, prog, arena);
        PacketResult packet = compiler_compile(&compiler);

        ASSERT(packet.data == NULL, "unresolved: no packet");
        ASSERT(compiler.errors.count > 0, "unresolved: has errors");
    }
    arena_destroy(arena);
}

static void test_sanitizer_using_inline_dep(void) {
    Arena *arena = arena_create(64 * 1024);

    /* Dependency: a simple function */
    const char *dep_src =
        "contract my-hasher\n"
        "  input value as string end\n"
        "  output value as string end\n"
        "end\n"
        "define my-hasher with input\n"
        "  result { value: $value + \"-hashed\" }\n"
        "end\n";
    AstProgram *dep_prog = parse_dep(arena, dep_src);
    ASSERT(dep_prog != NULL, "san-inline: dep parsed");

    const char *main_src =
        "contract t\n"
        "  uses my-hasher\n"
        "  tags\n"
        "    secret \"confidential\"\n"
        "  end\n"
        "  sanitizers\n"
        "    sanitize using my-hasher strips secret\n"
        "  end\n"
        "  input x as string tagged secret end\n"
        "  output y as string end\n"
        "  rules\n"
        "    forbid tagged secret in output\n"
        "  end\n"
        "end\n"
        "define t with input\n"
        "  result { y: sanitize({ value: $x }) }\n"
        "end\n";

    Lexer lex;
    lexer_init(&lex, main_src, "test", arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);
    ASSERT(!parser.had_error, "san-inline: main parsed");

    if (prog) {
        Checker checker;
        checker_init(&checker, prog, arena);
        int nerrs = checker_check(&checker);
        ASSERT(nerrs == 0, "san-inline: check ok");

        if (nerrs == 0) {
            CompilerDep deps[1];
            deps[0].name = "my-hasher";
            deps[0].prog = dep_prog;

            Compiler compiler;
            compiler_init_with_deps(&compiler, prog, arena, deps, 1);
            PacketResult packet = compiler_compile(&compiler);

            ASSERT(packet.data != NULL, "san-inline: packet produced");
            ASSERT(compiler.errors.count == 0, "san-inline: no errors");
            /* Should inline the dep, not emit STDLIB_CALL */
            ASSERT(find_instr(&compiler, OP_STR_CONCAT) >= 0,
                   "san-inline: STR_CONCAT from inlined body");
        }
    }

    arena_destroy(arena);
}

/* ── main ────────────────────────────────────────────────── */

int main(void) {
    printf("test_compiler:\n");

    /* Scratchpad */
    test_scratchpad_input_slots();
    test_scratchpad_outputs_after_inputs();

    /* Constant pool */
    test_const_string_dedup();
    test_const_integer();
    test_const_float();
    test_const_boolean();

    /* Literals */
    test_load_nothing();
    test_load_const_string();

    /* Binary ops */
    test_add_integers();
    test_str_concat();
    test_str_concat_mixed();
    test_arithmetic_ops();
    test_comparison_ops();
    test_boolean_ops();
    test_not_op();
    test_negate();

    /* Control flow */
    test_if_else();
    test_match_literal();
    test_match_wildcard();
    test_match_binding();
    test_match_range();

    /* Let bindings */
    test_let_binding();

    /* Record/list */
    test_record_new();
    test_list_new();

    /* Function calls */
    test_to_string_opcode();
    test_stdlib_call();

    /* Iteration */
    test_filter();
    test_map();
    test_fold();
    test_fold_multiply();

    /* Packet structure */
    test_packet_magic();
    test_packet_format_version();
    test_packet_total_size();
    test_packet_section_count();
    test_packet_all_sections_present();
    test_bytecode_section_alignment();

    /* Through pipeline */
    test_through_call();

    /* Access */
    test_record_access();

    /* Edge cases */
    test_tag_contract_rejected();
    test_call_empty_record();
    test_eq_neq();
    test_sanitizer_clear_tag_mode();

    /* Inlining and dependency resolution */
    test_inline_uses_call();
    test_sanitizer_using_stdlib_compile();
    test_unresolved_call_error();
    test_sanitizer_using_inline_dep();

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
