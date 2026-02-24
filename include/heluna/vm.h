#ifndef HELUNA_VM_H
#define HELUNA_VM_H

#include "heluna/compiler.h"
#include "heluna/evaluator.h"
#include "heluna/arena.h"
#include "heluna/errors.h"

#include <stdint.h>
#include <stddef.h>

/* ── Scratchpad slot ─────────────────────────────────────── */

typedef struct {
    HVal    *value;
    uint64_t tags;     /* tag bitfield per slot */
} VmSlot;

/* ── Contract metadata (parsed from binary packet) ───────── */

typedef struct {
    const char *name;
    uint8_t     type_id;
    uint64_t    tag_bits;
    uint16_t    scratchpad_offset;
} VmFieldDecl;

typedef struct {
    const char *name;
    uint8_t     bit_index;
    const char *description;
} VmTagDef;

typedef struct {
    const char *name;
    uint16_t    func_id;
    uint64_t    strips_tags;
} VmSanitizer;

typedef enum {
    VM_RULE_FORBID_FIELD  = 0x01,
    VM_RULE_FORBID_TAGGED = 0x02,
    VM_RULE_REQUIRE       = 0x03,
    VM_RULE_MATCH         = 0x04,
} VmRuleKind;

typedef struct {
    VmRuleKind kind;
    union {
        struct {
            const char *field_name;
            int         is_output;
        } forbid_field;
        struct {
            uint64_t tag_bits;
            int      is_output;
        } forbid_tagged;
        struct {
            const char *field_name;
            const char *reject_msg;
        } require;
        struct {
            const char *field_name;
            const char *reject_msg;
        } match;
    } as;
} VmRule;

/* ── Constant pool entry ─────────────────────────────────── */

typedef struct {
    uint8_t  type_id;
    HVal    *value;
} VmConstant;

/* ── Source metadata (parsed from binary packet) ─────────── */

typedef struct {
    const char *name;         /* source contract name */
    const char *config_json;  /* raw JSON config string */
    const char *keyed_by;     /* key field name */
} VmSource;

/* ── Parsed binary packet ────────────────────────────────── */

typedef struct {
    /* Contract metadata */
    const char   *name;
    uint16_t      scratchpad_size;
    uint16_t      input_count;
    uint16_t      output_count;
    VmFieldDecl  *input_fields;
    VmFieldDecl  *output_fields;

    /* Tags */
    VmTagDef     *tag_defs;
    uint16_t      tag_count;

    /* Sanitizers */
    VmSanitizer  *sanitizers;
    uint16_t      sanitizer_count;

    /* Rules */
    VmRule       *rules;
    uint16_t      rule_count;

    /* Constant pool */
    VmConstant   *constants;
    int           constant_count;

    /* Stdlib dependencies */
    uint16_t     *stdlib_deps;
    int           stdlib_dep_count;

    /* Bytecode */
    Instruction  *instructions;
    int           instr_count;

    /* Sources */
    VmSource     *sources;
    int           source_count;
} VmPacket;

/* ── VM runtime state ────────────────────────────────────── */

typedef struct {
    VmPacket    *packet;
    VmSlot      *scratchpad;
    Arena       *arena;
    HelunaError  error;
    int          had_error;
    HVal       **source_cache;  /* cached loaded source data, one per source */
} Vm;

/* ── Public API ──────────────────────────────────────────── */

/* Parse a binary packet into a VmPacket. Returns NULL on error. */
VmPacket *vm_load_packet(const uint8_t *data, size_t size,
                         Arena *arena, HelunaError *err);

/* Initialize VM runtime state for a loaded packet. */
void vm_init(Vm *vm, VmPacket *packet, Arena *arena);

/* Execute the packet with JSON input, return JSON output.
 * Returns NULL on runtime error (check vm->error). */
HVal *vm_execute(Vm *vm, HVal *input);

/* Stdlib dispatch by function ID. Called by the VM for OP_STDLIB_CALL.
 * Defined in vm_stdlib.c. */
HVal *vm_stdlib_call(uint16_t func_id, HVal *args,
                     Arena *arena, HelunaError *err);

/* Source lookup dispatch. Called by the VM for OP_SOURCE_LOOKUP.
 * Defined in vm_source.c. */
HVal *vm_source_resolve(Vm *vm, int source_idx, HVal *keys);

#endif /* HELUNA_VM_H */
