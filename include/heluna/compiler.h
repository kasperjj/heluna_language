#ifndef HELUNA_COMPILER_H
#define HELUNA_COMPILER_H

#include "heluna/ast.h"
#include "heluna/arena.h"
#include "heluna/errors.h"

#include <stdint.h>

/* ── Packet format constants ─────────────────────────────── */

#define PACKET_MAGIC         0x484C4E41u  /* "HLNA" */
#define PACKET_FORMAT_VER    1
#define PACKET_MIN_SPEC_VER  1
#define PACKET_HEADER_SIZE   88
#define SECTION_DIR_ENTRY    10
#define INSTRUCTION_SIZE     8

/* ── Section type IDs ────────────────────────────────────── */

#define SEC_CONTRACT         0x0001
#define SEC_CONSTANTS        0x0002
#define SEC_STDLIB_DEPS      0x0003
#define SEC_BYTECODE         0x0004
#define SEC_TESTS            0x0101

/* ── Opcodes ─────────────────────────────────────────────── */

/* Scratchpad & constants */
#define OP_LOAD_CONST        0x01
#define OP_LOAD_FIELD        0x02
#define OP_LOAD_NOTHING      0x03
#define OP_COPY              0x04

/* Arithmetic */
#define OP_ADD               0x10
#define OP_SUB               0x11
#define OP_MUL               0x12
#define OP_DIV               0x13
#define OP_MOD               0x14
#define OP_NEGATE            0x15

/* Comparison */
#define OP_EQ                0x20
#define OP_NEQ               0x21
#define OP_LT                0x22
#define OP_GT                0x23
#define OP_LTE               0x24
#define OP_GTE               0x25

/* Boolean logic */
#define OP_AND               0x30
#define OP_OR                0x31
#define OP_NOT               0x32

/* String */
#define OP_STR_CONCAT        0x40

/* Type testing */
#define OP_IS_STRING         0x50
#define OP_IS_INT            0x51
#define OP_IS_FLOAT          0x52
#define OP_IS_BOOL           0x53
#define OP_IS_NOTHING        0x54
#define OP_IS_LIST           0x55
#define OP_IS_RECORD         0x56

/* Type conversion */
#define OP_TO_STRING         0x58
#define OP_TO_INT            0x59
#define OP_TO_FLOAT          0x5A
#define OP_TO_BOOL           0x5B

/* Record operations */
#define OP_RECORD_NEW        0x60
#define OP_RECORD_SET        0x61
#define OP_RECORD_GET        0x62
#define OP_RECORD_HAS        0x63

/* List operations */
#define OP_LIST_NEW          0x70
#define OP_LIST_APPEND       0x71
#define OP_LIST_GET          0x72
#define OP_LIST_LENGTH       0x73

/* Control flow */
#define OP_JUMP              0x80
#define OP_JUMP_IF           0x81
#define OP_JUMP_IF_NOT       0x82

/* Nothing handling */
#define OP_COALESCE          0x85

/* Iteration */
#define OP_ITER_SETUP        0x90
#define OP_ITER_COLLECT      0x91

/* Stdlib dispatch */
#define OP_STDLIB_CALL       0xA0

/* Tag operations */
#define OP_TAG_SET           0xB0
#define OP_TAG_CHECK         0xB1

/* ── Type hint values (flags byte bits 0-2) ──────────────── */

#define THINT_UNSPECIFIED    0
#define THINT_STRING         1
#define THINT_INTEGER        2
#define THINT_FLOAT          3
#define THINT_BOOLEAN        4
#define THINT_LIST           5
#define THINT_RECORD         6
#define THINT_NOTHING        7

/* ── Tag mode values (flags byte bits 3-4) ───────────────── */

#define TMODE_PROPAGATE      0
#define TMODE_CLEAR          1
#define TMODE_SET            2

/* ── Iteration mode (ITER_SETUP flags bits 0-1) ─────────── */

#define ITER_MAP             0
#define ITER_FILTER          1
#define ITER_FOLD            2

/* ── Iteration parallel hint (ITER_SETUP flags bit 2) ────── */

#define ITER_INDEPENDENT     0
#define ITER_SEQUENTIAL      1

/* ── Field type IDs (packet format) ──────────────────────── */

#define FTYPE_STRING         0x01
#define FTYPE_INTEGER        0x02
#define FTYPE_FLOAT          0x03
#define FTYPE_BOOLEAN        0x04
#define FTYPE_NOTHING        0x05
#define FTYPE_MAYBE          0x06
#define FTYPE_LIST           0x07
#define FTYPE_RECORD         0x08

/* ── Instruction struct ──────────────────────────────────── */

typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t dest;
    uint16_t operand1;
    uint16_t operand2;
} Instruction;

/* ── Compile-time type tracking ──────────────────────────── */

typedef enum {
    CTYPE_UNKNOWN,
    CTYPE_STRING,
    CTYPE_INTEGER,
    CTYPE_FLOAT,
    CTYPE_BOOLEAN,
    CTYPE_NOTHING,
    CTYPE_LIST,
    CTYPE_RECORD,
} CompileType;

/* ── Compile result (slot + inferred type) ───────────────── */

typedef struct {
    uint16_t    slot;
    CompileType type;
} CompileResult;

/* ── Compiler errors (same pattern as CheckerErrors) ─────── */

typedef struct {
    HelunaError *errors;
    int          count;
    int          capacity;
    Arena       *arena;
} CompilerErrors;

/* ── Dependency for inlining ─────────────────────────────── */

typedef struct {
    const char       *name;
    const AstProgram *prog;
} CompilerDep;

/* ── Compiler struct ─────────────────────────────────────── */

typedef struct {
    const AstProgram *prog;
    Arena            *arena;
    CompilerErrors    errors;

    /* Instruction buffer */
    Instruction      *instructions;
    int               instr_count;
    int               instr_capacity;

    /* Constant pool */
    uint8_t          *const_data;
    int               const_data_len;
    int               const_data_cap;
    int               const_count;

    /* Constant dedup index (parallel arrays) */
    uint8_t          *const_types;
    int              *const_offsets;
    int              *const_lengths;
    int               const_index_cap;

    /* Scratchpad management */
    int               scratch_next;
    int               input_count;
    int               output_count;

    /* Compile scope */
    struct {
        const char  *name;
        uint16_t     slot;
        CompileType  type;
    }                *scope;
    int               scope_count;
    int               scope_capacity;

    /* Stdlib dependency tracking */
    uint16_t         *stdlib_deps;
    int               stdlib_dep_count;
    int               stdlib_dep_cap;

    /* Tag mapping */
    struct {
        const char  *name;
        int          bit_index;
    }                *tag_map;
    int               tag_map_count;

    /* Dependencies for inlining */
    CompilerDep      *deps;
    int               dep_count;
} Compiler;

/* ── Packet result ───────────────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t   size;
} PacketResult;

/* ── Public API ──────────────────────────────────────────── */

/* Initialize a compiler for a parsed + checked program. */
void compiler_init(Compiler *c, const AstProgram *prog, Arena *arena);

/* Initialize with dependency data for inlining. */
void compiler_init_with_deps(Compiler *c, const AstProgram *prog, Arena *arena,
                             CompilerDep *deps, int dep_count);

/* Compile the program and produce a binary packet.
 * Returns a PacketResult with the packet data and size.
 * On error, data is NULL and size is 0. */
PacketResult compiler_compile(Compiler *c);

#endif /* HELUNA_COMPILER_H */
